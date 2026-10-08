package com.limelight.binding.input;

import android.view.KeyEvent;

import com.limelight.nvstream.input.KeyboardPacket;
import com.titanus2.api.KeyGlyphs;

/**
 * Named deck keys and printed glyphs, using the same US catalog as Atlas
 * and Titan USB HID ({@link KeyGlyphs}). Glyphs become host key chords
 * (HID usage → Android key → Moonlight VK), not a second symbol font.
 */
public final class TitanHostKeys {
    private TitanHostKeys() {}

    public static final class Chord {
        /** Android keycode, or {@link KeyEvent#KEYCODE_UNKNOWN} when unset. */
        public final int keyCode;
        /** Extra {@link KeyboardPacket} modifier bits (Shift for shifted glyphs). */
        public final byte modifier;
        /** Fallback when the glyph has no US key. */
        public final String utf8;

        Chord(int keyCode, byte modifier, String utf8) {
            this.keyCode = keyCode;
            this.modifier = modifier;
            this.utf8 = utf8;
        }
    }

    public static boolean isModifierName(String key) {
        return "CTRL".equals(key) || "ALT".equals(key) || "SHIFT".equals(key)
                || "META".equals(key) || "CAPS".equals(key);
    }

    public static boolean isSymName(String key) {
        return "SYM".equals(key);
    }

    /** Named bar key → Android keycode. 0 if it is a modifier, SYM, or unknown. */
    public static int namedKeyCode(String key) {
        if (key == null) return 0;
        switch (key) {
            case "ESC": return KeyEvent.KEYCODE_ESCAPE;
            case "GRAVE": return KeyEvent.KEYCODE_GRAVE;
            case "BKSP": return KeyEvent.KEYCODE_DEL;
            case "TAB": return KeyEvent.KEYCODE_TAB;
            case "ENTER": return KeyEvent.KEYCODE_ENTER;
            case "HOME": return KeyEvent.KEYCODE_MOVE_HOME;
            case "END": return KeyEvent.KEYCODE_MOVE_END;
            case "PGUP": return KeyEvent.KEYCODE_PAGE_UP;
            case "PGDN": return KeyEvent.KEYCODE_PAGE_DOWN;
            case "↑": return KeyEvent.KEYCODE_DPAD_UP;
            case "↓": return KeyEvent.KEYCODE_DPAD_DOWN;
            case "←": return KeyEvent.KEYCODE_DPAD_LEFT;
            case "→": return KeyEvent.KEYCODE_DPAD_RIGHT;
            case "INS": return KeyEvent.KEYCODE_INSERT;
            case "DEL": return KeyEvent.KEYCODE_FORWARD_DEL;
            case "PRT": return KeyEvent.KEYCODE_SYSRQ;
            case "PAUSE": return KeyEvent.KEYCODE_BREAK;
            case "/": return KeyEvent.KEYCODE_SLASH;
            case "F1": return KeyEvent.KEYCODE_F1;
            case "F2": return KeyEvent.KEYCODE_F2;
            case "F3": return KeyEvent.KEYCODE_F3;
            case "F4": return KeyEvent.KEYCODE_F4;
            case "F5": return KeyEvent.KEYCODE_F5;
            case "F6": return KeyEvent.KEYCODE_F6;
            case "F7": return KeyEvent.KEYCODE_F7;
            case "F8": return KeyEvent.KEYCODE_F8;
            case "F9": return KeyEvent.KEYCODE_F9;
            case "F10": return KeyEvent.KEYCODE_F10;
            case "F11": return KeyEvent.KEYCODE_F11;
            case "F12": return KeyEvent.KEYCODE_F12;
            default:
                if (key.length() == 1) {
                    Chord g = glyph(key.charAt(0));
                    return g == null ? 0 : g.keyCode;
                }
                return 0;
        }
    }

    /**
     * {@code ctrl+shift+t} / {@code esc} / {@code f11} from a Controls
     * {@code host:} action, without the prefix.
     */
    public static Chord hostSpec(String spec) {
        if (spec == null || spec.isEmpty()) return null;
        byte mod = 0;
        int key = 0;
        for (String raw : spec.toLowerCase().split("\\+")) {
            String p = raw.trim();
            if (p.isEmpty()) continue;
            if ("ctrl".equals(p) || "control".equals(p)) {
                mod |= KeyboardPacket.MODIFIER_CTRL;
            } else if ("shift".equals(p)) {
                mod |= KeyboardPacket.MODIFIER_SHIFT;
            } else if ("alt".equals(p)) {
                mod |= KeyboardPacket.MODIFIER_ALT;
            } else if ("meta".equals(p) || "win".equals(p) || "cmd".equals(p)) {
                mod |= KeyboardPacket.MODIFIER_META;
            } else {
                int code = hostKey(p);
                if (code != 0) key = code;
            }
        }
        if (key == 0) return null;
        return new Chord(key, mod, null);
    }

