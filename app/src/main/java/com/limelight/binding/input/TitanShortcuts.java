package com.limelight.binding.input;

import android.content.Context;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;

import com.titanus2.api.Titan2ApiContract;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Moonlight's view of the Titan Controls shortcut map. Slot ids are the
 * same ones Controls remaps (side, Back, Recents, Fn, Sym, Alt). An action
 * is {@code mouse:*}, {@code host:*} (including chords such as
 * {@code host:ctrl+shift+esc}), {@code none}, or {@code default}.
 */
public final class TitanShortcuts {
    public static final String EXTRA_PREF = "titan_shortcut_extra";
    private static final String MIGRATED = "titan_side_scroll_v10";

    /** id, preference key, product default. */
    private static final String[][] SLOTS = {
            {Titan2ApiContract.SLOT_SIDE2_SHORT, "list_titan_side2_short", "mouse:scroll_up"},
            {Titan2ApiContract.SLOT_SIDE2_LONG, "list_titan_side2_long", "mouse:scroll_up"},
            {Titan2ApiContract.SLOT_SIDE2_DOUBLE, "list_titan_side2_double", "default"},
            {Titan2ApiContract.SLOT_SIDE_SHORT, "list_titan_side_short", "mouse:scroll_down"},
            {Titan2ApiContract.SLOT_SIDE_LONG, "list_titan_side_long", "mouse:scroll_down"},
            {Titan2ApiContract.SLOT_SIDE_DOUBLE, "list_titan_side_double", "default"},
            {"back_short", "list_titan_slot_back_short", "default"},
            {"back_long", "list_titan_slot_back_long", "default"},
            {"back_double", "list_titan_slot_back_double", "default"},
            {"recents_short", "list_titan_slot_recents_short", "default"},
            {"recents_long", "list_titan_slot_recents_long", "default"},
            {"recents_double", "list_titan_slot_recents_double", "default"},
            {"fn_short", "list_titan_slot_fn_short", "default"},
            {"fn_long", "list_titan_slot_fn_long", "default"},
            {"fn_double", "list_titan_slot_fn_double", "default"},
            {"sym_short", "list_titan_slot_sym_short", "default"},
            {"sym_long", "list_titan_slot_sym_long", "default"},
            {"sym_double", "list_titan_slot_sym_double", "default"},
            {"alt_short", "list_titan_slot_alt_short", "default"},
            {"alt_long", "list_titan_slot_alt_long", "default"},
            {"alt_double", "list_titan_slot_alt_double", "default"},
    };

    private TitanShortcuts() {}

    /**
     * One-shot: the previous deck defaults were mouse clicks and Esc.
     * A saved value that is still one of those becomes scroll. Anything
     * else the user picked is left alone.
     */
    public static void migrate(Context context) {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(context);
        SharedPreferences.Editor ed = prefs.edit();
        boolean dirty = false;
        if (!prefs.getBoolean(MIGRATED, false)) {
            dirty |= rewrite(prefs, ed, "list_titan_side_short", "mouse:left", "mouse:scroll_down");
            dirty |= rewrite(prefs, ed, "list_titan_side_long", "mouse:right", "mouse:scroll_down");
            dirty |= rewrite(prefs, ed, "list_titan_side2_short", "mouse:middle", "mouse:scroll_up");
            dirty |= rewrite(prefs, ed, "list_titan_side2_long", "host:esc", "mouse:scroll_up");
            ed.putBoolean(MIGRATED, true);
            dirty = true;
        }
        // The first deck build persisted Function keys = off. Turn that
        // stored default on once. A later Off stays off.
        if (!prefs.getBoolean("titan_deck_fn_on_v10", false)) {
            if (!prefs.getBoolean("checkbox_titan_deck_fn", true)) {
                ed.putBoolean("checkbox_titan_deck_fn", true);
            }
            ed.putBoolean("titan_deck_fn_on_v10", true);
            dirty = true;
        }
        if (dirty) ed.apply();
    }

    /** Slot id → action. {@code default} and blanks are omitted. */
    public static Map<String, String> read(Context context) {
        migrate(context);
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(context);
        Map<String, String> map = new LinkedHashMap<>();
        for (String[] slot : SLOTS) {
            put(map, slot[0], prefs.getString(slot[1], slot[2]));
        }
        applyExtra(map, prefs.getString(EXTRA_PREF, ""));
        return map;
    }

    /** Every known slot, with {@code null} where the user kept system default. */
    public static Map<String, String> readForProfile(Context context) {
        migrate(context);
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(context);
        Map<String, String> map = new LinkedHashMap<>();
        for (String[] slot : SLOTS) {
            String action = normalize(prefs.getString(slot[1], slot[2]));
            map.put(slot[0], isSet(action) ? action : null);
        }
        Map<String, String> extra = new LinkedHashMap<>();
        applyExtra(extra, prefs.getString(EXTRA_PREF, ""));
        for (Map.Entry<String, String> e : extra.entrySet()) {
            map.put(e.getKey(), e.getValue());
        }
        return map;
    }

    private static void applyExtra(Map<String, String> map, String text) {
        if (text == null || text.isEmpty()) return;
        String[] lines = text.split("\\r?\\n");
        for (String raw : lines) {
            String line = raw.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            int eq = line.indexOf('=');
            if (eq <= 0) continue;
            String slot = line.substring(0, eq).trim();
            String action = normalize(line.substring(eq + 1).trim());
            if (!slot.matches("[A-Za-z0-9_]+")) continue;
            if (!isSet(action)) {
                map.remove(slot);
            } else {
                map.put(slot, action);
            }
        }
    }

    /** Bare {@code ctrl+c} becomes {@code host:ctrl+c}. */
    static String normalize(String action) {
        if (action == null) return "";
        String t = action.trim();
        if (t.isEmpty() || "default".equals(t) || "none".equals(t)) return t;
        if (t.startsWith("mouse:") || t.startsWith("host:")
                || t.startsWith("keycode:") || t.startsWith("layout:")
                || t.startsWith("scan:") || t.startsWith("app:")) {
            return t;
        }
        return "host:" + t.toLowerCase();
    }

    private static boolean isSet(String action) {
        return action != null && !action.isEmpty() && !"default".equals(action);
    }

    private static void put(Map<String, String> map, String slot, String action) {
        String n = normalize(action);
        if (isSet(n)) map.put(slot, n);
    }

    private static boolean rewrite(SharedPreferences prefs, SharedPreferences.Editor ed,
                                   String key, String from, String to) {
        if (!prefs.contains(key)) return false;
        if (!from.equals(prefs.getString(key, ""))) return false;
        ed.putString(key, to);
        return true;
    }
}
