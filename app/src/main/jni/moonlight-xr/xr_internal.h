#ifndef MOONLIGHT_XR_INTERNAL_H
#define MOONLIGHT_XR_INTERNAL_H

#include <jni.h>
#include <pthread.h>
#include <stdint.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define XR_MAX_VIEWS 2
#define XR_MAX_SCREENS 8
#define XR_MAX_ROWS 12
#define XR_ROW_CHARS 48
#define XR_MENU_W 512
#define XR_MENU_H 768
#define XR_MAX_HITS 24

enum {
    XR_SCREEN_STAGE = 0,
    XR_SCREEN_MENU = 1,
    XR_SCREEN_CLONE = 2
};

enum {
    XR_ACT_SPAWN = 1,
    XR_ACT_CLOSE = 2,
    XR_ACT_PASSTHROUGH = 3,
    XR_ACT_RESET = 4,
    XR_ACT_EXIT = 5,
    XR_ACT_PC = 6,
    XR_ACT_APP = 7,
    XR_ACT_BACK = 8
};

enum {
    XR_PTR_DOWN = 0,
    XR_PTR_MOVE = 1,
    XR_PTR_UP = 2,
    XR_PTR_SCROLL = 3
};

typedef struct XrScreen {
    int alive;
    int kind;
    int locked;
    int visible;
    XrPosef pose;
    float width;
    float height;
    float curve;
    float aspect;
    int grab_mask;
    int primary_hand;
    XrPosef grab_offset;
    float two_dist;
    float two_width;
} XrScreen;

typedef struct XrRow {
    char label[XR_ROW_CHARS];
    int kind;
    int id;
} XrRow;

typedef struct XrHit {
    int x, y, w, h;
    int action;
    int arg;
} XrHit;

typedef struct XrApp {
    JavaVM* vm;
    jobject activity;
    jmethodID on_session_ended;
    jmethodID on_library_pick;
    jmethodID on_pointer;

    pthread_t thread;
    int thread_started;
    volatile int quit;
    int init_state;
    pthread_mutex_t init_mu;
    pthread_cond_t init_cv;

    XrInstance instance;
    XrSystemId system_id;
    XrSession session;
    XrSpace ref_space;
    XrSpace view_space;
    int space_is_stage;
    XrSessionState session_state;
    int session_running;
    int exit_requested;
    XrEnvironmentBlendMode blend_mode;
    int alpha_blend;
    char system_name[XR_MAX_SYSTEM_NAME_SIZE];

    int ext_fb_passthrough;
    int ext_pico_passthrough;
    int ext_cylinder;
    int ext_local_floor;

    EGLDisplay dpy;
    EGLContext ctx;
    EGLSurface surf;
    EGLConfig config;

    uint32_t view_count;
    XrViewConfigurationView view_config[XR_MAX_VIEWS];
    XrSwapchain swapchain[XR_MAX_VIEWS];
    uint32_t swap_w[XR_MAX_VIEWS];
    uint32_t swap_h[XR_MAX_VIEWS];
    uint32_t image_count[XR_MAX_VIEWS];
    XrSwapchainImageOpenGLESKHR* images[XR_MAX_VIEWS];
    GLuint fbo[XR_MAX_VIEWS];
    GLuint depth[XR_MAX_VIEWS];

    pthread_mutex_t lib_mu;
    int lib_mu_ready;

    int input_ok;
    XrActionSet action_set;
    XrAction action_aim;
    XrAction action_grip;
    XrAction action_squeeze;
    XrAction action_trigger;
    XrAction action_stick;
    XrAction action_stick_click;
    XrAction action_a;
    XrAction action_b;
    XrAction action_x;
    XrAction action_y;
    XrAction action_menu;
    XrSpace aim_space[2];
    XrSpace grip_space[2];
    XrPath hand_path[2];
    int hand_active[2];
    int grip_valid[2];
    XrPosef aim_pose[2];
    XrPosef grip_pose[2];
    float squeeze_value[2];
    float trigger_value[2];
    float stick_x[2];
    float stick_y[2];
    int stick_click[2];
    int btn_a, btn_b, btn_x, btn_y, btn_menu;

    XrPosef head_pose;
    int head_valid;
    float floor_y;
    int have_floor;

    XrScreen screens[XR_MAX_SCREENS];
    int layout_ready;
    int hand_hit[2];
    int hand_hover[2];
    float hand_u[2];
    float hand_v[2];
    XrVector3f hand_point[2];
    int hand_menu_hit[2];
    int menu_hover_hit;
    int squeeze_held[2];
    int trigger_held[2];
    int stick_held[2];
    int was_a, was_b, was_x, was_y, was_menu;
    int pointer_down;
    int pointer_hand;
    int pointer_button;
    int pointer_screen;
    XrTime last_scroll_time;

    XrRow rows[XR_MAX_ROWS];
    int row_count;
    char status[160];
    int menu_dirty;
    XrHit hits[XR_MAX_HITS];
    int hit_count;
    GLuint menu_tex;
    uint8_t* menu_pixels;

    int passthrough_on;
    int passthrough_supported;

    GLuint prog_solid;
    GLuint prog_tex;
    GLint loc_solid_mvp;
    GLint loc_solid_color;
    GLint loc_tex_mvp;
    GLuint vbo;
    GLuint vao;
    GLuint placeholder_tex;
} XrApp;

extern XrApp g_xr;

void xr_world_init(void);
void xr_world_destroy(void);
void xr_world_update(XrTime display_time, XrDuration period);
void xr_world_render(const XrView* view, int eye, uint32_t width, uint32_t height);
XrEnvironmentBlendMode xr_world_blend_mode(void);
XrCompositionLayerFlags xr_world_layer_flags(void);
int xr_world_push_underlays(const XrCompositionLayerBaseHeader** layers, int count);

void xr_input_init(void);
void xr_input_destroy(void);
void xr_input_sync(XrTime display_time);

void xr_screens_init(void);
void xr_screens_destroy(void);
void xr_screens_update(XrTime display_time, XrDuration period);

void xr_render_init(void);
void xr_render_destroy(void);
void xr_render_eye(const XrView* view, int eye, uint32_t width, uint32_t height);

void xr_jni_post_pick(int action, int arg);
void xr_jni_post_pointer(int phase, float u, float v, int button);

#endif
