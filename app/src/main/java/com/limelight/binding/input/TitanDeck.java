package com.limelight.binding.input;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.widget.FrameLayout;

import com.limelight.LimeLog;
import com.limelight.nvstream.NvConnection;
import com.limelight.nvstream.input.KeyboardPacket;
import com.limelight.nvstream.input.MouseButtonPacket;
import com.limelight.preferences.PreferenceConfiguration;
import com.limelight.ui.StreamView;
import com.titanus2.api.ControlPlane;
import com.titanus2.api.KeyInputTiming;
import com.titanus2.api.Titan2ApiContract;
import com.titanus2.api.Titan2Client;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Titan 2 stream session: square-screen key bars, and the same Controls
 * API Atlas and USB HID use (pad mode, key timing, per-app side keys).
 */
public final class TitanDeck implements TitanDeckBars.Host {
    public static final String LAYER = Titan2ApiContract.LAYER_MOONLIGHT_STREAM;
    private static final String TAG = "TitanDeck";

    private final Activity activity;
    private final StreamView streamView;
    private final TitanDeckBars bars;
    private final Titan2Client client;
    private final ExecutorService exec;
    private final BroadcastReceiver remoteInput;

    private KeyboardTranslator translator;
    private NvConnection conn;
    private Snap snap;
    private boolean sessionQueued;
    private boolean ctrl;
    private boolean alt;
    private boolean shift;
    private boolean meta;
    private boolean caps;
    private byte heldMouse;

