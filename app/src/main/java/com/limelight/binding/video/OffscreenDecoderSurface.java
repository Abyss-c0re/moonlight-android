package com.limelight.binding.video;

import android.graphics.SurfaceTexture;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES11Ext;
import android.opengl.GLES20;
import android.os.Handler;
import android.os.HandlerThread;
import android.view.Surface;

import com.limelight.LimeLog;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * A live MediaCodec output target that is not a View.
 * The decoder can keep draining after the Activity surface is destroyed.
 */
final class OffscreenDecoderSurface {
    private final Object lock = new Object();
    private HandlerThread thread;
    private Handler handler;
    private EGLDisplay eglDisplay = EGL14.EGL_NO_DISPLAY;
    private EGLContext eglContext = EGL14.EGL_NO_CONTEXT;
    private EGLSurface eglSurface = EGL14.EGL_NO_SURFACE;
    private SurfaceTexture texture;
    private Surface surface;
    private int textureName;

    Surface acquire(int width, int height) {
        synchronized (lock) {
            if (surface != null && surface.isValid()) {
                return surface;
            }
            releaseLocked();
            final int w = Math.max(width, 16);
            final int h = Math.max(height, 16);
            thread = new HandlerThread("Video - Offscreen");
            thread.start();
            handler = new Handler(thread.getLooper());
            final CountDownLatch latch = new CountDownLatch(1);
            final Surface[] created = new Surface[1];
            handler.post(new Runnable() {
                @Override
                public void run() {
                    try {
                        created[0] = createOnGlThread(w, h);
                    } catch (RuntimeException e) {
                        LimeLog.warning("Offscreen decoder surface failed: " + e.getMessage());
                        destroyGl();
                    } finally {
                        latch.countDown();
                    }
                }
            });
            try {
                if (!latch.await(2, TimeUnit.SECONDS)) {
                    LimeLog.warning("Offscreen decoder surface timed out");
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            surface = created[0];
            if (surface == null) {
                releaseLocked();
            }
            return surface;
        }
    }

    Surface getSurface() {
        synchronized (lock) {
            return surface;
        }
    }

    void release() {
        synchronized (lock) {
            releaseLocked();
        }
    }

    private void releaseLocked() {
        final Handler localHandler = handler;
        final HandlerThread localThread = thread;
        handler = null;
        thread = null;
        if (localHandler == null || localThread == null) {
            surface = null;
            texture = null;
            return;
        }
        final CountDownLatch latch = new CountDownLatch(1);
        localHandler.post(new Runnable() {
            @Override
            public void run() {
                try {
                    destroyGl();
                } finally {
                    latch.countDown();
                }
            }
        });
        try {
            latch.await(1, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        surface = null;
        texture = null;
        localThread.quitSafely();
    }

    private Surface createOnGlThread(int width, int height) {
        eglDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        if (eglDisplay == EGL14.EGL_NO_DISPLAY) {
            return null;
        }
        int[] version = new int[2];
        if (!EGL14.eglInitialize(eglDisplay, version, 0, version, 1)) {
            return null;
        }
        int[] attrib = {
                EGL14.EGL_RED_SIZE, 8,
                EGL14.EGL_GREEN_SIZE, 8,
                EGL14.EGL_BLUE_SIZE, 8,
                EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                EGL14.EGL_NONE
        };
        EGLConfig[] configs = new EGLConfig[1];
        int[] num = new int[1];
        if (!EGL14.eglChooseConfig(eglDisplay, attrib, 0, configs, 0, 1, num, 0) || num[0] < 1) {
            return null;
        }
        int[] ctxAttrib = {
                EGL14.EGL_CONTEXT_CLIENT_VERSION, 2,
                EGL14.EGL_NONE
        };
        eglContext = EGL14.eglCreateContext(eglDisplay, configs[0], EGL14.EGL_NO_CONTEXT, ctxAttrib, 0);
        if (eglContext == EGL14.EGL_NO_CONTEXT) {
            return null;
        }
        int[] pbuffer = {
                EGL14.EGL_WIDTH, 1,
                EGL14.EGL_HEIGHT, 1,
                EGL14.EGL_NONE
        };
        eglSurface = EGL14.eglCreatePbufferSurface(eglDisplay, configs[0], pbuffer, 0);
        if (eglSurface == EGL14.EGL_NO_SURFACE) {
            return null;
        }
        if (!EGL14.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
            return null;
        }

        int[] textures = new int[1];
        GLES20.glGenTextures(1, textures, 0);
        textureName = textures[0];
        if (textureName == 0) {
            return null;
        }
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureName);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);

        texture = new SurfaceTexture(textureName);
        texture.setDefaultBufferSize(width, height);
        texture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
            @Override
            public void onFrameAvailable(SurfaceTexture surfaceTexture) {
                try {
                    surfaceTexture.updateTexImage();
                } catch (RuntimeException ignored) {
                }
            }
        }, handler);
        Surface created = new Surface(texture);
        LimeLog.info("Offscreen decoder surface " + width + "x" + height);
        return created;
    }

    private void destroyGl() {
        if (surface != null) {
            surface.release();
            surface = null;
        }
        if (texture != null) {
            texture.release();
            texture = null;
        }
        if (textureName != 0 && eglDisplay != EGL14.EGL_NO_DISPLAY && eglContext != EGL14.EGL_NO_CONTEXT) {
            GLES20.glDeleteTextures(1, new int[]{textureName}, 0);
            textureName = 0;
        }
        if (eglDisplay != EGL14.EGL_NO_DISPLAY) {
            EGL14.eglMakeCurrent(eglDisplay, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
        }
        if (eglSurface != EGL14.EGL_NO_SURFACE) {
            EGL14.eglDestroySurface(eglDisplay, eglSurface);
            eglSurface = EGL14.EGL_NO_SURFACE;
        }
        if (eglContext != EGL14.EGL_NO_CONTEXT) {
            EGL14.eglDestroyContext(eglDisplay, eglContext);
            eglContext = EGL14.EGL_NO_CONTEXT;
        }
        if (eglDisplay != EGL14.EGL_NO_DISPLAY) {
            EGL14.eglTerminate(eglDisplay);
            eglDisplay = EGL14.EGL_NO_DISPLAY;
        }
    }
}
