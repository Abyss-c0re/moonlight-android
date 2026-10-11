#include "xr_internal.h"

#include "xr_geom.h"
#include "xr_linear.h"

#include <android/log.h>
#include <stdlib.h>
#include <string.h>

/* WayVR-style keyboard. The layout is the text from nativeSetKeyboard.
 * Trigger types. Stick click on the typing hand holds Shift for that key
 * (orange laser). Stick click on the other hand applies alt_modifier
 * (purple laser). Modifier keys latch until the next normal key, a second
 * press, or hide. Hiding releases every key. The panel starts hidden. */

#define KB_MAX_KEYS 160
#define KB_MAX_MACROS 8
#define KB_MAX_STEPS 12
#define KB_MAX_LABELS 80
#define KB_MAX_ROWS 8
#define KB_CFG 49152
#define KB_UNIT 64

#define MOD_SHIFT 0x01
#define MOD_CTRL 0x02
#define MOD_ALT 0x04
#define MOD_META 0x08

#define ROLE_KEY 0
#define ROLE_MACRO 2
#define ROLE_HIDE 3

#define KEY_DOWN 0x03
#define KEY_UP 0x04

typedef struct KbKey {
    float x, y, w;
    int x0, y0, x1, y1;
    int vk;
    int role;
    int macro;
    int mod_bit;
    int sticky;
    int held;
    char name[24];
    char label[16];
    char shift_label[16];
} KbKey;

typedef struct KbStep {
    int vk;
    int dir;
} KbStep;

typedef struct KbMacro {
    char name[24];
    KbStep steps[KB_MAX_STEPS];
    int count;
} KbMacro;

typedef struct KbLabel {
    char name[24];
    char a[16];
    char b[16];
} KbLabel;

typedef struct VkName {
    const char* name;
    int vk;
    int bit;
} VkName;

static const VkName VK_NAMES[] = {
        {"Escape", 0x1B, 0},
        {"F1", 0x70, 0},
        {"F2", 0x71, 0},
        {"F3", 0x72, 0},
        {"F4", 0x73, 0},
        {"F5", 0x74, 0},
        {"F6", 0x75, 0},
        {"F7", 0x76, 0},
        {"F8", 0x77, 0},
        {"F9", 0x78, 0},
        {"F10", 0x79, 0},
        {"F11", 0x7A, 0},
        {"F12", 0x7B, 0},
        {"Print", 0x2C, 0},
        {"Scroll", 0x91, 0},
        {"Pause", 0x13, 0},
        {"Oem3", 0xC0, 0},
        {"N1", 0x31, 0},
        {"N2", 0x32, 0},
        {"N3", 0x33, 0},
        {"N4", 0x34, 0},
        {"N5", 0x35, 0},
        {"N6", 0x36, 0},
        {"N7", 0x37, 0},
        {"N8", 0x38, 0},
        {"N9", 0x39, 0},
        {"N0", 0x30, 0},
        {"Minus", 0xBD, 0},
        {"Plus", 0xBB, 0},
        {"BackSpace", 0x08, 0},
        {"Backspace", 0x08, 0},
        {"Tab", 0x09, 0},
        {"Q", 0x51, 0},
        {"W", 0x57, 0},
        {"E", 0x45, 0},
        {"R", 0x52, 0},
        {"T", 0x54, 0},
        {"Y", 0x59, 0},
        {"U", 0x55, 0},
        {"I", 0x49, 0},
        {"O", 0x4F, 0},
        {"P", 0x50, 0},
        {"Oem4", 0xDB, 0},
        {"Oem6", 0xDD, 0},
        {"Oem5", 0xDC, 0},
        {"Delete", 0x2E, 0},
        {"End", 0x23, 0},
        {"Next", 0x22, 0},
        {"Prior", 0x21, 0},
        {"Home", 0x24, 0},
        {"Insert", 0x2D, 0},
        {"NumLock", 0x90, 0},
        {"KP_Divide", 0x6F, 0},
        {"KP_Multiply", 0x6A, 0},
        {"KP_Subtract", 0x6D, 0},
        {"KP_Add", 0x6B, 0},
        {"KP_Enter", 0x0D, 0},
        {"KP_Decimal", 0x6E, 0},
        {"KP_0", 0x60, 0},
        {"KP_1", 0x61, 0},
        {"KP_2", 0x62, 0},
        {"KP_3", 0x63, 0},
        {"KP_4", 0x64, 0},
        {"KP_5", 0x65, 0},
        {"KP_6", 0x66, 0},
        {"KP_7", 0x67, 0},
        {"KP_8", 0x68, 0},
        {"KP_9", 0x69, 0},
        {"XF86Favorites", 0xAB, 0},
        {"Caps", 0x14, 0},
        {"A", 0x41, 0},
        {"S", 0x53, 0},
        {"D", 0x44, 0},
        {"F", 0x46, 0},
        {"G", 0x47, 0},
        {"H", 0x48, 0},
        {"J", 0x4A, 0},
        {"K", 0x4B, 0},
        {"L", 0x4C, 0},
        {"Oem1", 0xBA, 0},
        {"Oem7", 0xDE, 0},
        {"Return", 0x0D, 0},
        {"Enter", 0x0D, 0},
        {"Oem102", 0xE2, 0},
        {"Z", 0x5A, 0},
        {"X", 0x58, 0},
        {"C", 0x43, 0},
        {"V", 0x56, 0},
        {"B", 0x42, 0},
        {"N", 0x4E, 0},
        {"M", 0x4D, 0},
        {"Comma", 0xBC, 0},
        {"Period", 0xBE, 0},
        {"Oem2", 0xBF, 0},
        {"Up", 0x26, 0},
        {"Down", 0x28, 0},
        {"Left", 0x25, 0},
        {"Right", 0x27, 0},
        {"Space", 0x20, 0},
        {"Menu", 0x5D, 0},
        {"LShift", 0xA0, MOD_SHIFT},
        {"RShift", 0xA1, MOD_SHIFT},
        {"Shift", 0xA0, MOD_SHIFT},
        {"LCtrl", 0xA2, MOD_CTRL},
        {"RCtrl", 0xA3, MOD_CTRL},
        {"Ctrl", 0xA2, MOD_CTRL},
        {"LAlt", 0xA4, MOD_ALT},
        {"RAlt", 0xA5, MOD_ALT},
        {"Alt", 0xA4, MOD_ALT},
        {"Meta", 0xA5, MOD_ALT},
        {"LSuper", 0x5B, MOD_META},
        {"RSuper", 0x5C, MOD_META},
        {"Super", 0x5B, MOD_META},
};

