package com.limelight.vr;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

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

    /**
     * Anti-aliased atlas for ASCII 32..126. The native UI blits this instead of
     * the 8x8 debug font. Direct buffer, native byte order:
     * 8 ints (width, height, count, ascent, fontPx, descent, 0, 0), then
     * count records of 8 ints (srcX, srcY, srcW, srcH, advance, offsetX,
     * offsetY, 0), then tightly packed RGBA.
     */
    public static ByteBuffer createGlyphAtlas(int textPx) {
        if (textPx < 8) {
            textPx = 8;
        }
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        paint.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL));
        paint.setTextSize(textPx);
        paint.setColor(Color.WHITE);
        paint.setAntiAlias(true);
        paint.setDither(true);

        final int count = 95;
        final int pad = 1;
        final int atlasW = 1024;
        int[] srcX = new int[count];
        int[] srcY = new int[count];
        int[] srcW = new int[count];
        int[] srcH = new int[count];
        int[] advance = new int[count];
        int[] offsetX = new int[count];
        int[] offsetY = new int[count];
        Rect bounds = new Rect();
        int penX = 1;
        int penY = 1;
        int rowH = 0;
        for (int i = 0; i < count; i++) {
            String letter = String.valueOf((char) (32 + i));
            paint.getTextBounds(letter, 0, 1, bounds);
            boolean empty = bounds.width() <= 0 || bounds.height() <= 0;
            int gw = empty ? 0 : bounds.width() + pad * 2;
            int gh = empty ? 0 : bounds.height() + pad * 2;
            if (gw > 0 && penX > 1 && penX + gw + 1 > atlasW) {
                penY += rowH + 1;
                penX = 1;
                rowH = 0;
            }
            srcX[i] = penX;
            srcY[i] = penY;
            srcW[i] = gw;
            srcH[i] = gh;
            advance[i] = Math.round(paint.measureText(letter));
            offsetX[i] = empty ? 0 : bounds.left - pad;
            offsetY[i] = empty ? 0 : bounds.top - pad;
            if (gw > 0) {
                penX += gw + 1;
                if (gh > rowH) {
                    rowH = gh;
                }
            }
        }
        int atlasH = penY + rowH + 1;
        if (atlasH < 2 || atlasH > 4096) {
            return null;
        }

        Bitmap bitmap = Bitmap.createBitmap(atlasW, atlasH, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        for (int i = 0; i < count; i++) {
            if (srcW[i] <= 0) {
                continue;
            }
            String letter = String.valueOf((char) (32 + i));
            paint.getTextBounds(letter, 0, 1, bounds);
            canvas.drawText(letter, srcX[i] + pad - bounds.left, srcY[i] + pad - bounds.top, paint);
        }

        Paint.FontMetricsInt metrics = paint.getFontMetricsInt();
        int headerInts = 8 + count * 8;
        ByteBuffer buffer = ByteBuffer.allocateDirect(headerInts * 4 + atlasW * atlasH * 4);
        buffer.order(ByteOrder.nativeOrder());
        buffer.putInt(atlasW);
        buffer.putInt(atlasH);
        buffer.putInt(count);
        buffer.putInt(-metrics.ascent);
        buffer.putInt(textPx);
        buffer.putInt(metrics.descent);
        buffer.putInt(0);
        buffer.putInt(0);
        for (int i = 0; i < count; i++) {
            buffer.putInt(srcX[i]);
            buffer.putInt(srcY[i]);
            buffer.putInt(srcW[i]);
            buffer.putInt(srcH[i]);
            buffer.putInt(advance[i]);
            buffer.putInt(offsetX[i]);
            buffer.putInt(offsetY[i]);
            buffer.putInt(0);
        }
        int[] pixels = new int[atlasW * atlasH];
        bitmap.getPixels(pixels, 0, atlasW, 0, 0, atlasW, atlasH);
        bitmap.recycle();
        for (int pixel : pixels) {
            buffer.put((byte) ((pixel >> 16) & 0xff));
            buffer.put((byte) ((pixel >> 8) & 0xff));
            buffer.put((byte) (pixel & 0xff));
            buffer.put((byte) ((pixel >>> 24) & 0xff));
        }
        buffer.position(0);
        return buffer;
    }
}
