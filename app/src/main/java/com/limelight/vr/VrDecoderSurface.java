package com.limelight.vr;

import android.graphics.Canvas;
import android.graphics.Rect;
import android.view.Surface;
import android.view.SurfaceHolder;

/**
 * Gives MediaCodec a Surface backed by the VR video texture.
 * The decoder only calls getSurface().
 */
final class VrDecoderSurface implements SurfaceHolder {
    private final Surface surface;
    private final Rect frame = new Rect();

    VrDecoderSurface(Surface surface) {
        this.surface = surface;
    }

    void setBufferSize(int width, int height) {
        frame.set(0, 0, Math.max(width, 0), Math.max(height, 0));
    }

    @Override
    public Surface getSurface() {
        return surface;
    }

    @Override
    public Rect getSurfaceFrame() {
        return new Rect(frame);
    }

    @Override
    public boolean isCreating() {
        return false;
    }

    @Override
    public void addCallback(Callback callback) {
    }

    @Override
    public void removeCallback(Callback callback) {
    }

    @Override
    public void setFixedSize(int width, int height) {
        setBufferSize(width, height);
    }

    @Override
    public void setSizeFromLayout() {
    }

    @Override
    public void setFormat(int format) {
    }

    @Override
    public void setKeepScreenOn(boolean screenOn) {
    }

    @Override
    @SuppressWarnings("deprecation")
    public void setType(int type) {
    }

    @Override
    public Canvas lockCanvas() {
        return null;
    }

    @Override
    public Canvas lockCanvas(Rect dirty) {
        return null;
    }

    @Override
    public Canvas lockHardwareCanvas() {
        return null;
    }

    @Override
    public void unlockCanvasAndPost(Canvas canvas) {
    }
}
