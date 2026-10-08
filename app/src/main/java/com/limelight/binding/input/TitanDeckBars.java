package com.limelight.binding.input;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.limelight.preferences.PreferenceConfiguration;
import com.titanus2.api.KeyGlyphs;

import java.util.ArrayList;
import java.util.List;

/**
 * Top and bottom key bands. Same keys as Atlas and USB HID, drawn as
 * DeviceDefault text tiles: no filled gray keys, theme color, accent only
 * while a modifier is held. Rows share the band height so the letterbox
 * is a grid, not an empty slab.
 */
public final class TitanDeckBars {
    public interface Host {
        void onNamedKey(String key);
        void onGlyph(String glyph);
        boolean isCtrlOn();
        boolean isAltOn();
        boolean isShiftOn();
        boolean isMetaOn();
        boolean isCapsOn();
    }

    private final Host host;
    private final LinearLayout top;
    private final LinearLayout bottom;
    private final List<TextView> mods = new ArrayList<>();
    private PreferenceConfiguration lastPrefs;
    private boolean lastTop;
    private boolean lastBottom;
    private int symbolPage;
    private int rowHeightPx;
    /** Light ink. The letterbox behind these keys is black video, not the settings page. */
    private static final int INK = 0xFFF2F2F2;
    private static final int HAIR = 0x99F2F2F2;
    private int accentColor;

    public TitanDeckBars(Context context, Host host) {
        this.host = host;
        resolveColors(context);
        top = bar(context);
        bottom = bar(context);
    }

    public LinearLayout top() { return top; }
    public LinearLayout bottom() { return bottom; }

    public void setRowHeightPx(int px) {
        rowHeightPx = px;
    }

    /** How many key rows this band will show. */
    public int rowCount(PreferenceConfiguration prefs, boolean topSide) {
        return rowsFor(prefs, topSide).size();
    }

    public void reload(PreferenceConfiguration prefs, boolean showTop, boolean showBottom) {
        lastPrefs = prefs;
        lastTop = showTop;
        lastBottom = showBottom;
        resolveColors(top.getContext());
        top.removeAllViews();
        bottom.removeAllViews();
        mods.clear();
        if (!showTop && !showBottom) {
            top.setVisibility(View.GONE);
            bottom.setVisibility(View.GONE);
            return;
        }
        List<String[]> topRows = rowsFor(prefs, true);
        List<String[]> botRows = rowsFor(prefs, false);
        fill(top, topRows, showTop && !topRows.isEmpty());
        fill(bottom, botRows, showBottom && !botRows.isEmpty());
        refreshModifiers();
    }

    public void refreshModifiers() {
        for (TextView b : mods) {
            String key = (String) b.getTag();
            boolean on = false;
            if ("CTRL".equals(key)) on = host.isCtrlOn();
            else if ("ALT".equals(key)) on = host.isAltOn();
            else if ("SHIFT".equals(key)) on = host.isShiftOn();
            else if ("META".equals(key)) on = host.isMetaOn();
            else if ("CAPS".equals(key)) on = host.isCapsOn();
            b.setTextColor(on ? accentColor : INK);
            b.setTypeface(Typeface.SANS_SERIF, on ? Typeface.BOLD : Typeface.NORMAL);
            b.setBackground(outline(b.getContext(), on ? accentColor : HAIR));
        }
    }

