#include "xr_internal.h"

#include "xr_geom.h"
#include "xr_linear.h"
#include "font8x8_basic.h"

#include <android/log.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Bitmap row 0 is the top of the panel. The texture shader flips v, and a hit
 * uses py = (1 - v) * height so the title at row 0 stays at the top. */

static const char* k_hint = "Pick a PC here. Grip moves. A/X adds a monitor.";

static void set_status(const char* text) {
    pthread_mutex_lock(&g_xr.lib_mu);
    g_xr.status[0] = '\0';
    if (text != NULL) {
        strncpy(g_xr.status, text, sizeof(g_xr.status) - 1);
        g_xr.status[sizeof(g_xr.status) - 1] = '\0';
    }
    g_xr.menu_dirty = 1;
    pthread_mutex_unlock(&g_xr.lib_mu);
}

static void make_screen(XrScreen* screen, int kind, int locked, float width, float height, float curve) {
    memset(screen, 0, sizeof(*screen));
    screen->alive = 1;
    screen->visible = 1;
    screen->kind = kind;
    screen->locked = locked;
    screen->width = width;
    screen->height = height;
    screen->curve = curve;
    screen->aspect = height > 1.0e-4f ? width / height : 1.0f;
    screen->primary_hand = -1;
    XrPosef_CreateIdentity(&screen->pose);
    XrPosef_CreateIdentity(&screen->grab_offset);
}

static void place_default(void) {
    make_screen(&g_xr.screens[0], XR_SCREEN_STAGE, 1, 1.80f, 1.80f * 9.0f / 16.0f, 0.0f);
    xr_geom_face(&g_xr.screens[0].pose, &g_xr.head_pose, 2.35f, 0.0f);
    make_screen(&g_xr.screens[1], XR_SCREEN_MENU, 1, 0.72f, 1.08f, 0.0f);
    xr_geom_face(&g_xr.screens[1].pose, &g_xr.head_pose, 2.05f, -1.40f);
    g_xr.floor_y = g_xr.space_is_stage ? 0.0f : g_xr.head_pose.position.y - 1.45f;
    g_xr.have_floor = 1;
    g_xr.layout_ready = 1;
}

static void spawn_screen(void) {
    if (!g_xr.head_valid) {
        set_status("Look forward, then add a monitor");
        return;
    }
    int slot = -1;
    int clones = 0;
    for (int i = 2; i < XR_MAX_SCREENS; i++) {
        if (g_xr.screens[i].alive) {
            clones++;
        } else if (slot < 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        set_status("No free monitor");
        return;
    }
    float width = 1.35f;
    make_screen(&g_xr.screens[slot], XR_SCREEN_CLONE, 0, width, width * 9.0f / 16.0f, 0.0f);
    float right = 0.55f * (float)((clones % 3) - 1);
    float distance = 2.15f - 0.12f * (float)(clones / 3);
    xr_geom_face(&g_xr.screens[slot].pose, &g_xr.head_pose, distance, right);
    set_status("Added a monitor");
}

static void end_pointer(void) {
    if (!g_xr.pointer_down) {
        return;
    }
    float u = 0.0f;
    float v = 0.0f;
    int hand = g_xr.pointer_hand;
    if (hand >= 0 && hand < 2) {
        u = g_xr.hand_u[hand];
        v = g_xr.hand_v[hand];
    }
    xr_jni_post_pointer(XR_PTR_UP, u, v, g_xr.pointer_button);
    g_xr.pointer_down = 0;
    g_xr.pointer_screen = -1;
}

static int hovered_clone(void) {
    for (int hand = 0; hand < 2; hand++) {
        if (!g_xr.hand_hit[hand]) {
            continue;
        }
        int index = g_xr.hand_hover[hand];
        if (index < 2 || index >= XR_MAX_SCREENS) {
            continue;
        }
        XrScreen* screen = &g_xr.screens[index];
        if (screen->alive && screen->kind == XR_SCREEN_CLONE && !screen->locked) {
            return index;
        }
    }
    return -1;
}

static void close_screen(void) {
    int index = hovered_clone();
    if (index < 0) {
        for (int i = XR_MAX_SCREENS - 1; i >= 2; i--) {
            if (g_xr.screens[i].alive) {
                index = i;
                break;
            }
        }
    }
    if (index < 0) {
        set_status("No extra monitor to close");
        return;
    }
    g_xr.screens[index].alive = 0;
    g_xr.screens[index].grab_mask = 0;
    if (g_xr.pointer_down && g_xr.pointer_screen == index) {
        end_pointer();
    }
    set_status("Closed a monitor");
}

static void reset_layout(void) {
    for (int i = 2; i < XR_MAX_SCREENS; i++) {
        g_xr.screens[i].alive = 0;
        g_xr.screens[i].grab_mask = 0;
    }
    if (g_xr.pointer_down) {
        end_pointer();
    }
    if (g_xr.head_valid) {
        place_default();
    } else {
        g_xr.layout_ready = 0;
    }
    set_status("Monitors reset");
}

static void toggle_passthrough(void) {
    int state = xr_passthrough_toggle();
    if (state < 0) {
        set_status("Passthrough is unavailable");
    } else if (state) {
        set_status("Passthrough on");
    } else {
        set_status("Passthrough off");
    }
}

static void activate(const XrHit* hit) {
    switch (hit->action) {
        case XR_ACT_SPAWN:
            spawn_screen();
            break;
        case XR_ACT_CLOSE:
            close_screen();
            break;
        case XR_ACT_RESET:
            reset_layout();
            break;
        case XR_ACT_PASSTHROUGH:
            toggle_passthrough();
            break;
        case XR_ACT_EXIT:
            g_xr.quit = 1;
            break;
        case XR_ACT_PC:
        case XR_ACT_APP:
        case XR_ACT_BACK:
            xr_jni_post_pick(hit->action, hit->arg);
            break;
        default:
            break;
    }
}

static void put_px(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if ((unsigned)x >= (unsigned)XR_MENU_W || (unsigned)y >= (unsigned)XR_MENU_H) {
        return;
    }
    uint8_t* pixel = g_xr.menu_pixels + ((y * XR_MENU_W + x) * 4);
    pixel[0] = r;
    pixel[1] = g;
    pixel[2] = b;
    pixel[3] = 255;
}

static void fill_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= XR_MENU_W || y >= XR_MENU_H || w <= 0 || h <= 0) {
        return;
    }
    if (x + w > XR_MENU_W) {
        w = XR_MENU_W - x;
    }
    if (y + h > XR_MENU_H) {
        h = XR_MENU_H - y;
    }
    for (int row = 0; row < h; row++) {
        uint8_t* pixel = g_xr.menu_pixels + (((y + row) * XR_MENU_W + x) * 4);
        for (int col = 0; col < w; col++) {
            pixel[0] = r;
            pixel[1] = g;
            pixel[2] = b;
            pixel[3] = 255;
            pixel += 4;
        }
    }
}