static KbKey keys[KB_MAX_KEYS];
static int key_count;
static KbMacro macros[KB_MAX_MACROS];
static int macro_count;
static KbKey draft_keys[KB_MAX_KEYS];
static int draft_count;
static KbMacro draft_macros[KB_MAX_MACROS];
static int draft_macro_count;
static KbLabel draft_labels[KB_MAX_LABELS];
static int draft_label_count;
static int draft_rows;
static float draft_units;
static int draft_alt;
static int draft_warns;

static char cfg[KB_CFG];
static int cfg_len;
static int cfg_dirty;
static char built[KB_CFG];
static int built_len;
static char pristine[KB_CFG];
static char scratch[KB_CFG];

static int face_w;
static int face_h;
static uint8_t* face;
static int paint_dirty;
static int last_hover[2];
static int last_shift;

static int sticky_mask;
static int sticky_n;
static int sticky_vk[8];
static int sticky_bit[8];
static int sticky_key[8];

static int held_index[2];
static int held_vk[2];
static int held_mods[2];
static int held_clear[2];

static void copy_str(char* dest, int cap, const char* src) {
    int i = 0;
    if (cap <= 0) {
        return;
    }
    if (src == NULL) {
        dest[0] = '\0';
        return;
    }
    while (src[i] != '\0' && i + 1 < cap) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static int lookup_vk(const char* name, int* bit) {
    int count = (int)(sizeof(VK_NAMES) / sizeof(VK_NAMES[0]));
    for (int i = 0; i < count; i++) {
        if (strcmp(VK_NAMES[i].name, name) == 0) {
            if (bit != NULL) {
                *bit = VK_NAMES[i].bit;
            }
            return VK_NAMES[i].vk;
        }
    }
    return 0;
}

static void lookup_vk_bit(int vk, int* bit);

static void send_vk(int vk, int down, int mods) {
    if (vk <= 0 || vk > 255) {
        return;
    }
    xr_jni_post_key(0x8000 | vk, down ? KEY_DOWN : KEY_UP, mods & 0x0F);
}

static void paint_mark(void) {
    paint_dirty = 1;
}

static int other_holds(int key, int hand) {
    int other = hand ^ 1;
    return held_index[other] == key;
}

static void release_stickies(void) {
    int mask = sticky_mask;
    for (int i = sticky_n - 1; i >= 0; i--) {
        int bit = sticky_bit[i];
        send_vk(sticky_vk[i], 0, mask);
        int still = 0;
        for (int j = 0; j < i; j++) {
            if (sticky_bit[j] == bit) {
                still = 1;
            }
        }
        if (!still) {
            mask &= ~bit;
        }
        if (sticky_key[i] >= 0 && sticky_key[i] < key_count) {
            keys[sticky_key[i]].sticky = 0;
        }
    }
    sticky_n = 0;
    sticky_mask = 0;
    paint_mark();
}

static void release_hand(int hand) {
    int index;
    int clear;
    if (hand < 0 || hand > 1 || held_index[hand] < 0) {
        held_index[hand] = -1;
        return;
    }
    index = held_index[hand];
    clear = held_clear[hand];
    held_index[hand] = -1;
    held_clear[hand] = 0;
    if (index >= 0 && index < key_count) {
        keys[index].held = other_holds(index, hand);
    }
    if (!other_holds(index, hand)) {
        send_vk(held_vk[hand], 0, held_mods[hand]);
    }
    if (clear) {
        release_stickies();
    }
    paint_mark();
}

static void release_inputs(void) {
    held_clear[0] = 0;
    held_clear[1] = 0;
    release_hand(0);
    release_hand(1);
    release_stickies();
}

static void hide_keyboard(const char* status) {
    XrScreen* screen = &g_xr.screens[XR_KEYBOARD_SLOT];
    release_inputs();
    screen->visible = 0;
    screen->grab_mask = 0;
    screen->primary_hand = -1;
    if (status != NULL) {
        xr_screens_set_status(status);
    }
}

static void toggle_sticky(int index) {
    KbKey* key = &keys[index];
    for (int i = 0; i < sticky_n; i++) {
        if (sticky_key[i] != index) {
            continue;
        }
        send_vk(sticky_vk[i], 0, sticky_mask);
        sticky_mask &= ~sticky_bit[i];
        for (int j = 0; j < sticky_n; j++) {
            if (j != i && sticky_bit[j] == sticky_bit[i]) {
                sticky_mask |= sticky_bit[j];
            }
        }
        key->sticky = 0;
        sticky_n--;
        sticky_vk[i] = sticky_vk[sticky_n];
        sticky_bit[i] = sticky_bit[sticky_n];
        sticky_key[i] = sticky_key[sticky_n];
        paint_mark();
        return;
    }
    if (sticky_n >= 8) {
        return;
    }
    sticky_mask |= key->mod_bit;
    sticky_vk[sticky_n] = key->vk;
    sticky_bit[sticky_n] = key->mod_bit;
    sticky_key[sticky_n] = index;
    sticky_n++;
    key->sticky = 1;
    send_vk(key->vk, 1, sticky_mask);
    paint_mark();
}

static void run_macro(int macro, int orange, int purple) {
    int mask = 0;
    if (macro < 0 || macro >= macro_count) {
        return;
    }
    if (orange) {
        mask |= MOD_SHIFT;
    }
    if (purple) {
        mask |= g_xr.keyboard_alt;
    }
    for (int i = 0; i < macros[macro].count; i++) {
        KbStep step = macros[macro].steps[i];
        int bit = 0;
        lookup_vk_bit(step.vk, &bit);
        if (step.dir == 1 || step.dir == 2) {
            if (bit != 0) {
                mask |= bit;
            }
            send_vk(step.vk, 1, mask);
            if (step.dir == 2) {
                send_vk(step.vk, 0, mask);
                if (bit != 0) {
                    mask &= ~bit;
                }
            }
        } else {
            send_vk(step.vk, 0, mask);
            if (bit != 0) {
                mask &= ~bit;
            }
        }
    }
}

static void lookup_vk_bit(int vk, int* bit) {
    int count = (int)(sizeof(VK_NAMES) / sizeof(VK_NAMES[0]));
    *bit = 0;
    for (int i = 0; i < count; i++) {
        if (VK_NAMES[i].vk == vk && VK_NAMES[i].bit != 0) {
            *bit = VK_NAMES[i].bit;
            return;
        }
    }
}

static int word_is(const char* line, const char* word) {
    size_t n = strlen(word);
    if (strncmp(line, word, n) != 0) {
        return 0;
    }
    char next = line[n];
    return next == '\0' || next == ' ' || next == '\t' || next == ':';
}

static const char* word_rest(const char* line, const char* word) {
    line += strlen(word);
    if (*line == ':') {
        line++;
    }
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    return line;
}

static const char* next_tok(const char* line, char* buf, int cap) {
    int n = 0;
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == '\0') {
        buf[0] = '\0';
        return line;
    }
    /* '#' is a comment only at the start of a line, so a shift label can be #. */
    while (*line != '\0' && *line != ' ' && *line != '\t' && n + 1 < cap) {
        buf[n++] = *line++;
    }
    buf[n] = '\0';
    return line;
}

