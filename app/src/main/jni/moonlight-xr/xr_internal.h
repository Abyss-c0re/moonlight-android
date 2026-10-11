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

typedef struct XrApp {
    JavaVM* vm;
    jobject activity;
    jmethodID on_session_ended;

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
} XrApp;

extern XrApp g_xr;

void xr_world_init(void);
void xr_world_destroy(void);
void xr_world_update(XrTime display_time, XrDuration period);
void xr_world_render(const XrView* view, int eye, uint32_t width, uint32_t height);
XrEnvironmentBlendMode xr_world_blend_mode(void);
XrCompositionLayerFlags xr_world_layer_flags(void);
int xr_world_push_underlays(const XrCompositionLayerBaseHeader** layers, int count);

#endif