static uint8_t* font_pixels;
static int font_w;
static int font_h;
static int font_ascent;
static int font_px;
static int font_count;
static int font_ready;
static int font_tried;
static XrFontGlyph font_glyphs[96];

static void xr_text_init(void) {
    if (font_ready || font_tried) {
        return;
    }
    font_tried = 1;
    uint8_t* pixels = NULL;
    int width = 0;
    int height = 0;
    int ascent = 0;
    int px = 0;
    int count = 0;
    if (!xr_jni_load_glyph_atlas(&pixels, &width, &height, &ascent, &px, font_glyphs, 96, &count)) {
        __android_log_print(ANDROID_LOG_WARN, "moonlight-xr", "system font unavailable; using bitmap font");
        return;
    }
    font_pixels = pixels;
    font_w = width;
    font_h = height;
    font_ascent = ascent;
    font_px = px;
    font_count = count;
    font_ready = 1;
    __android_log_print(ANDROID_LOG_INFO, "moonlight-xr", "system font %dpx atlas %dx%d glyphs %d", px, width, height,
                        count);
}

static const XrFontGlyph* font_glyph(unsigned char code) {
    int value = code < 128 ? (int)code : (int)'?';
    if (value < 32 || value > 126) {
        value = (int)'?';
    }
    int index = value - 32;
    if (index < 0 || index >= font_count) {
        return NULL;
    }
    return &font_glyphs[index];
}

static int atlas_alpha(float u, float v) {
    if (font_pixels == NULL || font_w < 2 || font_h < 2) {
        return 0;
    }
    if (u < 0.0f) {
        u = 0.0f;
    }
    if (v < 0.0f) {
        v = 0.0f;
    }
    float max_u = (float)font_w - 1.01f;
    float max_v = (float)font_h - 1.01f;
    if (u > max_u) {
        u = max_u;
    }
    if (v > max_v) {
        v = max_v;
    }
    int x = (int)u;
    int y = (int)v;
    float fx = u - (float)x;
    float fy = v - (float)y;
    const uint8_t* row = font_pixels + (y * font_w + x) * 4;
    int a00 = row[3];
    int a10 = row[7];
    int a01 = row[font_w * 4 + 3];
    int a11 = row[font_w * 4 + 7];
    float top = (1.0f - fx) * (float)a00 + fx * (float)a10;
    float bottom = (1.0f - fx) * (float)a01 + fx * (float)a11;
    return (int)((1.0f - fy) * top + fy * bottom);
}

static void blend_px(uint8_t* pixels, int stride, int limit_w, int limit_h, int x, int y, int r, int g, int b, int a) {
    if ((unsigned)x >= (unsigned)limit_w || (unsigned)y >= (unsigned)limit_h || a <= 0) {
        return;
    }
    uint8_t* pixel = pixels + (y * stride + x) * 4;
    if (a >= 255) {
        pixel[0] = (uint8_t)r;
        pixel[1] = (uint8_t)g;
        pixel[2] = (uint8_t)b;
        pixel[3] = 255;
        return;
    }
    pixel[0] = (uint8_t)((r * a + pixel[0] * (255 - a) + 127) / 255);
    pixel[1] = (uint8_t)((g * a + pixel[1] * (255 - a) + 127) / 255);
    pixel[2] = (uint8_t)((b * a + pixel[2] * (255 - a) + 127) / 255);
    pixel[3] = 255;
}