static int parse_mod_name(const char* name) {
    if (strcmp(name, "none") == 0 || strcmp(name, "None") == 0) {
        return 0;
    }
    if (strcmp(name, "shift") == 0 || strcmp(name, "Shift") == 0) {
        return MOD_SHIFT;
    }
    if (strcmp(name, "ctrl") == 0 || strcmp(name, "Ctrl") == 0) {
        return MOD_CTRL;
    }
    if (strcmp(name, "alt") == 0 || strcmp(name, "Alt") == 0) {
        return MOD_ALT;
    }
    /* WayVR Meta is AltGr. Moonlight carries that as the Alt modifier. */
    if (strcmp(name, "meta") == 0 || strcmp(name, "Meta") == 0) {
        return MOD_ALT;
    }
    if (strcmp(name, "super") == 0 || strcmp(name, "Super") == 0) {
        return MOD_META;
    }
    return -1;
}

static void draft_warn(const char* text) {
    if (draft_warns >= 8) {
        return;
    }
    draft_warns++;
    __android_log_print(ANDROID_LOG_WARN, "moonlight-xr", "vr keyboard %s", text);
}

static int find_draft_macro(const char* name) {
    for (int i = 0; i < draft_macro_count; i++) {
        if (strcmp(draft_macros[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void add_draft_key(float x, float y, float w, const char* name) {
    KbKey* key;
    int bit = 0;
    int vk;
    if (draft_count >= KB_MAX_KEYS) {
        draft_warn("layout is full");
        return;
    }
    key = &draft_keys[draft_count++];
    memset(key, 0, sizeof(*key));
    key->x = x;
    key->y = y;
    key->w = w;
    key->macro = -1;
    copy_str(key->name, (int)sizeof(key->name), name);
    if (strcmp(name, "HIDE") == 0) {
        key->role = ROLE_HIDE;
        copy_str(key->label, (int)sizeof(key->label), "Hide");
        return;
    }
    vk = lookup_vk(name, &bit);
    if (vk > 0) {
        key->vk = vk;
        key->mod_bit = bit;
        key->role = ROLE_KEY;
        copy_str(key->label, (int)sizeof(key->label), name);
        return;
    }
    key->role = ROLE_KEY;
    copy_str(key->label, (int)sizeof(key->label), name);
}

static void parse_row(const char* line) {
    char token[48];
    float x = 0.0f;
    if (draft_rows >= KB_MAX_ROWS) {
        draft_warn("too many rows");
        return;
    }
    while (*line != '\0') {
        float width = 1.0f;
        char* colon;
        line = next_tok(line, token, (int)sizeof(token));
        if (token[0] == '\0') {
            break;
        }
        colon = strchr(token, ':');
        if (colon != NULL) {
            *colon = '\0';
            width = strtof(token, NULL);
            memmove(token, colon + 1, strlen(colon + 1) + 1);
        }
        if (!(width > 0.15f)) {
            width = 0.15f;
        }
        if (width > 24.0f) {
            width = 24.0f;
        }
        if (strcmp(token, "~") != 0 && token[0] != '\0') {
            add_draft_key(x, (float)draft_rows, width, token);
        }
        x += width;
    }
    if (x > draft_units) {
        draft_units = x;
    }
    draft_rows++;
}

static void parse_macro(const char* line) {
    char token[48];
    KbMacro* macro;
    line = next_tok(line, token, (int)sizeof(token));
    if (token[0] == '\0' || draft_macro_count >= KB_MAX_MACROS) {
        draft_warn("skipped a macro");
        return;
    }
    macro = &draft_macros[draft_macro_count++];
    memset(macro, 0, sizeof(*macro));
    copy_str(macro->name, (int)sizeof(macro->name), token);
    while (*line != '\0' && macro->count < KB_MAX_STEPS) {
        int dir = 2;
        int len;
        int vk;
        line = next_tok(line, token, (int)sizeof(token));
        if (token[0] == '\0') {
            break;
        }
        len = (int)strlen(token);
        if (len > 1 && token[len - 1] == '+') {
            dir = 1;
            token[len - 1] = '\0';
        } else if (len > 1 && token[len - 1] == '-') {
            dir = 0;
            token[len - 1] = '\0';
        }
        vk = lookup_vk(token, NULL);
        if (vk <= 0) {
            draft_warn("macro key was not recognized");
            continue;
        }
        macro->steps[macro->count].vk = vk;
        macro->steps[macro->count].dir = dir;
        macro->count++;
    }
}

static void parse_label(const char* line) {
    char name[24];
    char a[16];
    char b[16];
    KbLabel* label;
    line = next_tok(line, name, (int)sizeof(name));
    if (name[0] == '\0' || draft_label_count >= KB_MAX_LABELS) {
        return;
    }
    line = next_tok(line, a, (int)sizeof(a));
    line = next_tok(line, b, (int)sizeof(b));
    label = &draft_labels[draft_label_count++];
    memset(label, 0, sizeof(*label));
    copy_str(label->name, (int)sizeof(label->name), name);
    copy_str(label->a, (int)sizeof(label->a), a);
    copy_str(label->b, (int)sizeof(label->b), b);
}

static void parse_line(char* line) {
    char token[32];
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == '\0' || *line == '#') {
        return;
    }
    if (line[0] == '\r') {
        return;
    }
    {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\r') {
            line[len - 1] = '\0';
        }
    }
    if (word_is(line, "alt_modifier")) {
        const char* rest = word_rest(line, "alt_modifier");
        int bit;
        next_tok(rest, token, (int)sizeof(token));
        bit = parse_mod_name(token);
        if (bit < 0) {
            draft_warn("alt_modifier was not recognized");
        } else {
            draft_alt = bit;
        }
        return;
    }
    if (word_is(line, "row")) {
        parse_row(word_rest(line, "row"));
        return;
    }
    if (word_is(line, "macro")) {
        parse_macro(word_rest(line, "macro"));
        return;
    }
    if (word_is(line, "label")) {
        parse_label(word_rest(line, "label"));
        return;
    }
    if (word_is(line, "exec")) {
        draft_warn("ignored exec; it does not run on the PC");
        return;
    }
}

static void apply_draft_labels(void) {
    for (int i = 0; i < draft_count; i++) {
        for (int l = 0; l < draft_label_count; l++) {
            if (strcmp(draft_keys[i].name, draft_labels[l].name) != 0) {
                continue;
            }
            copy_str(draft_keys[i].label, (int)sizeof(draft_keys[i].label), draft_labels[l].a);
            copy_str(draft_keys[i].shift_label, (int)sizeof(draft_keys[i].shift_label), draft_labels[l].b);
        }
    }
}

static void bind_draft_macros(void) {
    int kept = 0;
    for (int i = 0; i < draft_count; i++) {
        int macro = find_draft_macro(draft_keys[i].name);
        if (macro >= 0) {
            draft_keys[i].role = ROLE_MACRO;
            draft_keys[i].macro = macro;
            draft_keys[i].vk = 0;
        }
        if (draft_keys[i].role == ROLE_KEY && draft_keys[i].vk <= 0) {
            draft_warn("ignored an unknown key");
            continue;
        }
        draft_keys[kept++] = draft_keys[i];
    }
    draft_count = kept;
}

static void assign_key_pixels(float units, int tex_w, int tex_h) {
    if (units < 1.0f) {
        units = 1.0f;
    }
    for (int i = 0; i < draft_count; i++) {
        KbKey* key = &draft_keys[i];
        key->x0 = (int)(key->x / units * (float)tex_w);
        key->x1 = (int)((key->x + key->w) / units * (float)tex_w);
        if (key->x1 <= key->x0) {
            key->x1 = key->x0 + 1;
        }
        if (key->x0 < 0) {
            key->x0 = 0;
        }
        if (key->x1 > tex_w) {
            key->x1 = tex_w;
        }
        key->y0 = (int)(key->y * (float)KB_UNIT);
        key->y1 = key->y0 + KB_UNIT;
        if (key->y0 < 0) {
            key->y0 = 0;
        }
        if (key->y1 > tex_h) {
            key->y1 = tex_h;
        }
    }
}

static int parse_layout(char* text, float* units_out, int* rows_out) {
    char* line = text;
    draft_count = 0;
    draft_macro_count = 0;
    draft_label_count = 0;
    draft_rows = 0;
    draft_units = 1.0f;
    draft_alt = 0;
    draft_warns = 0;
    if (line[0] == (char)0xEF && line[1] == (char)0xBB && line[2] == (char)0xBF) {
        line += 3;
    }
    while (line != NULL && *line != '\0') {
        char* next = strchr(line, '\n');
        if (next != NULL) {
            *next = '\0';
            next++;
        }
        parse_line(line);
        line = next;
    }
    bind_draft_macros();
    apply_draft_labels();
    if (draft_count <= 0 || draft_rows <= 0) {
        return 0;
    }
    *units_out = draft_units;
    *rows_out = draft_rows;
    return 1;
}

static void fill_px(uint8_t* pixels, int stride, int limit_w, int limit_h, int x, int y, int w, int h, uint8_t r,
                    uint8_t g, uint8_t b) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w <= 0 || h <= 0 || x >= limit_w || y >= limit_h) {
        return;
    }
    if (x + w > limit_w) {
        w = limit_w - x;
    }
    if (y + h > limit_h) {
        h = limit_h - y;
    }
    for (int row = 0; row < h; row++) {
        uint8_t* pixel = pixels + (((y + row) * stride + x) * 4);
        for (int col = 0; col < w; col++) {
            pixel[0] = r;
            pixel[1] = g;
            pixel[2] = b;
            pixel[3] = 255;
            pixel += 4;
        }
    }
}

static void paint_label(int x, int y, int w, int h, const char* text, int px, uint8_t r, uint8_t g, uint8_t b) {
    int width;
    int left;
    int top;
    if (text == NULL || text[0] == '\0' || w < 4 || h < 4) {
        return;
    }
    width = xr_ui_measure(text, px);
    left = x + (w - width) / 2;
    top = y + (h - px) / 2;
    if (left < x) {
        left = x;
    }
    xr_ui_string(face, face_w, face_w, face_h, left, top, px, text, r, g, b);
}

static void paint_keys(int hover0, int hover1, int show_shift) {
    int bytes;
    if (face == NULL || face_w < 2 || face_h < 2) {
        return;
    }
    xr_ui_prepare();
    bytes = face_w * face_h * 4;
    memset(face, 0, (size_t)bytes);
    fill_px(face, face_w, face_w, face_h, 0, 0, face_w, face_h, 14, 16, 24);
    for (int i = 0; i < key_count; i++) {
        KbKey* key = &keys[i];
        int inset = (key->x1 - key->x0) >= 16 && (key->y1 - key->y0) >= 16 ? 3 : 1;
        int x = key->x0 + inset;
        int y = key->y0 + inset;
        int w = key->x1 - key->x0 - inset * 2;
        int h = key->y1 - key->y0 - inset * 2;
        int hover = i == hover0 || i == hover1;
        const char* text = key->label;
        uint8_t r = 36, g = 42, b = 54;
        uint8_t er = 78, eg = 148, eb = 168;
        if (key->held) {
            r = 28;
            g = 96;
            b = 118;
        } else if (key->sticky) {
            r = 132;
            g = 84;
            b = 28;
            er = 210;
            eg = 150;
            eb = 60;
        } else if (hover) {
            r = 24;
            g = 72;
            b = 92;
        }
        fill_px(face, face_w, face_w, face_h, x, y, w, h, r, g, b);
        fill_px(face, face_w, face_w, face_h, x, y, w, 3, er, eg, eb);
        if (show_shift && key->shift_label[0] != '\0') {
            text = key->shift_label;
        }
        paint_label(x, y, w, h, text, h >= 52 ? 22 : 16, 232, 238, 244);
        if (!show_shift && key->shift_label[0] != '\0' && w >= 28) {
            int sw = xr_ui_measure(key->shift_label, 14);
            int sx = x + w - sw - 4;
            if (sx < x + 2) {
                sx = x + 2;
            }
            xr_ui_string(face, face_w, face_w, face_h, sx, y + 4, 14, key->shift_label, 170, 180, 150);
        }
    }
    xr_ui_upload(&g_xr.keyboard_tex, face, face_w, face_h);
}

static void ensure_face(int tex_w, int tex_h) {
    if (face != NULL && face_w == tex_w && face_h == tex_h) {
        return;
    }
    free(face);
    face = (uint8_t*)calloc((size_t)tex_w * (size_t)tex_h * 4u, 1u);
    face_w = face != NULL ? tex_w : 0;
    face_h = face != NULL ? tex_h : 0;
    if (face == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "vr keyboard face alloc failed");
    }
}

