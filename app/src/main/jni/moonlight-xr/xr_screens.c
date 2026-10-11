#include "xr_internal.h"

#include "xr_geom.h"
#include "xr_linear.h"
#include "font8x8_basic.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Bitmap row 0 is the top of the panel. The texture shader flips v, and a hit
 * uses py = (1 - v) * height so the title at row 0 stays at the top. */

static const char* k_hint = "Grip moves  |  A/X spawns  |  menu shows this panel";

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
    make_screen(&g_xr.screens[0], XR_SCREEN_STAGE, 1, 1.80f, 1.80f * 9.0f / 16.0f, 2.5f);
    xr_geom_face(&g_xr.screens[0].pose, &g_xr.head_pose, 2.35f, 0.0f);
    make_screen(&g_xr.screens[1], XR_SCREEN_MENU, 1, 0.72f, 1.08f, 0.0f);
    xr_geom_face(&g_xr.screens[1].pose, &g_xr.head_pose, 2.05f, -1.40f);
    g_xr.floor_y = g_xr.space_is_stage ? 0.0f : g_xr.head_pose.position.y - 1.45f;
    g_xr.have_floor = 1;
    g_xr.layout_ready = 1;
}

static void spawn_screen(void) {
    if (!g_xr.head_valid) {
        set_status("Look forward, then spawn again");
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
        set_status("No free screen");
        return;
    }
    float width = 1.35f;
    make_screen(&g_xr.screens[slot], XR_SCREEN_CLONE, 0, width, width * 9.0f / 16.0f, 2.2f);
    float right = 0.55f * (float)((clones % 3) - 1);
    float distance = 2.15f - 0.12f * (float)(clones / 3);
    xr_geom_face(&g_xr.screens[slot].pose, &g_xr.head_pose, distance, right);
    set_status("Spawned a screen");
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
        set_status("No extra screen to close");
        return;
    }
    g_xr.screens[index].alive = 0;
    g_xr.screens[index].grab_mask = 0;
    if (g_xr.pointer_down && g_xr.pointer_screen == index) {
        end_pointer();
    }
    set_status("Closed a screen");
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
    set_status("Screens reset");
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

static int text_width(const char* text, int scale) {
    int count = 0;
    if (text != NULL) {
        while (text[count] != '\0') {
            count++;
        }
    }
    return count * 8 * scale;
}

static void draw_string(int x, int y, int scale, const char* text, uint8_t r, uint8_t g, uint8_t b) {
    if (text == NULL || scale < 1) {
        return;
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

static void draw_button(int y, const char* label, int action) {
    int x = 16;
    int w = XR_MENU_W - 32;
    int h = 44;
    int hover = g_xr.hit_count == g_xr.menu_hover_hit;
    if (hover) {
        fill_rect(x, y, w, h, 30, 90, 110);
    } else {
        fill_rect(x, y, w, h, 28, 36, 48);
    }
    fill_rect(x, y, w, 2, 70, 150, 170);
    int width = text_width(label, 2);
    draw_string(x + (w - width) / 2, y + 14, 2, label, 230, 240, 245);
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

    fill_rect(0, 0, XR_MENU_W, XR_MENU_H, 12, 14, 20);
    g_xr.hit_count = 0;
    int title_w = text_width("MOONLIGHT VR", 2);
    draw_string((XR_MENU_W - title_w) / 2, 14, 2, "MOONLIGHT VR", 220, 230, 240);
    draw_string(16, 44, 1, status, 150, 165, 180);

    draw_button(68, "Spawn", XR_ACT_SPAWN);
    draw_button(114, "Close", XR_ACT_CLOSE);
    draw_button(160, "Reset", XR_ACT_RESET);
    draw_button(206, "Passthrough", XR_ACT_PASSTHROUGH);
    draw_button(252, "Exit to app", XR_ACT_EXIT);
    draw_string(16, 304, 1, "LIBRARY", 120, 140, 160);

    int row_y = 322;
    for (int i = 0; i < row_count; i++) {
        int x = 16;
        int w = XR_MENU_W - 32;
        int h = 30;
        int hover = g_xr.hit_count == g_xr.menu_hover_hit;
        if (hover) {
            fill_rect(x, row_y, w, h, 24, 70, 88);
        } else {
            fill_rect(x, row_y, w, h, 20, 24, 32);
        }
        draw_string(28, row_y + 11, 1, rows[i].label, 220, 228, 235);
        add_hit(x, row_y, w, h, rows[i].kind, rows[i].id);
        row_y += 32;
        if (row_y >= XR_MENU_H - 8) {
            break;
        }
    }

    if (g_xr.menu_tex == 0) {
        glGenTextures(1, &g_xr.menu_tex);
    }
    if (g_xr.menu_tex == 0) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, g_xr.menu_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, XR_MENU_W, XR_MENU_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_xr.menu_pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
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
    update_pointer(display_time);
    update_buttons();
    raycast_all();
    if (g_xr.menu_dirty) {
        rebuild_menu();
    }
}
