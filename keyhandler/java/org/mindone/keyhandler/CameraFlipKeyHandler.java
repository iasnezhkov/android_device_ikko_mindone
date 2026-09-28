// SPDX-License-Identifier: Apache-2.0

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

public class CameraFlipKeyHandler implements DeviceKeyHandler {
    private static final String TAG = "MindOneCameraFlip";

    private static final int POSITION_FOR_KEYCODE_F2 = CameraFlipContract.POSITION_BACK;

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
