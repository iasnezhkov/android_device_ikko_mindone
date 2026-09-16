/*
 * mindone: contract constants for the flip-camera module position (F4296).
 * See CAMERA-FLIP-PLAN-1309.
 *
 * Mirrored (not shared) in Aperture's own sources as
 * app/src/main/java/org/lineageos/aperture/MindOneCameraFlip.kt, since Aperture cannot depend on
 * a device-tree-local system component at build time. Keep both copies in sync.
 */
package org.mindone.keyhandler;

public final class CameraFlipContract {
    private CameraFlipContract() {}

    /** Name of the mtk-kpd input device the flip events are filtered by (KPD_NAME in mtk_kpd.c). */
    public static final String INPUT_DEVICE_NAME = "mtk-kpd";

    /**
     * Settings.System integer key holding the last position recorded for the flip module.
     * Values: {@link #POSITION_BACK} or {@link #POSITION_FRONT}; absent (default
     * {@link #POSITION_UNKNOWN}) means the module has not been flipped since this boot and the
     * kernel does not yet expose the boot-time hall level (see the kernel follow-up in the plan
     * doc). Chosen over a system property specifically because it is readable by any app,
     * including Aperture, with no extra permission and no sepolicy -- see the plan doc's
     * "Settings.System vs. sysprop" section for the full comparison.
     */
    public static final String SETTING_CAMERA_FLIPPED = "mindone_camera_flipped";
    public static final int POSITION_BACK = 0;
    public static final int POSITION_FRONT = 1;
    public static final int POSITION_UNKNOWN = -1;

    /** Sent every time the recorded position changes. Declared <protected-broadcast>. */
    public static final String ACTION_CAMERA_FLIPPED = "org.mindone.intent.action.CAMERA_FLIPPED";

    /** int extra on {@link #ACTION_CAMERA_FLIPPED}, same values as {@link #SETTING_CAMERA_FLIPPED}. */
    public static final String EXTRA_POSITION = "org.mindone.intent.extra.POSITION";

    /** Signature-level permission required to receive {@link #ACTION_CAMERA_FLIPPED}. */
    public static final String PERMISSION_RECEIVE = "org.mindone.permission.RECEIVE_CAMERA_FLIP_STATE";
}