static void ensure_screen(float width, float height) {
    XrScreen* screen = &g_xr.screens[XR_KEYBOARD_SLOT];
    int was = screen->alive;
    if (!was) {
        memset(screen, 0, sizeof(*screen));
        XrPosef_CreateIdentity(&screen->pose);
        XrPosef_CreateIdentity(&screen->grab_offset);
        screen->primary_hand = -1;
        screen->visible = 0;
    }
    screen->alive = 1;
    screen->kind = XR_SCREEN_KEYBOARD;
    screen->locked = 0;
    screen->width = width;
    screen->height = height;
    screen->curve = 0.0f;
    screen->aspect = height > 1.0e-4f ? width / height : 1.0f;
}

static void commit_layout(float units, int rows) {
    int tex_w = (int)(units * (float)KB_UNIT + 0.5f);
    int tex_h = rows * KB_UNIT;
    float unit_m = 0.036f;
    if (tex_w < KB_UNIT) {
        tex_w = KB_UNIT;
    }
    if (tex_w > 4096) {
        tex_w = 4096;
    }
    if (tex_h < KB_UNIT) {
        tex_h = KB_UNIT;
    }
    if (tex_h > 2048) {
        tex_h = 2048;
    }
    assign_key_pixels(units, tex_w, tex_h);
    release_inputs();
    memcpy(keys, draft_keys, sizeof(draft_keys[0]) * (size_t)draft_count);
    key_count = draft_count;
    memcpy(macros, draft_macros, sizeof(draft_macros[0]) * (size_t)draft_macro_count);
    macro_count = draft_macro_count;
    g_xr.keyboard_alt = draft_alt;
    ensure_screen(units * unit_m, (float)rows * unit_m);
    ensure_face(tex_w, tex_h);
    last_hover[0] = -1;
    last_hover[1] = -1;
    last_shift = 0;
    paint_dirty = 0;
    paint_keys(-1, -1, 0);
    __android_log_print(ANDROID_LOG_INFO, "moonlight-xr", "vr keyboard %d keys, %d rows, face %dx%d, hidden until Show keyboard",
                        key_count, rows, tex_w, tex_h);
}

