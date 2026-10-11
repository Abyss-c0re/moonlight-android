package com.limelight.vr;

public final class MoonlightXr {
    public static final int ACT_SPAWN = 1;
    public static final int ACT_CLOSE = 2;
    public static final int ACT_PASSTHROUGH = 3;
    public static final int ACT_RESET = 4;
    public static final int ACT_EXIT = 5;
    public static final int ACT_PC = 6;
    public static final int ACT_APP = 7;
    public static final int ACT_BACK = 8;

    static {
        System.loadLibrary("moonlight-xr");
    }

    private MoonlightXr() {
    }

    public static native boolean nativeStart(VrActivity activity);

    public static native void nativeRequestExit();

    public static native void nativeShutdown();

    public static native void nativeSetLibrary(String[] labels, int[] kinds, int[] ids);

    public static native void nativeSetStatus(String status);

    public static native void nativeClearVideo();
}
