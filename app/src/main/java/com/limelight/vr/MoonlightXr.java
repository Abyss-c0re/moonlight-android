package com.limelight.vr;

public final class MoonlightXr {
    static {
        System.loadLibrary("moonlight-xr");
    }

    private MoonlightXr() {
    }

    public static native boolean nativeStart(VrActivity activity);

    public static native void nativeRequestExit();

    public static native void nativeShutdown();
}
