/*
 * mindone: device key handler for the flip-camera module's hall switch (F4296).
 * See CAMERA-FLIP-PLAN-1309 for the full design.
 */
package org.mindone.keyhandler;

import android.content.Context;
import android.content.Intent;
import android.provider.Settings;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;

import com.android.internal.os.DeviceKeyHandler;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

/**
 * The phone has a single physical camera module the user flips by hand between a "back" and a
 * "front" position; a hall switch reports the transition. The kernel (mtk_kpd.c,
 * hall_eint_handler(), F4296) reproduces the factory behaviour: it emits an EV_KEY press+release
 * of KEY_F2 on one edge and KEY_F3 on the other, on the "mtk-kpd" input device, exactly like the
 * two logical keys the stock MediaTek camera app used to consume.
 *
 * This handler:
 *  1. records the new position in {@link CameraFlipContract#SETTING_CAMERA_FLIPPED} (a
 *     Settings.System key) so any app (in practice, the camera app) can read the current
 *     position without needing any permission or observing anything;
 *  2. broadcasts {@link CameraFlipContract#ACTION_CAMERA_FLIPPED} so a running app can react
 *     immediately instead of polling Settings;
 *  3. fully consumes the key (returns null) so plain KEYCODE_F2 / KEYCODE_F3 never reach any app.
 *
 * Registered via config_deviceKeyHandlerLibs / config_deviceKeyHandlerClasses (overlay/lineage-
 * sdk/lineage/res/res/values/config.xml) and instantiated by PhoneWindowManager through
 * reflection: {@code new PathClassLoader(apkPath, ...).loadClass(...).getConstructor(
 * Context.class).newInstance(mContext)}, running inside system_server. handleKeyEvent() is
 * called from PhoneWindowManager#interceptKeyBeforeQueueing(), i.e. before the key is queued to
 * any window, so returning null here really does mean no app ever sees the event.
 *
 * KEY MAPPING IS UNVERIFIED ON HARDWARE (device-free implementation, see plan doc "Decisions"):
 * the kernel driver only guarantees that KEY_F2 and KEY_F3 are the two opposite hall transitions
 * -- it does not (and, being a hall switch wired to a GPIO level, cannot on its own) know which
 * physical orientation ("lens facing the user" vs. "lens facing away") each one corresponds to.
 * {@link #POSITION_FOR_KEYCODE_F2} below is the single point to flip if on-device testing
 * (getevent + watching the preview) shows the mapping is inverted.
 */
public class CameraFlipKeyHandler implements DeviceKeyHandler {
    private static final String TAG = "MindOneCameraFlip";

    /**
     * Position reported when KEY_F2 fires; KEY_F3 reports the other one. See the class doc.
     *
     * 🔴 15.09 (F4458): VERIFIED ON HARDWARE and inverted. On the device, flipping the
     * module does switch the camera, but the preview comes out upside down in BOTH positions -
     * which is exactly what an inverted mapping looks like: each position selects the logical
     * camera meant for the other one, and the two differ by 180 deg in SENSOR_ORIENTATION, so
     * neither position is ever right. Was POSITION_FRONT (the device-free guess).
     */
    private static final int POSITION_FOR_KEYCODE_F2 = CameraFlipContract.POSITION_BACK;

    // Best-effort source for the position at boot, before any flip has happened this session.
    // Not implemented in the kernel yet (F4296 follow-up, see the plan doc) -- reading this
    // always fails today, which is handled gracefully (position stays whatever Settings.System
    // already had from a previous boot, or unknown on first boot ever). Kept as a plain sysfs
    // scan, matched by the input device name rather than a hardcoded eventN, so this starts
    // working with zero app-side changes the day the attribute is added.
    /**
     * Boot-time position exported by our mtk_kpd driver (the kernel module tree mtk_kpd.c, hall_position_show()):
     * "1" = the KEY_F2 side, "0" = the KEY_F3 side, "-1" = hall switch not registered. The path
     * is the keypad platform device from the DT (kp@10010000); it is a fixed path on purpose so
     * that system_server needs read access to exactly one labelled sysfs file
     * (sepolicy/vendor: sysfs_mindone_hall) and no directory walk over /sys/class/input.
     */
    private static final String HALL_POSITION_PATH =
            "/sys/devices/platform/soc/10010000.kp/hall_position";

    private final Context mContext;

    public CameraFlipKeyHandler(Context context) {
        mContext = context;
        tryInitializeFromKernelBootState();
    }

    @Override
    public KeyEvent handleKeyEvent(KeyEvent event) {
        final int keyCode = event.getKeyCode();
        if (keyCode != KeyEvent.KEYCODE_F2 && keyCode != KeyEvent.KEYCODE_F3) {
            return event;
        }

        final InputDevice device = event.getDevice();
        if (device == null || !CameraFlipContract.INPUT_DEVICE_NAME.equals(device.getName())) {
            // A real F2/F3 from some other input source (e.g. a plugged-in keyboard) -- not
            // ours, let the framework deliver it normally.
            return event;
        }

        // The kernel emits a press *and* a release for every edge; apply the position change
        // once, on the down event, and swallow both so apps never see F2/F3 at all.
        if (event.getAction() == KeyEvent.ACTION_DOWN) {
            final int position = (keyCode == KeyEvent.KEYCODE_F2)
                    ? POSITION_FOR_KEYCODE_F2
                    : otherPosition(POSITION_FOR_KEYCODE_F2);
            setPosition(position);
        }

        return null;
    }

    private static int otherPosition(int position) {
        return position == CameraFlipContract.POSITION_FRONT
                ? CameraFlipContract.POSITION_BACK
                : CameraFlipContract.POSITION_FRONT;
    }

    private void setPosition(int position) {
        Settings.System.putInt(mContext.getContentResolver(),
                CameraFlipContract.SETTING_CAMERA_FLIPPED, position);

        Intent intent = new Intent(CameraFlipContract.ACTION_CAMERA_FLIPPED);
        intent.putExtra(CameraFlipContract.EXTRA_POSITION, position);
        intent.setFlags(Intent.FLAG_RECEIVER_REGISTERED_ONLY | Intent.FLAG_RECEIVER_FOREGROUND);
        mContext.sendBroadcast(intent, CameraFlipContract.PERMISSION_RECEIVE);

        Log.i(TAG, "camera flip position=" + position);
    }

    private void tryInitializeFromKernelBootState() {
        try {
            String value = readFirstLine(new File(HALL_POSITION_PATH));
            if ("1".equals(value)) {
                setPosition(POSITION_FOR_KEYCODE_F2);
            } else if ("0".equals(value)) {
                setPosition(otherPosition(POSITION_FOR_KEYCODE_F2));
            } else {
                Log.d(TAG, "kernel hall position unavailable: " + value);
            }
        } catch (Exception e) {
            // Attribute does not exist (older kernel module), or is not readable -- stay with
            // whatever Settings.System already has from a previous boot, or unknown. Never crash
            // system_server over a best-effort boot hint.
            Log.d(TAG, "no kernel boot-time hall position available", e);
        }
    }

    private static String readFirstLine(File file) throws Exception {
        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            String line = reader.readLine();
            return line != null ? line.trim() : null;
        }
    }
}