static int paint_width(const char* text, int px) {
    if (!font_ready || text == NULL || font_px < 1) {
        return 0;
    }
    float scale = (float)px / (float)font_px;
    float width = 0.0f;
    for (const unsigned char* glyph = (const unsigned char*)text; *glyph != 0; glyph++) {
        const XrFontGlyph* info = font_glyph(*glyph);
        if (info != NULL) {
            width += (float)info->advance * scale;
        }
    }
    return (int)(width + 0.5f);
}

static void paint_string(uint8_t* pixels, int stride, int limit_w, int limit_h, int x, int y, int px, const char* text,
                         uint8_t r, uint8_t g, uint8_t b) {
    if (!font_ready || pixels == NULL || text == NULL || px < 1 || font_px < 1) {
        return;
    }
    float scale = (float)px / (float)font_px;
    float baseline = (float)y + (float)font_ascent * scale;
    float pen = (float)x;
    for (const unsigned char* glyph = (const unsigned char*)text; *glyph != 0; glyph++) {
        const XrFontGlyph* info = font_glyph(*glyph);
        if (info == NULL) {
            continue;
        }
        if (info->src_w > 0 && info->src_h > 0 && scale > 0.0f) {
            float dx = pen + (float)info->offset_x * scale;
            float dy = baseline + (float)info->offset_y * scale;
            float dw = (float)info->src_w * scale;
            float dh = (float)info->src_h * scale;
            int x0 = (int)floorf(dx);
            int y0 = (int)floorf(dy);
            int x1 = (int)ceilf(dx + dw);
            int y1 = (int)ceilf(dy + dh);
            for (int py = y0; py < y1; py++) {
                for (int px_i = x0; px_i < x1; px_i++) {
                    float u = (((float)px_i + 0.5f) - dx) / scale + (float)info->src_x;
                    float v = (((float)py + 0.5f) - dy) / scale + (float)info->src_y;
                    blend_px(pixels, stride, limit_w, limit_h, px_i, py, r, g, b, atlas_alpha(u, v));
                }
            }
        }
        pen += (float)info->advance * scale;
        if (pen >= (float)limit_w) {
            break;
        }
    }
}

static int text_width(const char* text, int px) {
    if (font_ready) {
        return paint_width(text, px);
    }
    int count = 0;
    int scale = px / 8;
    if (scale < 1) {
        scale = 1;
    }
    if (text != NULL) {
        while (text[count] != '\0') {
            count++;
        }
    }
    return count * 8 * scale;
}

static void draw_string(int x, int y, int px, const char* text, uint8_t r, uint8_t g, uint8_t b) {
    if (text == NULL || px < 1) {
        return;
    }
    if (font_ready) {
        paint_string(g_xr.menu_pixels, XR_MENU_W, XR_MENU_W, XR_MENU_H, x, y, px, text, r, g, b);
        return;
    }
    int scale = px / 8;
    if (scale < 1) {
        scale = 1;
    }
    int cursor = x;
    for (const unsigned char* glyph = (const unsigned char*)text; *glyph != 0; glyph++) {
        int code = *glyph < 128 ? *glyph : (int)'?';
        for (int row = 0; row < 8; row++) {
            unsigned char bits = (unsigned char)font8x8_basic[code][row];
            for (int col = 0; col < 8; col++) {
                if ((bits & (unsigned char)(1u << col)) == 0) {
                    continue;
                }
                int px = cursor + col * scale;
                int py = y + row * scale;
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        put_px(px + sx, py + sy, r, g, b);
                    }
                }
            }
        }
        cursor += 8 * scale;
        if (cursor >= XR_MENU_W) {
            break;
        }
    }
}

static void add_hit(int x, int y, int w, int h, int action, int arg) {
    if (g_xr.hit_count >= XR_MAX_HITS) {
        return;
    }
    XrHit* hit = &g_xr.hits[g_xr.hit_count++];
    hit->x = x;
    hit->y = y;
    hit->w = w;
    hit->h = h;
    hit->action = action;
    hit->arg = arg;
}

static void face_fill(uint8_t* pixels, int stride, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= XR_MONITOR_W || y >= XR_MONITOR_H || w <= 0 || h <= 0) {
        return;
    }
    if (x + w > XR_MONITOR_W) {
        w = XR_MONITOR_W - x;
    }
    if (y + h > XR_MONITOR_H) {
        h = XR_MONITOR_H - y;
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

static void face_px(uint8_t* pixels, int stride, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if ((unsigned)x >= (unsigned)XR_MONITOR_W || (unsigned)y >= (unsigned)XR_MONITOR_H) {
        return;
    }
    uint8_t* pixel = pixels + ((y * stride + x) * 4);
    pixel[0] = r;
    pixel[1] = g;
    pixel[2] = b;
    pixel[3] = 255;
}

static void face_string(uint8_t* pixels, int x, int y, int px, const char* text, uint8_t r, uint8_t g, uint8_t b) {
    if (text == NULL || px < 1) {
        return;
    }
    if (font_ready) {
        paint_string(pixels, XR_MONITOR_W, XR_MONITOR_W, XR_MONITOR_H, x, y, px, text, r, g, b);
        return;
    }
    int scale = px / 8;
    if (scale < 1) {
        scale = 1;
    }
    int cursor = x;
    for (const unsigned char* glyph = (const unsigned char*)text; *glyph != 0; glyph++) {
        int code = *glyph < 128 ? *glyph : (int)'?';
        for (int row = 0; row < 8; row++) {
            unsigned char bits = (unsigned char)font8x8_basic[code][row];
            for (int col = 0; col < 8; col++) {
                if ((bits & (unsigned char)(1u << col)) == 0) {
                    continue;
                }
                int px = cursor + col * scale;
                int py = y + row * scale;
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        face_px(pixels, XR_MONITOR_W, px + sx, py + sy, r, g, b);
                    }
                }
            }
        }
        cursor += 8 * scale;
        if (cursor >= XR_MONITOR_W) {
            break;
        }
    }
}

