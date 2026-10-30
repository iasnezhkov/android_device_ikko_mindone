// SPDX-License-Identifier: Apache-2.0

package org.mindone.keyhandler;

public final class CameraFlipContract {
    private CameraFlipContract() {}

    public static final String INPUT_DEVICE_NAME = "mtk-kpd";

    public static final String SETTING_CAMERA_FLIPPED = "mindone_camera_flipped";
    public static final int POSITION_BACK = 0;
    public static final int POSITION_FRONT = 1;
    public static final int POSITION_UNKNOWN = -1;

    public static final String ACTION_CAMERA_FLIPPED = "org.mindone.intent.action.CAMERA_FLIPPED";

    public static final String EXTRA_POSITION = "org.mindone.intent.extra.POSITION";

    public static final String PERMISSION_RECEIVE = "org.mindone.permission.RECEIVE_CAMERA_FLIP_STATE";
}