static void clear_gl(void) {
    free(face);
    face = NULL;
    face_w = 0;
    face_h = 0;
    if (g_xr.keyboard_tex != 0) {
        glDeleteTextures(1, &g_xr.keyboard_tex);
        g_xr.keyboard_tex = 0;
    }
}

void xr_keyboard_init(void) {
    key_count = 0;
    macro_count = 0;
    built_len = 0;
    built[0] = '\0';
    cfg_len = 0;
    cfg[0] = '\0';
    cfg_dirty = 0;
    sticky_n = 0;
    sticky_mask = 0;
    held_index[0] = -1;
    held_index[1] = -1;
    held_clear[0] = 0;
    held_clear[1] = 0;
    paint_dirty = 1;
    last_hover[0] = -2;
    last_hover[1] = -2;
    last_shift = 0;
    g_xr.keyboard_alt = 0;
    g_xr.hand_key[0] = -1;
    g_xr.hand_key[1] = -1;
    clear_gl();
}

void xr_keyboard_destroy(void) {
    release_inputs();
    key_count = 0;
    clear_gl();
}

void xr_keyboard_submit(const char* text, int length) {
    if (text == NULL || length <= 0 || !g_xr.lib_mu_ready) {
        return;
    }
    if (length >= KB_CFG) {
        length = KB_CFG - 1;
    }
    pthread_mutex_lock(&g_xr.lib_mu);
    memcpy(cfg, text, (size_t)length);
    cfg[length] = '\0';
    cfg_len = length;
    cfg_dirty = 1;
    pthread_mutex_unlock(&g_xr.lib_mu);
}