static void face_centered(uint8_t* pixels, int y, int px, const char* text, uint8_t r, uint8_t g, uint8_t b) {
    int width = text_width(text, px);
    face_string(pixels, (XR_MONITOR_W - width) / 2, y, px, text, r, g, b);
}

static int ui_aniso(void) {
    static int checked;
    static int supported;
    if (checked) {
        return supported;
    }
    checked = 1;
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; i++) {
        const char* name = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (name != NULL && strcmp(name, "GL_EXT_texture_filter_anisotropic") == 0) {
            supported = 1;
            break;
        }
    }
    return supported;
}

static void upload_ui_texture(GLuint* tex, const uint8_t* pixels, int width, int height) {
    if (*tex == 0) {
        glGenTextures(1, tex);
    }
    if (*tex == 0) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    if (ui_aniso()) {
        glTexParameterf(GL_TEXTURE_2D, 0x84FE, 8.0f);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void xr_upload_monitor_face(void) {
    const int width = XR_MONITOR_W;
    const int height = XR_MONITOR_H;
    xr_text_init();
    uint8_t* pixels = (uint8_t*)calloc((size_t)width * (size_t)height * 4u, 1u);
    if (pixels == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "monitor face allocation failed");
        return;
    }
    face_fill(pixels, width, 0, 0, width, height, 36, 40, 48);
    face_fill(pixels, width, 8, 8, width - 16, height - 16, 12, 16, 24);
    face_fill(pixels, width, 8, 8, width - 16, 3, 58, 68, 82);

    face_centered(pixels, 390, 84, "YOUR MONITOR", 226, 234, 242);
    face_centered(pixels, 510, 48, "The PC stream shows on this screen.", 176, 190, 204);
    face_centered(pixels, 576, 48, "Pick a computer on the menu.", 176, 190, 204);
    face_centered(pixels, 642, 48, "Grip moves it. A or X adds another.", 176, 190, 204);

    upload_ui_texture(&g_xr.placeholder_tex, pixels, width, height);
    free(pixels);
    if (g_xr.placeholder_tex == 0) {
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "monitor face texture failed");
        return;
    }
    __android_log_print(ANDROID_LOG_INFO, "moonlight-xr", "monitor face %dx%d", width, height);
}

static void draw_button(int y, const char* label, int action) {
    int x = 32;
    int w = XR_MENU_W - 64;
    int h = 92;
    int hover = g_xr.hit_count == g_xr.menu_hover_hit;
    if (hover) {
        fill_rect(x, y, w, h, 30, 90, 110);
    } else {
        fill_rect(x, y, w, h, 28, 36, 48);
    }
    fill_rect(x, y, w, 4, 70, 150, 170);
    int width = text_width(label, 36);
    draw_string(x + (w - width) / 2, y + 28, 36, label, 230, 240, 245);
    add_hit(x, y, w, h, action, 0);
}