    public TitanDeck(Activity activity, StreamView streamView) {
        this.activity = activity;
        this.streamView = streamView;
        this.bars = new TitanDeckBars(activity, this);
        this.client = new Titan2Client(activity);
        this.exec = Executors.newSingleThreadExecutor(r -> {
            Thread t = new Thread(r, "titan-deck");
            t.setDaemon(true);
            return t;
        });
        this.remoteInput = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                if (conn == null || intent == null) return;
                handleRemote(intent);
            }
        };
        FrameLayout parent = (FrameLayout) streamView.getParent();
        FrameLayout.LayoutParams topLp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, 0, Gravity.TOP);
        FrameLayout.LayoutParams botLp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, 0, Gravity.BOTTOM);
        parent.addView(bars.top(), topLp);
        parent.addView(bars.bottom(), botLp);
        IntentFilter filter = new IntentFilter(Titan2ApiContract.ACTION_REMOTE_INPUT);
        filter.addAction(Titan2ApiContract.ACTION_HOST_MOUSE);
        // Sender must hold the Controls permission. Moonlight itself cannot:
        // the permission is signature|privileged.
        if (Build.VERSION.SDK_INT >= 33) {
            activity.registerReceiver(remoteInput, filter,
                    Titan2ApiContract.PERMISSION_USE, null, Context.RECEIVER_EXPORTED);
        } else {
            activity.registerReceiver(remoteInput, filter,
                    Titan2ApiContract.PERMISSION_USE, null);
        }
    }

    /** Push the Moonlight per-app key profile into Titan Controls. */
    public static void publishProfile(Context context) {
        PreferenceConfiguration prefs = PreferenceConfiguration.readPreferences(context);
        Titan2Client api = new Titan2Client(context);
        try {
            api.connect();
            writeProfile(context, api, prefs);
        } finally {
            api.shutdown();
        }
    }

    public void layout(PreferenceConfiguration prefs) {
        if (prefs == null) return;
        boolean pip = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                && activity.isInPictureInPictureMode();
        FrameLayout parent = (FrameLayout) streamView.getParent();
        int screenW = parent.getWidth();
        int screenH = parent.getHeight();
        if (screenW <= 0 || screenH <= 0) {
            screenW = activity.getWindowManager().getDefaultDisplay().getWidth();
            screenH = activity.getWindowManager().getDefaultDisplay().getHeight();
        }
        boolean top = false;
        boolean bottom = false;
        if (!pip) {
            String mode = prefs.titanDeckPanels == null ? "auto" : prefs.titanDeckPanels;
            if ("top".equals(mode)) top = true;
            else if ("bottom".equals(mode)) bottom = true;
            else if ("both".equals(mode)) {
                top = true;
                bottom = true;
            } else if ("auto".equals(mode)) {
                double streamAspect = prefs.width / (double) Math.max(1, prefs.height);
                int naturalH = (int) (screenW / streamAspect);
                int leftover = screenH - Math.min(screenH, naturalH);
                boolean squarish = PreferenceConfiguration.isSquarishScreen(screenW, screenH);
                if (squarish && leftover > dp(48)) {
                    top = true;
                    bottom = true;
                }
            }
        }
        boolean anyKeys = prefs.titanDeckNav || prefs.titanDeckMods || prefs.titanDeckFn
                || prefs.titanDeckEdit || prefs.titanDeckSymbols;
        if (!anyKeys) {
            top = false;
            bottom = false;
        }
        int rowH = rowHeightPx(prefs);
        bars.setRowHeightPx(rowH);
        int topH = top ? bars.rowCount(prefs, true) * rowH : 0;
        int botH = bottom ? bars.rowCount(prefs, false) * rowH : 0;
        int minVideo = dp(120);
        if (topH + botH > screenH - minVideo) {
            int room = Math.max(0, screenH - minVideo);
            if (topH + botH > 0) {
                topH = room * topH / (topH + botH);
                botH = room - topH;
            }
        }
        FrameLayout.LayoutParams topLp = (FrameLayout.LayoutParams) bars.top().getLayoutParams();
        FrameLayout.LayoutParams botLp = (FrameLayout.LayoutParams) bars.bottom().getLayoutParams();
        topLp.height = topH;
        botLp.height = botH;
        bars.top().setLayoutParams(topLp);
        bars.bottom().setLayoutParams(botLp);
        bars.reload(prefs, topH > 0, botH > 0);

        FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) streamView.getLayoutParams();
        if (topH == 0 && botH == 0) {
            lp.width = FrameLayout.LayoutParams.MATCH_PARENT;
            lp.height = FrameLayout.LayoutParams.MATCH_PARENT;
            lp.gravity = Gravity.CENTER;
            lp.topMargin = 0;
            lp.leftMargin = 0;
            streamView.setLayoutParams(lp);
            streamView.setDesiredAspectRatio((double) prefs.width / (double) Math.max(1, prefs.height));
            return;
        }
        int videoH = Math.max(dp(80), screenH - topH - botH);
        double aspect = prefs.width / (double) Math.max(1, prefs.height);
        int videoW = Math.min(screenW, (int) (videoH * aspect));
        videoH = Math.min(videoH, (int) (videoW / aspect));
        lp.width = videoW;
        lp.height = videoH;
        lp.gravity = Gravity.TOP | Gravity.CENTER_HORIZONTAL;
        lp.topMargin = topH + Math.max(0, (screenH - topH - botH - videoH) / 2);
        lp.leftMargin = 0;
        streamView.setLayoutParams(lp);
        streamView.setDesiredAspectRatio(aspect);
    }

    public void bindSession(NvConnection connection, KeyboardTranslator translator) {
        this.conn = connection;
        this.translator = translator;
        sessionQueued = true;
        PreferenceConfiguration prefs = PreferenceConfiguration.readPreferences(activity);
        exec.execute(() -> {
            client.connect();
            snap = apply(prefs);
            Log.i(TAG, "session layer=" + LAYER + " pad=" + prefs.titanPadMode);
        });
    }

    public void refreshSession() {
        if (!sessionQueued) {
            exec.execute(() -> {
                client.connect();
                writeProfile(activity, client, PreferenceConfiguration.readPreferences(activity));
            });
            return;
        }
        exec.execute(() -> {
            if (snap != null) restore(snap);
            snap = apply(PreferenceConfiguration.readPreferences(activity));
        });
    }

    public void releaseSession() {
        exec.execute(() -> {
            if (snap != null) {
                restore(snap);
                snap = null;
            }
            client.popTempKeyMap(LAYER);
            client.disconnect();
            sessionQueued = false;
            LimeLog.info("Titan deck session released");
        });
        conn = null;
    }

    public void destroy() {
        releaseSession();
        try {
            activity.unregisterReceiver(remoteInput);
        } catch (Exception ignored) {}
    }

    @Override public boolean isCtrlOn() { return ctrl; }
    @Override public boolean isAltOn() { return alt; }
    @Override public boolean isShiftOn() { return shift; }
    @Override public boolean isMetaOn() { return meta; }
    @Override public boolean isCapsOn() { return caps; }

    @Override
    public void onNamedKey(String key) {
        if (TitanHostKeys.isModifierName(key)) {
            if ("CTRL".equals(key)) ctrl = !ctrl;
            else if ("ALT".equals(key)) alt = !alt;
            else if ("SHIFT".equals(key)) shift = !shift;
            else if ("META".equals(key)) meta = !meta;
            else if ("CAPS".equals(key)) caps = !caps;
            bars.refreshModifiers();
            return;
        }
        int code = TitanHostKeys.namedKeyCode(key);
        if (code == 0 || conn == null || translator == null) return;
        sendKey(code, stickyModifiers());
        clearOneShot();
        streamView.requestFocus();
    }

    @Override
    public void onGlyph(String glyph) {
        if (glyph == null || glyph.isEmpty() || conn == null || translator == null) return;
        char c = glyph.charAt(0);
        if (caps && c >= 'a' && c <= 'z') c = (char) (c - 32);
        TitanHostKeys.Chord chord = TitanHostKeys.glyph(c);
        if (chord == null) return;
        if (chord.keyCode != KeyEvent.KEYCODE_UNKNOWN && chord.keyCode != 0) {
            /* The glyph already carries its Shift. A latched Shift must not
             * turn 8 into * or ! into 1. Ctrl, Alt, and Meta still apply. */
            byte sticky = (byte) (stickyModifiers() & ~KeyboardPacket.MODIFIER_SHIFT);
            sendKey(chord.keyCode, (byte) (chord.modifier | sticky));
        } else if (chord.utf8 != null) {
            conn.sendUtf8Text(chord.utf8);
        }
        clearOneShot();
        streamView.requestFocus();
        exec.execute(client::bumpKeyActivity);
    }

    private void sendKey(int androidKey, byte modifier) {
        short vk = translator.translate(androidKey, -1);
        if (vk == 0) return;
        conn.sendKeyboardInput(vk, KeyboardPacket.KEY_DOWN, modifier, (byte) 0);
        conn.sendKeyboardInput(vk, KeyboardPacket.KEY_UP, modifier, (byte) 0);
        exec.execute(client::bumpKeyActivity);
    }

    private byte stickyModifiers() {
        byte m = 0;
        if (ctrl) m |= KeyboardPacket.MODIFIER_CTRL;
        if (alt) m |= KeyboardPacket.MODIFIER_ALT;
        if (shift) m |= KeyboardPacket.MODIFIER_SHIFT;
        if (meta) m |= KeyboardPacket.MODIFIER_META;
        return m;
    }

    private void clearOneShot() {
        ctrl = alt = shift = meta = false;
        bars.refreshModifiers();
    }

    private void handleRemote(Intent intent) {
        String action = intent.getStringExtra(Titan2ApiContract.EXTRA_REMOTE_ACTION);
        String kind = intent.getStringExtra(Titan2ApiContract.EXTRA_KIND);
        int wheel = intent.getIntExtra(Titan2ApiContract.EXTRA_MOUSE_WHEEL, 0);
        if (wheel == 0) wheel = wheelNotches(action);
        if (wheel != 0) {
            conn.sendMouseScroll((byte) Math.max(-128, Math.min(127, wheel)));
            return;
        }
        if (Titan2ApiContract.KIND_KEY.equals(kind)
                || (action != null && action.startsWith("host:"))) {
            if (translator == null) return;
            int hid = intent.getIntExtra(Titan2ApiContract.EXTRA_HID_USAGE, 0);
            int code = TitanHostKeys.hidUsageToKeyCode(hid);
            if (code == 0) code = intent.getIntExtra(Titan2ApiContract.EXTRA_KEYCODE, 0);
            int mods = intent.getIntExtra(Titan2ApiContract.EXTRA_MODIFIERS, 0);
            byte mod = modifierBits(mods);
            if (code == 0 && action != null && action.startsWith("host:")) {
                TitanHostKeys.Chord chord = TitanHostKeys.hostSpec(action.substring(5));
                if (chord == null) return;
                code = chord.keyCode;
                mod = chord.modifier;
            }
            if (code == 0) return;
            sendKey(code, mod);
            return;
        }
        if (kind != null && !Titan2ApiContract.KIND_MOUSE.equals(kind)) return;
        int buttons = intent.getIntExtra(Titan2ApiContract.EXTRA_MOUSE_BUTTONS, 0);
        boolean tap = intent.getBooleanExtra(Titan2ApiContract.EXTRA_MOUSE_TAP, buttons != 0);
        if (buttons == 0 && !tap) {
            if (heldMouse != 0) conn.sendMouseButtonUp(heldMouse);
            heldMouse = 0;
            return;
        }
        byte button = mouseButton(buttons == 0 ? 1 : buttons);
        if (tap) {
            conn.sendMouseButtonDown(button);
            conn.sendMouseButtonUp(button);
            return;
        }
        heldMouse = button;
        conn.sendMouseButtonDown(button);
    }

    private static int wheelNotches(String action) {
        if (action == null) return 0;
        if ("mouse:scroll_up".equals(action) || "mouse:wheel_up".equals(action)) return 1;
        if ("mouse:scroll_down".equals(action) || "mouse:wheel_down".equals(action)) return -1;
        if (action.startsWith("mouse:wheel:")) {
            try {
                return Integer.parseInt(action.substring("mouse:wheel:".length()).trim());
            } catch (NumberFormatException e) {
                return 0;
            }
        }
        return 0;
    }

    private static byte modifierBits(int mods) {
        byte mod = 0;
        if ((mods & 1) != 0) mod |= KeyboardPacket.MODIFIER_CTRL;
        if ((mods & 2) != 0) mod |= KeyboardPacket.MODIFIER_SHIFT;
        if ((mods & 4) != 0) mod |= KeyboardPacket.MODIFIER_ALT;
        if ((mods & 8) != 0) mod |= KeyboardPacket.MODIFIER_META;
        return mod;
    }

    private static byte mouseButton(int buttons) {
        if ((buttons & 2) != 0) return MouseButtonPacket.BUTTON_RIGHT;
        if ((buttons & 4) != 0) return MouseButtonPacket.BUTTON_MIDDLE;
        return MouseButtonPacket.BUTTON_LEFT;
    }

    private int rowHeightPx(PreferenceConfiguration prefs) {
        String h = prefs.titanDeckHeight == null ? "44" : prefs.titanDeckHeight;
        try {
            int dp = Integer.parseInt(h);
            if (dp < 32) dp = 44;
            if (dp > 64) dp = 52;
            return dp(dp);
        } catch (NumberFormatException e) {
            return dp(44);
        }
    }

    private int dp(int v) {
        return Math.round(v * activity.getResources().getDisplayMetrics().density);
    }

    private Snap apply(PreferenceConfiguration prefs) {
        Snap saved = new Snap();
        writeProfile(activity, client, prefs);
        Map<String, String> layer = TitanShortcuts.read(activity);
        if (!layer.isEmpty()) {
            boolean pushed = client.pushTempKeyMap(LAYER, layer);
            saved.pushedLayer = pushed;
            Log.i(TAG, "push " + LAYER + " ok=" + pushed + " slots=" + layer.size());
        }
        if (!"leave".equals(prefs.titanPadMode)) {
            saved.padMode = client.getPadMode();
            client.setPadMode(prefs.titanPadMode);
            saved.wrotePad = true;
        }
        saved.wroteDbl = putPlane(Titan2ApiContract.FILE_PAD_DBLTAP, prefs.titanDbltap, saved);
        saved.wroteTap = putFlag(Titan2ApiContract.FILE_PAD_TAP_CLICK, prefs.titanTapClick, saved);
        saved.wroteLong = putFlag(Titan2ApiContract.FILE_PAD_LONG_CLICK, prefs.titanLongClick, saved);
        saved.wroteScroll = putFlag(Titan2ApiContract.FILE_PAD_SCROLL, prefs.titanScroll, saved);
        if (prefs.titanTypingLockMs != null && !"leave".equals(prefs.titanTypingLockMs)) {
            saved.pauseMs = ControlPlane.get(activity, Titan2ApiContract.FILE_PAD_CURSOR_PAUSE_MS, "400");
            ControlPlane.put(activity, Titan2ApiContract.FILE_PAD_CURSOR_PAUSE_MS, prefs.titanTypingLockMs);
            saved.wrotePause = true;
        }
        if (prefs.titanKeyRepeat != null && !"system".equals(prefs.titanKeyRepeat)) {
            saved.repeatEn = KeyInputTiming.isKeyRepeatEnabled(activity);
            saved.repeatTimeout = readRepeatTimeout();
            saved.repeatDelay = readRepeatDelay();
            boolean en = !"off".equals(prefs.titanKeyRepeat);
            int timeout = 400;
            int delay = 50;
            if ("fast".equals(prefs.titanKeyRepeat)) {
                timeout = 200;
                delay = 30;
            } else if ("slow".equals(prefs.titanKeyRepeat)) {
                timeout = 800;
                delay = 90;
            }
            KeyInputTiming.publish(activity, en, timeout, delay);
            saved.wroteRepeat = true;
        }
        return saved;
    }

    private void restore(Snap saved) {
        if (saved.pushedLayer) client.popTempKeyMap(LAYER);
        if (saved.wrotePad && saved.padMode != null) client.setPadMode(saved.padMode);
        if (saved.wroteDbl) ControlPlane.put(activity, Titan2ApiContract.FILE_PAD_DBLTAP, saved.dbltap);
        if (saved.wroteTap) ControlPlane.put(activity, Titan2ApiContract.FILE_PAD_TAP_CLICK, saved.tap);
        if (saved.wroteLong) ControlPlane.put(activity, Titan2ApiContract.FILE_PAD_LONG_CLICK, saved.longClick);
        if (saved.wroteScroll) ControlPlane.put(activity, Titan2ApiContract.FILE_PAD_SCROLL, saved.scroll);
        if (saved.wrotePause) {
            ControlPlane.put(activity, Titan2ApiContract.FILE_PAD_CURSOR_PAUSE_MS, saved.pauseMs);
        }
        if (saved.wroteRepeat) {
            KeyInputTiming.publish(activity, saved.repeatEn, saved.repeatTimeout, saved.repeatDelay);
        }
    }

    private boolean putPlane(String file, String value, Snap saved) {
        if (value == null || "leave".equals(value)) return false;
        if (file.equals(Titan2ApiContract.FILE_PAD_DBLTAP)) {
            saved.dbltap = ControlPlane.get(activity, file, Titan2ApiContract.PAD_DBLTAP_CLASSIC);
        }
        ControlPlane.put(activity, file, value);
        return true;
    }

    private boolean putFlag(String file, String value, Snap saved) {
        if (value == null || "leave".equals(value)) return false;
        String prev = ControlPlane.get(activity, file, "1");
        if (file.equals(Titan2ApiContract.FILE_PAD_TAP_CLICK)) saved.tap = prev;
        else if (file.equals(Titan2ApiContract.FILE_PAD_LONG_CLICK)) saved.longClick = prev;
        else if (file.equals(Titan2ApiContract.FILE_PAD_SCROLL)) saved.scroll = prev;
        ControlPlane.put(activity, file, "on".equals(value) ? "1" : "0");
        return true;
    }

    private int readRepeatTimeout() {
        int t = KeyInputTiming.keyRepeatTimeoutMs(activity);
        if (t > 10000) return KeyInputTiming.FALLBACK_REPEAT_TIMEOUT_MS;
        return t;
    }

    private int readRepeatDelay() {
        return KeyInputTiming.keyRepeatDelayMs(activity);
    }

    private static void writeProfile(Context context, Titan2Client api, PreferenceConfiguration prefs) {
        String pkg = context.getPackageName();
        api.ensureKeymapProfile(pkg, "Moonlight");
        Map<String, String> profile = TitanShortcuts.readForProfile(context);
        for (Map.Entry<String, String> e : profile.entrySet()) {
            String action = e.getValue();
            if (action == null || action.isEmpty() || "default".equals(action)) {
                api.setKeyAction(e.getKey(), "", pkg);
            } else {
                api.setKeyAction(e.getKey(), action, pkg);
            }
        }
    }

    private static final class Snap {
        boolean pushedLayer;
        boolean wrotePad;
        String padMode;
        boolean wroteDbl;
        String dbltap;
        boolean wroteTap;
        String tap;
        boolean wroteLong;
        String longClick;
        boolean wroteScroll;
        String scroll;
        boolean wrotePause;
        String pauseMs;
        boolean wroteRepeat;
        boolean repeatEn;
        int repeatTimeout = 400;
        int repeatDelay = 50;
    }
}