void xr_keyboard_build_pending(void) {
    int len = 0;
    int dirty = 0;
    float units = 1.0f;
    int rows = 0;
    if (!g_xr.lib_mu_ready) {
        return;
    }
    pthread_mutex_lock(&g_xr.lib_mu);
    dirty = cfg_dirty;
    if (dirty) {
        len = cfg_len;
        if (len < 0) {
            len = 0;
        }
        if (len >= KB_CFG) {
            len = KB_CFG - 1;
        }
        memcpy(scratch, cfg, (size_t)len + 1);
        cfg_dirty = 0;
    }
    pthread_mutex_unlock(&g_xr.lib_mu);
    if (!dirty) {
        return;
    }
    if (len == built_len && memcmp(scratch, built, (size_t)len) == 0) {
        return;
    }
    memcpy(pristine, scratch, (size_t)len + 1);
    if (!parse_layout(scratch, &units, &rows)) {
        __android_log_print(ANDROID_LOG_WARN, "moonlight-xr", "vr keyboard file has no keys");
        xr_screens_set_status("Keyboard file has no keys");
        return;
    }
    memcpy(built, pristine, (size_t)len + 1);
    built_len = len;
    commit_layout(units, rows);
}

void xr_keyboard_toggle(void) {
    XrScreen* screen;
    xr_jni_reload_keyboard();
    xr_keyboard_build_pending();
    screen = &g_xr.screens[XR_KEYBOARD_SLOT];
    if (!screen->alive || key_count <= 0) {
        xr_screens_set_status("Keyboard failed to load");
        return;
    }
    if (screen->visible) {
        hide_keyboard("Keyboard hidden");
        return;
    }
    if (!g_xr.head_valid) {
        xr_screens_set_status("Look forward, then show the keyboard");
        return;
    }
    xr_geom_face(&screen->pose, &g_xr.head_pose, 0.70f, 0.0f);
    screen->pose.position.y -= 0.36f;
    screen->visible = 1;
    screen->grab_mask = 0;
    screen->primary_hand = -1;
    xr_screens_set_status("Keyboard on. Stick click holds Shift.");
    paint_dirty = 1;
}