    private static int hostKey(String p) {
        switch (p) {
            case "esc":
            case "escape": return KeyEvent.KEYCODE_ESCAPE;
            case "tab": return KeyEvent.KEYCODE_TAB;
            case "enter":
            case "return": return KeyEvent.KEYCODE_ENTER;
            case "space": return KeyEvent.KEYCODE_SPACE;
            case "backspace":
            case "bksp":
            case "bs": return KeyEvent.KEYCODE_DEL;
            case "delete":
            case "del": return KeyEvent.KEYCODE_FORWARD_DEL;
            case "up": return KeyEvent.KEYCODE_DPAD_UP;
            case "down": return KeyEvent.KEYCODE_DPAD_DOWN;
            case "left": return KeyEvent.KEYCODE_DPAD_LEFT;
            case "right": return KeyEvent.KEYCODE_DPAD_RIGHT;
            case "home": return KeyEvent.KEYCODE_MOVE_HOME;
            case "end": return KeyEvent.KEYCODE_MOVE_END;
            case "pageup":
            case "pgup": return KeyEvent.KEYCODE_PAGE_UP;
            case "pagedown":
            case "pgdn": return KeyEvent.KEYCODE_PAGE_DOWN;
            case "super": return KeyEvent.KEYCODE_META_LEFT;
            default:
                break;
        }
        if (p.startsWith("f") && p.length() >= 2) {
            try {
                int n = Integer.parseInt(p.substring(1));
                if (n >= 1 && n <= 12) return KeyEvent.KEYCODE_F1 + (n - 1);
            } catch (NumberFormatException ignored) {}
        }
        if (p.length() == 1) {
            char c = p.charAt(0);
            if (c >= 'a' && c <= 'z') return KeyEvent.KEYCODE_A + (c - 'a');
            if (c == '0') return KeyEvent.KEYCODE_0;
            if (c >= '1' && c <= '9') return KeyEvent.KEYCODE_1 + (c - '1');
        }
        return 0;
    }

    /**
     * Printed character → US key chord, same shift bit as {@link KeyGlyphs#hidFor}.
     * Null when the character is not on the shared catalog.
     */
    public static Chord glyph(char c) {
        int[] hid = KeyGlyphs.hidFor(c);
        if (hid == null) {
            if (Character.isISOControl(c)) return null;
            return new Chord(KeyEvent.KEYCODE_UNKNOWN, (byte) 0, String.valueOf(c));
        }
        int key = hidUsageToKeyCode(hid[1]);
        byte mod = (hid[0] & 0x02) != 0 ? KeyboardPacket.MODIFIER_SHIFT : 0;
        if (key == 0) {
            return new Chord(KeyEvent.KEYCODE_UNKNOWN, mod, String.valueOf(c));
        }
        return new Chord(key, mod, null);
    }

    /** Boot-keyboard HID usage → Android keycode. 0 if unknown. */
    static int hidUsageToKeyCode(int usage) {
        if (usage >= 0x04 && usage <= 0x1d) {
            return KeyEvent.KEYCODE_A + (usage - 0x04);
        }
        if (usage >= 0x1e && usage <= 0x26) {
            return KeyEvent.KEYCODE_1 + (usage - 0x1e);
        }
        if (usage >= 0x3a && usage <= 0x45) {
            return KeyEvent.KEYCODE_F1 + (usage - 0x3a);
        }
        switch (usage) {
            case 0x27: return KeyEvent.KEYCODE_0;
            case 0x28: return KeyEvent.KEYCODE_ENTER;
            case 0x29: return KeyEvent.KEYCODE_ESCAPE;
            case 0x2a: return KeyEvent.KEYCODE_DEL;
            case 0x2b: return KeyEvent.KEYCODE_TAB;
            case 0x2c: return KeyEvent.KEYCODE_SPACE;
            case 0x2d: return KeyEvent.KEYCODE_MINUS;
            case 0x2e: return KeyEvent.KEYCODE_EQUALS;
            case 0x2f: return KeyEvent.KEYCODE_LEFT_BRACKET;
            case 0x30: return KeyEvent.KEYCODE_RIGHT_BRACKET;
            case 0x31: return KeyEvent.KEYCODE_BACKSLASH;
            case 0x33: return KeyEvent.KEYCODE_SEMICOLON;
            case 0x34: return KeyEvent.KEYCODE_APOSTROPHE;
            case 0x35: return KeyEvent.KEYCODE_GRAVE;
            case 0x36: return KeyEvent.KEYCODE_COMMA;
            case 0x37: return KeyEvent.KEYCODE_PERIOD;
            case 0x38: return KeyEvent.KEYCODE_SLASH;
            case 0x49: return KeyEvent.KEYCODE_INSERT;
            case 0x4a: return KeyEvent.KEYCODE_MOVE_HOME;
            case 0x4b: return KeyEvent.KEYCODE_PAGE_UP;
            case 0x4c: return KeyEvent.KEYCODE_FORWARD_DEL;
            case 0x4d: return KeyEvent.KEYCODE_MOVE_END;
            case 0x4e: return KeyEvent.KEYCODE_PAGE_DOWN;
            case 0x4f: return KeyEvent.KEYCODE_DPAD_RIGHT;
            case 0x50: return KeyEvent.KEYCODE_DPAD_LEFT;
            case 0x51: return KeyEvent.KEYCODE_DPAD_DOWN;
            case 0x52: return KeyEvent.KEYCODE_DPAD_UP;
            default: return 0;
        }
    }
}