    private void fill(LinearLayout bar, List<String[]> rows, boolean show) {
        Context c = bar.getContext();
        if (!show || rows.isEmpty()) {
            bar.setVisibility(View.GONE);
            return;
        }
        bar.setVisibility(View.VISIBLE);
        for (String[] row : rows) {
            LinearLayout line = new LinearLayout(c);
            line.setOrientation(LinearLayout.HORIZONTAL);
            line.setGravity(Gravity.CENTER);
            line.setClickable(false);
            line.setBackgroundColor(0);
            for (String key : row) {
                TextView b = makeKey(c, key);
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                        0, ViewGroup.LayoutParams.MATCH_PARENT, 1f);
                line.addView(b, lp);
            }
            int h = rowHeightPx > 0 ? rowHeightPx : dp(c, 44);
            bar.addView(line, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, h));
        }
    }

    private List<String[]> rowsFor(PreferenceConfiguration prefs, boolean topSide) {
        List<String[]> out = new ArrayList<>();
        if (prefs == null) return out;
        boolean showTop = true;
        boolean showBottom = true;
        String mode = prefs.titanDeckPanels == null ? "auto" : prefs.titanDeckPanels;
        if ("off".equals(mode)) {
            showTop = false;
            showBottom = false;
        } else if ("top".equals(mode)) {
            showBottom = false;
        } else if ("bottom".equals(mode)) {
            showTop = false;
        }
        if (topSide && !showTop) return out;
        if (!topSide && !showBottom) return out;
        String[][] rows = KeyGlyphs.rows(
                prefs.titanDeckNav, prefs.titanDeckMods,
                prefs.titanDeckFn, prefs.titanDeckEdit);
        List<String[]> fn = new ArrayList<>();
        List<String[]> body = new ArrayList<>();
        for (String[] row : rows) {
            if (isFnRow(row)) fn.add(row);
            else body.add(row);
        }
        boolean fnTop = !"bottom".equals(prefs.titanDeckFnPlace);
        List<String[]> topRows = new ArrayList<>();
        List<String[]> botRows = new ArrayList<>();
        if (showTop && showBottom) {
            if (fnTop && !fn.isEmpty()) {
                topRows.addAll(fn);
                botRows.addAll(body);
            } else {
                int mid = Math.max(1, (body.size() + 1) / 2);
                for (int i = 0; i < body.size(); i++) {
                    if (i < mid) topRows.add(body.get(i));
                    else botRows.add(body.get(i));
                }
                if (!fnTop) botRows.addAll(fn);
                else topRows.addAll(0, fn);
            }
        } else {
            List<String[]> all = new ArrayList<>();
            if (fnTop) {
                all.addAll(fn);
                all.addAll(body);
            } else {
                all.addAll(body);
                all.addAll(fn);
            }
            if (showTop) topRows.addAll(all);
            else botRows.addAll(all);
        }
        if (prefs.titanDeckSymbols) {
            String[] symbols = symbolRow(prefs);
            if (symbols.length > 1) {
                if (showBottom) botRows.add(symbols);
                else topRows.add(symbols);
            }
        }
        if (topSide) out.addAll(topRows);
        else out.addAll(botRows);
        return out;
    }

    private static boolean isFnRow(String[] row) {
        return row != null && row.length > 0 && row[0] != null && row[0].startsWith("F");
    }

    /**
     * Glyphs the Titan Sym layer does not type, and that are not already a
     * key on the deck (grave and slash sit on the rows above).
     */
    static String[] extraSymbols() {
        final String onKeyboard = "0123456789()_-/:@*#+\"'!.?,`";
        List<String> out = new ArrayList<>();
        for (String glyph : KeyGlyphs.SYMBOLS) {
            if (glyph == null || glyph.length() != 1) continue;
            if (onKeyboard.indexOf(glyph.charAt(0)) >= 0) continue;
            out.add(glyph);
        }
        return out.toArray(new String[0]);
    }

    private String[] symbolRow(PreferenceConfiguration prefs) {
        String[] all = (prefs != null && "all".equals(prefs.titanDeckSymbolSet))
                ? KeyGlyphs.SYMBOLS : extraSymbols();
        int pageSize = 9;
        int pages = Math.max(1, (all.length + pageSize - 1) / pageSize);
        if (symbolPage >= pages) symbolPage = 0;
        int start = symbolPage * pageSize;
        List<String> row = new ArrayList<>();
        row.add("SYM");
        for (int i = 0; i < pageSize && start + i < all.length; i++) {
            row.add(all[start + i]);
        }
        return row.toArray(new String[0]);
    }

    private TextView makeKey(Context c, String key) {
        TextView b = new TextView(c);
        b.setTag(key);
        b.setText(label(key));
        b.setAllCaps(false);
        b.setGravity(Gravity.CENTER);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        b.setTypeface(Typeface.SANS_SERIF);
        b.setTextColor(INK);
        b.setClickable(true);
        b.setFocusable(false);
        b.setBackground(outline(c, HAIR));
        b.setOnClickListener(v -> {
            if ("SYM".equals(key)) {
                symbolPage++;
                if (lastPrefs != null) reload(lastPrefs, lastTop, lastBottom);
                return;
            }
            /* A one-character catalog glyph goes through onGlyph so "!" keeps
             * its Shift. namedKeyCode drops that bit and would print 1.
             * Arrows stay named keys: "↑" is not in the US catalog. */
            boolean catalog = key.length() == 1
                    && com.titanus2.api.KeyGlyphs.hidFor(key.charAt(0)) != null;
            if (!catalog && (TitanHostKeys.isModifierName(key)
                    || TitanHostKeys.namedKeyCode(key) != 0)) {
                host.onNamedKey(key);
            } else {
                host.onGlyph(key);
            }
            refreshModifiers();
        });
        if (TitanHostKeys.isModifierName(key)) mods.add(b);
        return b;
    }

    /** Short labels, same words as the HID pad bar. */
    private static String label(String key) {
        if (key == null) return "";
        switch (key) {
            case "ESC": return "Esc";
            case "GRAVE": return "`";
            case "BKSP": return "Bksp";
            case "PGUP": return "PgUp";
            case "PGDN": return "PgDn";
            case "PRT": return "Prt";
            case "PAUSE": return "Pse";
            case "SHIFT": return "Shift";
            case "CAPS": return "Caps";
            case "SYM": return "Sym";
            case "CTRL": return "Ctrl";
            case "ALT": return "Alt";
            case "META": return "Meta";
            case "TAB": return "Tab";
            case "INS": return "Ins";
            case "DEL": return "Del";
            case "HOME": return "Home";
            case "END": return "End";
            default: return key;
        }
    }

    private void resolveColors(Context c) {
        accentColor = 0xFFFF141A;
        TypedValue v = new TypedValue();
        if (!c.getTheme().resolveAttribute(android.R.attr.colorAccent, v, true)) {
            return;
        }
        int color = 0;
        if (v.type >= TypedValue.TYPE_FIRST_COLOR_INT && v.type <= TypedValue.TYPE_LAST_COLOR_INT) {
            color = v.data;
        } else if (v.resourceId != 0) {
            color = c.getResources().getColor(v.resourceId, c.getTheme());
        }
        if (color != 0 && ((color >>> 24) & 0xFF) > 0x40) {
            accentColor = color;
        }
    }

    /** Transparent face, light hairline. No gray fill. */
    private static GradientDrawable outline(Context c, int strokeColor) {
        GradientDrawable d = new GradientDrawable();
        d.setShape(GradientDrawable.RECTANGLE);
        d.setColor(0x00000000);
        int stroke = Math.max(1, Math.round(c.getResources().getDisplayMetrics().density));
        d.setStroke(stroke, strokeColor);
        return d;
    }

    private static int dp(Context c, int v) {
        return Math.round(v * c.getResources().getDisplayMetrics().density);
    }

    private static LinearLayout bar(Context c) {
        LinearLayout bar = new LinearLayout(c);
        bar.setOrientation(LinearLayout.VERTICAL);
        bar.setBackgroundColor(0);
        bar.setClickable(false);
        bar.setFocusable(false);
        bar.setVisibility(View.GONE);
        return bar;
    }
}