static void rebuild_menu(void) {
    if (g_xr.menu_pixels == NULL) {
        return;
    }
    XrRow rows[XR_MAX_ROWS];
    char status[160];
    int row_count = 0;
    pthread_mutex_lock(&g_xr.lib_mu);
    row_count = g_xr.row_count;
    if (row_count < 0) {
        row_count = 0;
    }
    if (row_count > XR_MAX_ROWS) {
        row_count = XR_MAX_ROWS;
    }
    memcpy(rows, g_xr.rows, sizeof(rows[0]) * (size_t)row_count);
    memcpy(status, g_xr.status, sizeof(status));
    g_xr.menu_dirty = 0;
    pthread_mutex_unlock(&g_xr.lib_mu);

    xr_text_init();
    fill_rect(0, 0, XR_MENU_W, XR_MENU_H, 12, 14, 20);
    g_xr.hit_count = 0;
    int title_w = text_width("MOONLIGHT VR", 44);
    draw_string((XR_MENU_W - title_w) / 2, 28, 44, "MOONLIGHT VR", 220, 230, 240);
    draw_string(32, 88, 28, status, 150, 165, 180);
    draw_string(32, 128, 28, "Point at a computer, then an app.", 120, 140, 160);

    int row_y = 176;
    /* Five controls stay on the panel even when every library row is filled. */
    int rows_limit = XR_MENU_H - (5 * 96) - 16;
    if (row_count == 0) {
        draw_string(56, row_y + 16, 30, "No computers online", 150, 165, 180);
        row_y += 72;
    }
    for (int i = 0; i < row_count; i++) {
        int x = 32;
        int w = XR_MENU_W - 64;
        int h = 64;
        if (row_y + h > rows_limit) {
            break;
        }
        int hover = g_xr.hit_count == g_xr.menu_hover_hit;
        if (hover) {
            fill_rect(x, row_y, w, h, 24, 70, 88);
        } else {
            fill_rect(x, row_y, w, h, 20, 24, 32);
        }
        draw_string(56, row_y + 16, 32, rows[i].label, 220, 228, 235);
        add_hit(x, row_y, w, h, rows[i].kind, rows[i].id);
        row_y += 68;
    }

    int button_y = row_y + 16;
    if (button_y + 5 * 96 > XR_MENU_H - 8) {
        button_y = XR_MENU_H - 8 - 5 * 96;
    }
    draw_button(button_y, "Add monitor", XR_ACT_SPAWN);
    button_y += 96;
    draw_button(button_y, "Close monitor", XR_ACT_CLOSE);
    button_y += 96;
    draw_button(button_y, "Reset monitors", XR_ACT_RESET);
    button_y += 96;
    draw_button(button_y, "Passthrough", XR_ACT_PASSTHROUGH);
    button_y += 96;
    draw_button(button_y, "Exit to app", XR_ACT_EXIT);

    upload_ui_texture(&g_xr.menu_tex, g_xr.menu_pixels, XR_MENU_W, XR_MENU_H);
}

