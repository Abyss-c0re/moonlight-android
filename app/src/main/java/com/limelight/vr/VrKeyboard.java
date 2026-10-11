package com.limelight.vr;

import android.content.Context;
import android.content.res.AssetManager;

import com.limelight.LimeLog;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;

/**
 * Loads the VR keyboard layout and hands the text to native code.
 * A non-empty file in the app's files directory replaces the packaged layout.
 * The native parser runs on the OpenXR thread.
 */
final class VrKeyboard {
    private static final int MAX_BYTES = 47999;
    private static final String FILE_NAME = "vr_keyboard.cfg";

    private VrKeyboard() {
    }

    static void apply(Context context) {
        if (context == null) {
            return;
        }
        File override = new File(context.getFilesDir(), FILE_NAME);
        String text = readOverride(override);
        if (text == null) {
            text = readAsset(context.getAssets(), override);
        }
        if (text == null || text.length() == 0) {
            LimeLog.warning("VR keyboard config is empty");
            return;
        }
        MoonlightXr.nativeSetKeyboard(text);
    }

    private static String readOverride(File file) {
        FileInputStream in = null;
        try {
            if (!file.isFile()) {
                return null;
            }
            long length = file.length();
            if (length <= 0 || length > MAX_BYTES) {
                LimeLog.warning("VR keyboard file skipped (" + length + " bytes) " + file.getAbsolutePath());
                return null;
            }
            byte[] bytes = new byte[(int) length];
            in = new FileInputStream(file);
            int off = 0;
            while (off < bytes.length) {
                int n = in.read(bytes, off, bytes.length - off);
                if (n < 0) {
                    break;
                }
                off += n;
            }
            if (off <= 0) {
                return null;
            }
            LimeLog.info("VR keyboard file " + file.getAbsolutePath());
            return new String(bytes, 0, off, "UTF-8");
        } catch (Exception e) {
            LimeLog.warning("VR keyboard file failed: " + e.getMessage());
            return null;
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    private static String readAsset(AssetManager assets, File override) {
        InputStream in = null;
        try {
            in = assets.open(FILE_NAME);
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] chunk = new byte[4096];
            int total = 0;
            while (total <= MAX_BYTES) {
                int n = in.read(chunk, 0, Math.min(chunk.length, MAX_BYTES + 1 - total));
                if (n <= 0) {
                    break;
                }
                total += n;
                if (total > MAX_BYTES) {
                    LimeLog.warning("VR keyboard asset is too large");
                    return null;
                }
                out.write(chunk, 0, n);
            }
            if (total <= 0) {
                return null;
            }
            LimeLog.info("VR keyboard asset; override " + override.getAbsolutePath());
            return new String(out.toByteArray(), "UTF-8");
        } catch (Exception e) {
            LimeLog.warning("VR keyboard asset failed: " + e.getMessage());
            return null;
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (Exception ignored) {
                }
            }
        }
    }
}
