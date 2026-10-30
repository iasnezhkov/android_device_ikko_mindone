# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
import os
from pathlib import Path
import runpy
import signal
import stat
import subprocess
import sys
import tempfile
import unittest


RIG = runpy.run_path(str(Path(__file__).with_name("android_check.py")), run_name="rig_helpers")
CHILD = "import os,signal,sys,time;from pathlib import Path;signal.signal(signal.SIGTERM,signal.SIG_IGN);Path(sys.argv[1]).write_text(str(os.getpid()));time.sleep(120)"
PARENT = """import subprocess,sys,time
from pathlib import Path
subprocess.Popen([sys.executable,'-c',sys.argv[1],sys.argv[2]])
deadline=time.monotonic()+5
while not Path(sys.argv[2]).exists():
    if time.monotonic()>=deadline:
        raise SystemExit(2)
    time.sleep(0.01)
if sys.argv[3]=='wait':
    time.sleep(120)
"""


class ProcessCleanupTests(unittest.TestCase):
    def command(self, marker, mode):
        return [sys.executable, "-c", PARENT, CHILD, str(marker), mode]

    def emergency_stop(self, group):
        try:
            os.killpg(group, signal.SIGKILL)
        except ProcessLookupError:
            pass

    def stop_recorded_child(self, marker):
        if not marker.exists():
            return
        try:
            self.emergency_stop(os.getpgid(int(marker.read_text())))
        except ProcessLookupError:
            pass

    def test_cleanup_kills_a_term_ignoring_descendant_after_leader_exit(self):
        with tempfile.TemporaryDirectory(prefix="android-cleanup-control-") as directory:
            marker = Path(directory) / "child.pid"
            parent = subprocess.Popen(self.command(marker, "exit"), start_new_session=True)
            try:
                self.assertEqual(parent.wait(timeout=8), 0)
                descendant = int(marker.read_text())
                self.assertIn(descendant, RIG["live_group"](parent.pid))
                self.assertTrue(RIG["stop_process"](parent))
                self.assertFalse(RIG["live_group"](parent.pid))
            finally:
                self.emergency_stop(parent.pid)
                parent.wait(timeout=5)

    def test_successful_step_with_a_live_descendant_is_rejected_and_cleaned(self):
        with tempfile.TemporaryDirectory(prefix="android-success-control-") as directory:
            output = Path(directory)
            marker = output / "child.pid"
            group = None
            try:
                result = RIG["execute_step"](self.command(marker, "exit"), output, os.environ.copy(), output / "step.log", 8)
                group = result["process_group"]
                self.assertEqual(result["rc"], 125)
                self.assertTrue(result["descendants_stopped"])
                self.assertTrue(marker.exists())
                self.assertFalse(RIG["live_group"](group))
            finally:
                if group is not None:
                    self.emergency_stop(group)
                self.stop_recorded_child(marker)

    def test_timeout_stops_a_term_ignoring_process_group(self):
        with tempfile.TemporaryDirectory(prefix="android-timeout-control-") as directory:
            output = Path(directory)
            marker = output / "child.pid"
            try:
                result = RIG["execute_step"](self.command(marker, "wait"), output, os.environ.copy(), output / "step.log", 1)
                self.assertEqual(result["rc"], 124)
                self.assertTrue(result["descendants_stopped"])
                self.assertTrue(marker.exists())
                group = result["process_group"]
                self.assertFalse(RIG["live_group"](group))
            finally:
                self.stop_recorded_child(marker)

    def test_clean_step_keeps_its_success_status(self):
        with tempfile.TemporaryDirectory(prefix="android-clean-control-") as directory:
            output = Path(directory)
            result = RIG["execute_step"]([sys.executable, "-c", "print('complete')"], output, os.environ.copy(), output / "step.log", 5)
            self.assertEqual(result["rc"], 0)
            self.assertFalse(result["descendants_stopped"])

    def test_temporary_directory_is_private_and_ignores_a_predictable_symlink(self):
        with tempfile.TemporaryDirectory(prefix="android-directory-control-") as directory:
            output = Path(directory)
            protected = output / "protected"
            protected.mkdir()
            (output / "tmp").symlink_to(protected, target_is_directory=True)
            temporary = RIG["private_directory"](output)
            self.assertNotEqual(temporary, output / "tmp")
            self.assertFalse(temporary.is_symlink())
            self.assertEqual(stat.S_IMODE(temporary.stat().st_mode), 0o700)
            self.assertFalse(list(protected.iterdir()))


if __name__ == "__main__":
    unittest.main()