int xr_keyboard_pick(float u, float v) {
    int px;
    int py;
    if (face_w < 2 || face_h < 2 || key_count <= 0) {
        return -1;
    }
    if (u < 0.0f) {
        u = 0.0f;
    }
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (u > 1.0f) {
        u = 1.0f;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }
    px = (int)(u * (float)face_w);
    py = (int)((1.0f - v) * (float)face_h);
    if (px >= face_w) {
        px = face_w - 1;
    }
    if (py >= face_h) {
        py = face_h - 1;
    }
    for (int i = 0; i < key_count; i++) {
        if (px >= keys[i].x0 && py >= keys[i].y0 && px < keys[i].x1 && py < keys[i].y1) {
            return i;
        }
    }
    return -1;
}

void xr_keyboard_press(int hand, int key, int orange, int purple) {
    KbKey* item;
    int mods;
    if (hand < 0 || hand > 1 || key < 0 || key >= key_count) {
        return;
    }
    if (!g_xr.screens[XR_KEYBOARD_SLOT].visible) {
        return;
    }
    item = &keys[key];
    if (item->role == ROLE_HIDE) {
        hide_keyboard("Keyboard hidden");
        return;
    }
    if (item->role == ROLE_MACRO) {
        run_macro(item->macro, orange, purple);
        return;
    }
    if (item->mod_bit != 0) {
        toggle_sticky(key);
        return;
    }
    mods = sticky_mask;
    if (orange) {
        mods |= MOD_SHIFT;
    }
    if (purple) {
        mods |= g_xr.keyboard_alt;
    }
    if (held_index[hand] >= 0) {
        release_hand(hand);
    }
    held_index[hand] = key;
    held_vk[hand] = item->vk;
    held_mods[hand] = mods;
    held_clear[hand] = sticky_n > 0;
    item->held = 1;
    if (!other_holds(key, hand)) {
        send_vk(item->vk, 1, mods);
    }
    paint_mark();
}