static void locate_head(XrTime time) {
    if (g_xr.view_space == XR_NULL_HANDLE || g_xr.ref_space == XR_NULL_HANDLE) {
        return;
    }
    XrSpaceLocation location;
    memset(&location, 0, sizeof(location));
    location.type = XR_TYPE_SPACE_LOCATION;
    if (XR_FAILED(xrLocateSpace(g_xr.view_space, g_xr.ref_space, time, &location))) {
        return;
    }
    if ((location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0 ||
        (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 0) {
        return;
    }
    g_xr.head_pose = location.pose;
    g_xr.head_valid = 1;
}

static void raycast_all(void) {
    int menu_hover = -1;
    int saw_menu = 0;
    float menu_t = 1.0e30f;
    for (int hand = 0; hand < 2; hand++) {
        g_xr.hand_hit[hand] = 0;
        g_xr.hand_hover[hand] = -1;
        g_xr.hand_menu_hit[hand] = -1;
        if (!g_xr.hand_active[hand]) {
            continue;
        }
        XrVector3f forward = {0.0f, 0.0f, -1.0f};
        XrVector3f direction;
        XrQuaternionf_RotateVector3f(&direction, &g_xr.aim_pose[hand].orientation, &forward);
        int best = -1;
        float best_t = 1.0e30f;
        float best_u = 0.0f;
        float best_v = 0.0f;
        XrVector3f best_point = {0.0f, 0.0f, 0.0f};
        for (int index = 0; index < XR_MAX_SCREENS; index++) {
            XrScreen* screen = &g_xr.screens[index];
            if (!screen->alive || !screen->visible) {
                continue;
            }
            float t = 0.0f;
            float u = 0.0f;
            float v = 0.0f;
            XrVector3f point;
            if (!xr_geom_ray(&screen->pose, screen->width, screen->height, screen->curve,
                             &g_xr.aim_pose[hand].position, &direction, &t, &u, &v, &point)) {
                continue;
            }
            if (t < best_t) {
                best_t = t;
                best = index;
                best_u = u;
                best_v = v;
                best_point = point;
            }
        }
        if (best < 0) {
            continue;
        }
        g_xr.hand_hit[hand] = 1;
        g_xr.hand_hover[hand] = best;
        g_xr.hand_u[hand] = best_u;
        g_xr.hand_v[hand] = best_v;
        g_xr.hand_point[hand] = best_point;
        if (g_xr.screens[best].kind != XR_SCREEN_MENU) {
            continue;
        }
        int px = (int)(best_u * (float)XR_MENU_W);
        int py = (int)((1.0f - best_v) * (float)XR_MENU_H);
        for (int hit_index = 0; hit_index < g_xr.hit_count; hit_index++) {
            XrHit* hit = &g_xr.hits[hit_index];
            if (px >= hit->x && py >= hit->y && px < hit->x + hit->w && py < hit->y + hit->h) {
                g_xr.hand_menu_hit[hand] = hit_index;
                break;
            }
        }
        if (!saw_menu || best_t < menu_t) {
            saw_menu = 1;
            menu_t = best_t;
            menu_hover = g_xr.hand_menu_hit[hand];
        }
    }
    if (!saw_menu) {
        menu_hover = -1;
    }
    if (menu_hover != g_xr.menu_hover_hit) {
        g_xr.menu_hover_hit = menu_hover;
        g_xr.menu_dirty = 1;
    }
}

static void capture_hand(XrScreen* screen, int hand) {
    xr_geom_capture(&screen->grab_offset, &g_xr.grip_pose[hand], &screen->pose);
    screen->primary_hand = hand;
    screen->two_dist = 0.0f;
}

static void press_hand(int hand) {
    if (!g_xr.hand_hit[hand]) {
        return;
    }
    int index = g_xr.hand_hover[hand];
    if (index < 0 || index >= XR_MAX_SCREENS) {
        return;
    }
    XrScreen* screen = &g_xr.screens[index];
    if (!screen->alive || !g_xr.grip_valid[hand]) {
        return;
    }
    if (screen->grab_mask == 0) {
        capture_hand(screen, hand);
    } else {
        screen->two_dist = 0.0f;
    }
    screen->grab_mask |= (1 << hand);
}

static void release_hand(int hand) {
    int other = hand ^ 1;
    for (int index = 0; index < XR_MAX_SCREENS; index++) {
        XrScreen* screen = &g_xr.screens[index];
        if ((screen->grab_mask & (1 << hand)) == 0) {
            continue;
        }
        screen->grab_mask &= ~(1 << hand);
        screen->two_dist = 0.0f;
        if (screen->primary_hand != hand) {
            continue;
        }
        if ((screen->grab_mask & (1 << other)) != 0 && g_xr.grip_valid[other]) {
            capture_hand(screen, other);
        } else {
            screen->primary_hand = -1;
        }
    }
}

static void set_width(XrScreen* screen, float width) {
    if (width < 0.25f) {
        width = 0.25f;
    }
    if (width > 4.0f) {
        width = 4.0f;
    }
    screen->width = width;
    if (screen->aspect > 0.01f) {
        screen->height = width / screen->aspect;
    }
}

static void move_grabs(float dt) {
    for (int index = 0; index < XR_MAX_SCREENS; index++) {
        XrScreen* screen = &g_xr.screens[index];
        if (!screen->alive || screen->grab_mask == 0) {
            continue;
        }
        if (screen->primary_hand < 0) {
            for (int hand = 0; hand < 2; hand++) {
                if ((screen->grab_mask & (1 << hand)) != 0 && g_xr.grip_valid[hand]) {
                    capture_hand(screen, hand);
                    break;
                }
            }
        }
        int primary = screen->primary_hand;
        int both = (screen->grab_mask & 1) != 0 && (screen->grab_mask & 2) != 0 && g_xr.grip_valid[0] &&
                   g_xr.grip_valid[1];
        int primary_held = primary >= 0 && (screen->grab_mask & (1 << primary)) != 0 && g_xr.grip_valid[primary];
        if (primary_held) {
            xr_geom_apply(&screen->pose, &g_xr.grip_pose[primary], &screen->grab_offset);
        }
        if (both) {
            XrVector3f delta;
            XrVector3f_Sub(&delta, &g_xr.grip_pose[0].position, &g_xr.grip_pose[1].position);
            float distance = XrVector3f_Length(&delta);
            if (screen->two_dist < 0.02f) {
                if (distance > 0.02f) {
                    screen->two_dist = distance;
                    screen->two_width = screen->width;
                }
            } else if (distance > 0.02f) {
                set_width(screen, screen->two_width * (distance / screen->two_dist));
            }
        } else if (primary_held) {
            float stick_x = g_xr.stick_x[primary];
            float stick_y = g_xr.stick_y[primary];
            int dolled = 0;
            if (fabsf(stick_x) > 0.25f) {
                set_width(screen, screen->width * (1.0f + 0.75f * dt * stick_x));
            }
            if (fabsf(stick_y) > 0.25f) {
                xr_geom_dolly(&screen->pose, 1.15f * dt * stick_y);
                dolled = 1;
            }
            if (dolled) {
                xr_geom_capture(&screen->grab_offset, &g_xr.grip_pose[primary], &screen->pose);
            }
        }
    }
}

static void update_grabs(float dt) {
    for (int hand = 0; hand < 2; hand++) {
        int can_grab = g_xr.hand_active[hand] && g_xr.grip_valid[hand];
        float squeeze = g_xr.squeeze_value[hand];
        int pressed = can_grab && !g_xr.squeeze_held[hand] && squeeze >= 0.70f;
        int released = g_xr.squeeze_held[hand] && (!can_grab || squeeze < 0.40f);
        if (pressed) {
            press_hand(hand);
        }
        if (released) {
            release_hand(hand);
        }
    }
    move_grabs(dt);
    for (int hand = 0; hand < 2; hand++) {
        int can_grab = g_xr.hand_active[hand] && g_xr.grip_valid[hand];
        float squeeze = g_xr.squeeze_value[hand];
        if (g_xr.squeeze_held[hand]) {
            g_xr.squeeze_held[hand] = can_grab && squeeze >= 0.40f;
        } else {
            g_xr.squeeze_held[hand] = can_grab && squeeze >= 0.70f;
        }
    }
}

static int gripping(int hand) {
    return g_xr.squeeze_held[hand];
}

/* Stage and clones are the desktop. The menu is not a mouse surface. */
static int stream_screen(int index) {
    if (index < 0 || index >= XR_MAX_SCREENS) {
        return 0;
    }
    XrScreen* screen = &g_xr.screens[index];
    return screen->alive && screen->visible && screen->kind != XR_SCREEN_MENU && screen->grab_mask == 0;
}

/* Absolute mouse, sent on the XR thread. Moonlight keeps only the latest
 * position, so a still laser does not need a packet every frame. 8192 covers
 * a 4K stream with less than one pixel of slack. */
static void update_hover_pointer(void) {
    if (g_xr.pointer_down) {
        g_xr.hover_hand = -1;
        return;
    }
    int hand = -1;
    int previous = g_xr.hover_hand;
    if (previous >= 0 && previous < 2 && !gripping(previous) && g_xr.hand_hit[previous] &&
        stream_screen(g_xr.hand_hover[previous])) {
        hand = previous;
    } else {
        for (int i = 0; i < 2; i++) {
            if (gripping(i) || !g_xr.hand_hit[i] || !stream_screen(g_xr.hand_hover[i])) {
                continue;
            }
            hand = i;
            break;
        }
    }
    g_xr.hover_hand = hand;
    if (hand < 0) {
        g_xr.hover_screen = -1;
        return;
    }
    float u = g_xr.hand_u[hand];
    float v = g_xr.hand_v[hand];
    int screen = g_xr.hand_hover[hand];
    int qx = (int)lrintf(u * 8192.0f);
    int qy = (int)lrintf(v * 8192.0f);
    if (screen == g_xr.hover_screen && qx == g_xr.hover_qx && qy == g_xr.hover_qy) {
        return;
    }
    g_xr.hover_screen = screen;
    g_xr.hover_qx = qx;
    g_xr.hover_qy = qy;
    xr_jni_post_pointer(XR_PTR_MOVE, u, v, 0);
}

static void update_pointer(XrTime time) {
    int started_hand = -1;
    for (int hand = 0; hand < 2; hand++) {
        int active = g_xr.hand_active[hand];
        float trigger = g_xr.trigger_value[hand];
        int trigger_down = active && !g_xr.trigger_held[hand] && trigger >= 0.60f;
        int stick_down = g_xr.stick_click[hand] && !g_xr.stick_held[hand];
        int on_screen = g_xr.hand_hit[hand] && g_xr.hand_hover[hand] >= 0;
        XrScreen* screen = on_screen ? &g_xr.screens[g_xr.hand_hover[hand]] : NULL;
        if (trigger_down && !gripping(hand) && screen != NULL && screen->grab_mask == 0) {
            if (screen->kind == XR_SCREEN_MENU) {
                int hit_index = g_xr.hand_menu_hit[hand];
                if (hit_index >= 0 && hit_index < g_xr.hit_count) {
                    activate(&g_xr.hits[hit_index]);
                }
            } else if (!g_xr.pointer_down) {
                g_xr.pointer_down = 1;
                g_xr.pointer_hand = hand;
                g_xr.pointer_button = 0;
                g_xr.pointer_screen = g_xr.hand_hover[hand];
                started_hand = hand;
                xr_jni_post_pointer(XR_PTR_DOWN, g_xr.hand_u[hand], g_xr.hand_v[hand], 0);
            }
        } else if (stick_down && !gripping(hand) && screen != NULL && screen->kind != XR_SCREEN_MENU &&
                   screen->grab_mask == 0 && !g_xr.pointer_down) {
            g_xr.pointer_down = 1;
            g_xr.pointer_hand = hand;
            g_xr.pointer_button = 1;
            g_xr.pointer_screen = g_xr.hand_hover[hand];
            started_hand = hand;
            xr_jni_post_pointer(XR_PTR_DOWN, g_xr.hand_u[hand], g_xr.hand_v[hand], 1);
        }
    }

    if (g_xr.pointer_down && g_xr.pointer_hand != started_hand) {
        int hand = g_xr.pointer_hand;
        int screen_index = g_xr.pointer_screen;
        int on_same = g_xr.hand_hit[hand] && g_xr.hand_hover[hand] == screen_index;
        int held = 0;
        if (g_xr.pointer_button == 0) {
            held = g_xr.hand_active[hand] && g_xr.trigger_value[hand] >= 0.35f;
        } else {
            held = g_xr.stick_click[hand];
        }
        int blocked = gripping(hand) || screen_index < 0 || screen_index >= XR_MAX_SCREENS ||
                      (g_xr.screens[screen_index].grab_mask & (1 << hand)) != 0;
        if (!on_same || !held || blocked) {
            end_pointer();
        } else {
            xr_jni_post_pointer(XR_PTR_MOVE, g_xr.hand_u[hand], g_xr.hand_v[hand], g_xr.pointer_button);
        }
    }

    update_hover_pointer();

    for (int hand = 0; hand < 2; hand++) {
        if (g_xr.pointer_down || gripping(hand) || !g_xr.hand_hit[hand]) {
            continue;
        }
        int index = g_xr.hand_hover[hand];
        if (index < 0 || index >= XR_MAX_SCREENS) {
            continue;
        }
        XrScreen* screen = &g_xr.screens[index];
        if (!screen->alive || screen->kind == XR_SCREEN_MENU || screen->grab_mask != 0) {
            continue;
        }
        if (fabsf(g_xr.stick_y[hand]) <= 0.55f) {
            continue;
        }
        if (g_xr.last_scroll_time != 0 && time - g_xr.last_scroll_time < 150000000LL) {
            continue;
        }
        g_xr.last_scroll_time = time;
        xr_jni_post_pointer(XR_PTR_SCROLL, g_xr.hand_u[hand], g_xr.hand_v[hand],
                            g_xr.stick_y[hand] > 0.0f ? 1 : -1);
        break;
    }

    for (int hand = 0; hand < 2; hand++) {
        int active = g_xr.hand_active[hand];
        float trigger = g_xr.trigger_value[hand];
        if (g_xr.trigger_held[hand]) {
            g_xr.trigger_held[hand] = active && trigger >= 0.35f;
        } else {
            g_xr.trigger_held[hand] = active && trigger >= 0.60f;
        }
        g_xr.stick_held[hand] = g_xr.stick_click[hand];
    }
}

static void update_buttons(void) {
    if ((g_xr.btn_a && !g_xr.was_a) || (g_xr.btn_x && !g_xr.was_x)) {
        spawn_screen();
    }
    if ((g_xr.btn_b && !g_xr.was_b) || (g_xr.btn_y && !g_xr.was_y)) {
        toggle_passthrough();
    }
    if (g_xr.btn_menu && !g_xr.was_menu && g_xr.screens[1].alive) {
        g_xr.screens[1].visible = !g_xr.screens[1].visible;
        if (!g_xr.screens[1].visible && g_xr.pointer_down && g_xr.pointer_screen == 1) {
            end_pointer();
        }
    }
    g_xr.was_a = g_xr.btn_a;
    g_xr.was_b = g_xr.btn_b;
    g_xr.was_x = g_xr.btn_x;
    g_xr.was_y = g_xr.btn_y;
    g_xr.was_menu = g_xr.btn_menu;
}

void xr_screens_init(void) {
    if (!g_xr.lib_mu_ready) {
        pthread_mutex_init(&g_xr.lib_mu, NULL);
        g_xr.lib_mu_ready = 1;
    }
    free(g_xr.menu_pixels);
    g_xr.menu_pixels = (uint8_t*)calloc((size_t)XR_MENU_W * (size_t)XR_MENU_H * 4u, 1u);
    if (g_xr.menu_tex != 0) {
        glDeleteTextures(1, &g_xr.menu_tex);
        g_xr.menu_tex = 0;
    }
    memset(g_xr.screens, 0, sizeof(g_xr.screens));
    g_xr.layout_ready = 0;
    g_xr.head_valid = 0;
    g_xr.have_floor = 0;
    g_xr.menu_hover_hit = -1;
    g_xr.hit_count = 0;
    g_xr.pointer_down = 0;
    g_xr.pointer_hand = -1;
    g_xr.pointer_button = 0;
    g_xr.pointer_screen = -1;
    g_xr.hover_hand = -1;
    g_xr.hover_screen = -1;
    g_xr.hover_qx = -1;
    g_xr.hover_qy = -1;
    g_xr.last_scroll_time = 0;
    g_xr.hand_hit[0] = g_xr.hand_hit[1] = 0;
    g_xr.hand_hover[0] = g_xr.hand_hover[1] = -1;
    g_xr.hand_menu_hit[0] = g_xr.hand_menu_hit[1] = -1;
    g_xr.squeeze_held[0] = g_xr.squeeze_held[1] = 0;
    g_xr.trigger_held[0] = g_xr.trigger_held[1] = 0;
    g_xr.stick_held[0] = g_xr.stick_held[1] = 0;
    g_xr.was_a = g_xr.was_b = g_xr.was_x = g_xr.was_y = g_xr.was_menu = 0;
    pthread_mutex_lock(&g_xr.lib_mu);
    g_xr.row_count = 0;
    pthread_mutex_unlock(&g_xr.lib_mu);
    set_status(k_hint);
}

void xr_screens_destroy(void) {
    free(font_pixels);
    font_pixels = NULL;
    font_ready = 0;
    font_tried = 0;
    free(g_xr.menu_pixels);
    g_xr.menu_pixels = NULL;
    if (g_xr.menu_tex != 0) {
        glDeleteTextures(1, &g_xr.menu_tex);
        g_xr.menu_tex = 0;
    }
}

void xr_screens_update(XrTime display_time, XrDuration period) {
    locate_head(display_time);
    float dt = 0.011f;
    if (period > 0) {
        float seconds = (float)period / 1000000000.0f;
        if (seconds > 0.0f && seconds <= 0.1f) {
            dt = seconds;
        }
    }
    if (!g_xr.layout_ready && g_xr.head_valid) {
        place_default();
    }
    if (g_xr.menu_dirty) {
        rebuild_menu();
    }
    raycast_all();
    update_grabs(dt);
    update_buttons();
    raycast_all();
    /* The second aim is the one the laser draws, so the host cursor matches it. */
    update_pointer(display_time);
    if (g_xr.menu_dirty) {
        rebuild_menu();
    }
}