void xr_keyboard_sustain(int hand) {
    int index;
    int still;
    if (hand < 0 || hand > 1 || held_index[hand] < 0) {
        return;
    }
    index = held_index[hand];
    still = g_xr.hand_active[hand] && g_xr.trigger_value[hand] >= 0.35f && !g_xr.squeeze_held[hand] &&
            g_xr.hand_hit[hand] && g_xr.hand_hover[hand] == XR_KEYBOARD_SLOT &&
            g_xr.screens[XR_KEYBOARD_SLOT].grab_mask == 0 && g_xr.hand_key[hand] == index &&
            g_xr.screens[XR_KEYBOARD_SLOT].visible;
    if (!still) {
        release_hand(hand);
    }
}

void xr_keyboard_paint_if_needed(void) {
    int hover0 = -1;
    int hover1 = -1;
    int show_shift = (sticky_mask & MOD_SHIFT) != 0;
    XrScreen* screen = &g_xr.screens[XR_KEYBOARD_SLOT];
    if (key_count <= 0) {
        return;
    }
    if (screen->alive && screen->visible) {
        for (int hand = 0; hand < 2; hand++) {
            if (!g_xr.hand_hit[hand] || g_xr.hand_hover[hand] != XR_KEYBOARD_SLOT) {
                continue;
            }
            if (hand == 0) {
                hover0 = g_xr.hand_key[hand];
            } else {
                hover1 = g_xr.hand_key[hand];
            }
            if (g_xr.stick_click[hand] ||
                (g_xr.stick_click[hand ^ 1] && (g_xr.keyboard_alt & MOD_SHIFT) != 0)) {
                show_shift = 1;
            }
        }
    }
    if (!paint_dirty && hover0 == last_hover[0] && hover1 == last_hover[1] && show_shift == last_shift) {
        return;
    }
    last_hover[0] = hover0;
    last_hover[1] = hover1;
    last_shift = show_shift;
    paint_dirty = 0;
    paint_keys(hover0, hover1, show_shift);
}
