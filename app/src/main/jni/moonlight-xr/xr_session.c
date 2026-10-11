#include "xr_internal.h"

#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

XrApp g_xr;

static void xr_log_result(XrResult result, const char* what) {
    char text[XR_MAX_RESULT_STRING_SIZE];
    text[0] = '\0';
    if (g_xr.instance != XR_NULL_HANDLE) {
        xrResultToString(g_xr.instance, result, text);
    }
    __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "%s failed (%d) %s", what, (int)result, text);
}

static int xr_check(XrResult result, const char* what) {
    if (XR_SUCCEEDED(result)) {
        return 1;
    }
    xr_log_result(result, what);
    return 0;
}

static void xr_signal_init(int ok) {
    pthread_mutex_lock(&g_xr.init_mu);
    g_xr.init_state = ok ? 1 : -1;
    pthread_cond_signal(&g_xr.init_cv);
    pthread_mutex_unlock(&g_xr.init_mu);
}

static void xr_post_ended(const char* reason) {
    if (g_xr.vm == NULL || g_xr.activity == NULL || g_xr.on_session_ended == NULL) {
        return;
    }
    JNIEnv* env = NULL;
    if ((*g_xr.vm)->AttachCurrentThread(g_xr.vm, &env, NULL) != JNI_OK || env == NULL) {
        return;
    }
    jstring text = (*env)->NewStringUTF(env, reason != NULL ? reason : "");
    (*env)->CallVoidMethod(env, g_xr.activity, g_xr.on_session_ended, text);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, text);
}

static void xr_teardown(void) {
    xr_world_destroy();

    for (uint32_t eye = 0; eye < XR_MAX_VIEWS; eye++) {
        if (g_xr.fbo[eye] != 0) {
            glDeleteFramebuffers(1, &g_xr.fbo[eye]);
            g_xr.fbo[eye] = 0;
        }
        if (g_xr.depth[eye] != 0) {
            glDeleteRenderbuffers(1, &g_xr.depth[eye]);
            g_xr.depth[eye] = 0;
        }
        free(g_xr.images[eye]);
        g_xr.images[eye] = NULL;
        if (g_xr.swapchain[eye] != XR_NULL_HANDLE) {
            xrDestroySwapchain(g_xr.swapchain[eye]);
            g_xr.swapchain[eye] = XR_NULL_HANDLE;
        }
    }
    if (g_xr.view_space != XR_NULL_HANDLE) {
        xrDestroySpace(g_xr.view_space);
        g_xr.view_space = XR_NULL_HANDLE;
    }
    if (g_xr.ref_space != XR_NULL_HANDLE) {
        xrDestroySpace(g_xr.ref_space);
        g_xr.ref_space = XR_NULL_HANDLE;
    }
    if (g_xr.session != XR_NULL_HANDLE) {
        xrDestroySession(g_xr.session);
        g_xr.session = XR_NULL_HANDLE;
    }
    if (g_xr.instance != XR_NULL_HANDLE) {
        xrDestroyInstance(g_xr.instance);
        g_xr.instance = XR_NULL_HANDLE;
    }
    if (g_xr.dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_xr.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_xr.surf != EGL_NO_SURFACE) {
            eglDestroySurface(g_xr.dpy, g_xr.surf);
            g_xr.surf = EGL_NO_SURFACE;
        }
        if (g_xr.ctx != EGL_NO_CONTEXT) {
            eglDestroyContext(g_xr.dpy, g_xr.ctx);
            g_xr.ctx = EGL_NO_CONTEXT;
        }
        eglTerminate(g_xr.dpy);
        g_xr.dpy = EGL_NO_DISPLAY;
    }
    g_xr.session_running = 0;
}

static int xr_extension_present(const XrExtensionProperties* list, uint32_t count, const char* name) {
    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(list[i].extensionName, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int xr_init_loader(void) {
    PFN_xrInitializeLoaderKHR initialize = NULL;
    XrResult result = xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                            (PFN_xrVoidFunction*)&initialize);
    if (XR_FAILED(result) || initialize == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "xrInitializeLoaderKHR is missing");
        return 0;
    }

    JNIEnv* env = NULL;
    if ((*g_xr.vm)->AttachCurrentThread(g_xr.vm, &env, NULL) != JNI_OK) {
        return 0;
    }

    XrLoaderInitInfoAndroidKHR info;
    memset(&info, 0, sizeof(info));
    info.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
    info.applicationVM = g_xr.vm;
    info.applicationContext = g_xr.activity;
    return xr_check(initialize((const XrLoaderInitInfoBaseHeaderKHR*)&info), "xrInitializeLoaderKHR");
}

static int xr_create_instance(void) {
    uint32_t count = 0;
    XrResult result = xrEnumerateInstanceExtensionProperties(NULL, 0, &count, NULL);
    if (!xr_check(result, "xrEnumerateInstanceExtensionProperties")) {
        return 0;
    }
    XrExtensionProperties* props = (XrExtensionProperties*)calloc(count > 0 ? count : 1, sizeof(XrExtensionProperties));
    if (props == NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    }
    if (count > 0) {
        result = xrEnumerateInstanceExtensionProperties(NULL, count, &count, props);
        if (!xr_check(result, "xrEnumerateInstanceExtensionProperties list")) {
            free(props);
            return 0;
        }
    }

    if (!xr_extension_present(props, count, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME) ||
        !xr_extension_present(props, count, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME)) {
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr",
                            "Runtime is missing Android or OpenGL ES instance extensions");
        free(props);
        return 0;
    }

    g_xr.ext_fb_passthrough = xr_extension_present(props, count, "XR_FB_passthrough");
    g_xr.ext_pico_passthrough = xr_extension_present(props, count, "XR_PICO_passthrough");
    g_xr.ext_cylinder = xr_extension_present(props, count, XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
    g_xr.ext_local_floor = xr_extension_present(props, count, XR_EXT_LOCAL_FLOOR_EXTENSION_NAME);
    if (g_xr.ext_pico_passthrough) {
        __android_log_print(ANDROID_LOG_INFO, "moonlight-xr",
                            "Runtime lists XR_PICO_passthrough; passthrough uses XR_FB_passthrough or alpha blend");
    }

    const char* enabled[8];
    uint32_t enabled_count = 0;
    enabled[enabled_count++] = XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME;
    enabled[enabled_count++] = XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME;
    if (g_xr.ext_fb_passthrough) {
        enabled[enabled_count++] = "XR_FB_passthrough";
    }
    if (g_xr.ext_cylinder) {
        enabled[enabled_count++] = XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME;
    }
    if (g_xr.ext_local_floor) {
        enabled[enabled_count++] = XR_EXT_LOCAL_FLOOR_EXTENSION_NAME;
    }
    free(props);

    XrInstanceCreateInfoAndroidKHR android_info;
    memset(&android_info, 0, sizeof(android_info));
    android_info.type = XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR;
    android_info.applicationVM = g_xr.vm;
    android_info.applicationActivity = g_xr.activity;

    XrInstanceCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = XR_TYPE_INSTANCE_CREATE_INFO;
    info.next = &android_info;
    strncpy(info.applicationInfo.applicationName, "Moonlight", XR_MAX_APPLICATION_NAME_SIZE - 1);
    info.applicationInfo.applicationVersion = 1;
    strncpy(info.applicationInfo.engineName, "moonlight-xr", XR_MAX_ENGINE_NAME_SIZE - 1);
    info.applicationInfo.engineVersion = 1;
    info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    info.enabledExtensionCount = enabled_count;
    info.enabledExtensionNames = enabled;
    return xr_check(xrCreateInstance(&info, &g_xr.instance), "xrCreateInstance");
}

static int xr_create_system(void) {
    XrSystemGetInfo info;
    memset(&info, 0, sizeof(info));
    info.type = XR_TYPE_SYSTEM_GET_INFO;
    info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!xr_check(xrGetSystem(g_xr.instance, &info, &g_xr.system_id), "xrGetSystem")) {
        return 0;
    }

    XrSystemProperties properties;
    memset(&properties, 0, sizeof(properties));
    properties.type = XR_TYPE_SYSTEM_PROPERTIES;
    if (xr_check(xrGetSystemProperties(g_xr.instance, g_xr.system_id, &properties), "xrGetSystemProperties")) {
        strncpy(g_xr.system_name, properties.systemName, sizeof(g_xr.system_name) - 1);
        __android_log_print(ANDROID_LOG_INFO, "moonlight-xr", "OpenXR system %s vendor %u",
                            g_xr.system_name, (unsigned)properties.vendorId);
    }
    return 1;
}

static int xr_select_blend_mode(void) {
    uint32_t count = 0;
    XrResult result = xrEnumerateEnvironmentBlendModes(
            g_xr.instance, g_xr.system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, NULL);
    if (!xr_check(result, "xrEnumerateEnvironmentBlendModes") || count == 0) {
        return 0;
    }
    XrEnvironmentBlendMode* modes = (XrEnvironmentBlendMode*)calloc(count, sizeof(XrEnvironmentBlendMode));
    if (modes == NULL) {
        return 0;
    }
    result = xrEnumerateEnvironmentBlendModes(
            g_xr.instance, g_xr.system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, modes);
    if (!xr_check(result, "xrEnumerateEnvironmentBlendModes list")) {
        free(modes);
        return 0;
    }
    g_xr.blend_mode = modes[0];
    for (uint32_t i = 0; i < count; i++) {
        if (modes[i] == XR_ENVIRONMENT_BLEND_MODE_OPAQUE) {
            g_xr.blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        }
        if (modes[i] == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) {
            g_xr.alpha_blend = 1;
        }
    }
    free(modes);
    return 1;
}

static int xr_create_egl(const XrVersion min_version) {
    g_xr.dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_xr.dpy == EGL_NO_DISPLAY || !eglInitialize(g_xr.dpy, NULL, NULL)) {
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "eglInitialize failed");
        g_xr.dpy = EGL_NO_DISPLAY;
        return 0;
    }

    const EGLint config_attr[] = {
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 0,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_NONE};
    EGLConfig configs[16];
    EGLint config_count = 0;
    if (!eglChooseConfig(g_xr.dpy, config_attr, configs, 16, &config_count) || config_count < 1) {
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "eglChooseConfig failed");
        return 0;
    }
    g_xr.config = configs[0];

    const EGLint minors[] = {2, 1, 0};
    for (int i = 0; i < 3; i++) {
        const EGLint ctx_attr[] = {
                EGL_CONTEXT_MAJOR_VERSION, 3,
                EGL_CONTEXT_MINOR_VERSION, minors[i],
                EGL_NONE};
        g_xr.ctx = eglCreateContext(g_xr.dpy, g_xr.config, EGL_NO_CONTEXT, ctx_attr);
        if (g_xr.ctx != EGL_NO_CONTEXT) {
            XrVersion created = XR_MAKE_VERSION(3, minors[i], 0);
            if (created < min_version) {
                eglDestroyContext(g_xr.dpy, g_xr.ctx);
                g_xr.ctx = EGL_NO_CONTEXT;
                continue;
            }
            break;
        }
    }
    if (g_xr.ctx == EGL_NO_CONTEXT) {
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "No usable OpenGL ES 3 context");
        return 0;
    }

    const EGLint pbuffer_attr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    g_xr.surf = eglCreatePbufferSurface(g_xr.dpy, g_xr.config, pbuffer_attr);
    if (g_xr.surf == EGL_NO_SURFACE || !eglMakeCurrent(g_xr.dpy, g_xr.surf, g_xr.surf, g_xr.ctx)) {
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "EGL pbuffer makeCurrent failed");
        return 0;
    }
    return 1;
}

static int xr_create_session(void) {
    PFN_xrGetOpenGLESGraphicsRequirementsKHR requirements_fn = NULL;
    if (!xr_check(xrGetInstanceProcAddr(g_xr.instance, "xrGetOpenGLESGraphicsRequirementsKHR",
                                        (PFN_xrVoidFunction*)&requirements_fn),
                  "xrGetOpenGLESGraphicsRequirementsKHR addr") ||
        requirements_fn == NULL) {
        return 0;
    }
    XrGraphicsRequirementsOpenGLESKHR requirements;
    memset(&requirements, 0, sizeof(requirements));
    requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR;
    if (!xr_check(requirements_fn(g_xr.instance, g_xr.system_id, &requirements),
                  "xrGetOpenGLESGraphicsRequirementsKHR")) {
        return 0;
    }
    if (!xr_create_egl(requirements.minApiVersionSupported)) {
        return 0;
    }

    XrGraphicsBindingOpenGLESAndroidKHR binding;
    memset(&binding, 0, sizeof(binding));
    binding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR;
    binding.display = g_xr.dpy;
    binding.config = g_xr.config;
    binding.context = g_xr.ctx;

    XrSessionCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = XR_TYPE_SESSION_CREATE_INFO;
    info.next = &binding;
    info.systemId = g_xr.system_id;
    if (!xr_check(xrCreateSession(g_xr.instance, &info, &g_xr.session), "xrCreateSession")) {
        return 0;
    }

    PFN_xrSetAndroidApplicationThreadKHR set_thread = NULL;
    xrGetInstanceProcAddr(g_xr.instance, "xrSetAndroidApplicationThreadKHR", (PFN_xrVoidFunction*)&set_thread);
    if (set_thread != NULL) {
        set_thread(g_xr.session, XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR, (uint32_t)gettid());
    }
    return 1;
}

static int xr_create_space(XrReferenceSpaceType type, XrSpace* out_space, int required) {
    XrReferenceSpaceCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    info.referenceSpaceType = type;
    info.poseInReferenceSpace.orientation.w = 1.0f;
    *out_space = XR_NULL_HANDLE;
    XrResult result = xrCreateReferenceSpace(g_xr.session, &info, out_space);
    if (XR_SUCCEEDED(result)) {
        return 1;
    }
    if (required) {
        xr_log_result(result, "xrCreateReferenceSpace");
    }
    *out_space = XR_NULL_HANDLE;
    return 0;
}

static int xr_create_spaces(void) {
    g_xr.space_is_stage = 0;
    if (xr_create_space(XR_REFERENCE_SPACE_TYPE_STAGE, &g_xr.ref_space, 0)) {
        g_xr.space_is_stage = 1;
    } else if (!xr_create_space(XR_REFERENCE_SPACE_TYPE_LOCAL, &g_xr.ref_space, 1)) {
        return 0;
    }
    return xr_create_space(XR_REFERENCE_SPACE_TYPE_VIEW, &g_xr.view_space, 1);
}

static int64_t xr_choose_format(void) {
    uint32_t count = 0;
    if (!xr_check(xrEnumerateSwapchainFormats(g_xr.session, 0, &count, NULL), "xrEnumerateSwapchainFormats") ||
        count == 0) {
        return 0;
    }
    int64_t* formats = (int64_t*)calloc(count, sizeof(int64_t));
    if (formats == NULL) {
        return 0;
    }
    if (!xr_check(xrEnumerateSwapchainFormats(g_xr.session, count, &count, formats),
                  "xrEnumerateSwapchainFormats list")) {
        free(formats);
        return 0;
    }
    int64_t chosen = formats[0];
    for (uint32_t i = 0; i < count; i++) {
        if (formats[i] == GL_RGBA8) {
            chosen = GL_RGBA8;
            break;
        }
    }
    if (chosen != GL_RGBA8) {
        for (uint32_t i = 0; i < count; i++) {
            if (formats[i] == GL_SRGB8_ALPHA8) {
                chosen = GL_SRGB8_ALPHA8;
                break;
            }
        }
    }
    free(formats);
    return chosen;
}

static int xr_create_swapchains(void) {
    uint32_t count = 0;
    if (!xr_check(xrEnumerateViewConfigurationViews(g_xr.instance, g_xr.system_id,
                                                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, NULL),
                  "xrEnumerateViewConfigurationViews") ||
        count == 0 || count > XR_MAX_VIEWS) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        g_xr.view_config[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    }
    if (!xr_check(xrEnumerateViewConfigurationViews(g_xr.instance, g_xr.system_id,
                                                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count,
                                                    g_xr.view_config),
                  "xrEnumerateViewConfigurationViews list")) {
        return 0;
    }
    g_xr.view_count = count;

    int64_t format = xr_choose_format();
    if (format == 0) {
        return 0;
    }

    for (uint32_t eye = 0; eye < g_xr.view_count; eye++) {
        XrViewConfigurationView* view = &g_xr.view_config[eye];
        if (view->recommendedImageRectWidth == 0 || view->recommendedImageRectHeight == 0) {
            return 0;
        }
        XrSwapchainCreateInfo info;
        memset(&info, 0, sizeof(info));
        info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        info.format = format;
        /* Sample count 1 keeps the swapchain image a plain GL_TEXTURE_2D. */
        info.sampleCount = 1;
        info.width = view->recommendedImageRectWidth;
        info.height = view->recommendedImageRectHeight;
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;
        if (!xr_check(xrCreateSwapchain(g_xr.session, &info, &g_xr.swapchain[eye]), "xrCreateSwapchain")) {
            return 0;
        }
        g_xr.swap_w[eye] = info.width;
        g_xr.swap_h[eye] = info.height;

        uint32_t image_count = 0;
        if (!xr_check(xrEnumerateSwapchainImages(g_xr.swapchain[eye], 0, &image_count, NULL),
                      "xrEnumerateSwapchainImages") ||
            image_count == 0) {
            return 0;
        }
        g_xr.images[eye] = (XrSwapchainImageOpenGLESKHR*)calloc(image_count, sizeof(XrSwapchainImageOpenGLESKHR));
        if (g_xr.images[eye] == NULL) {
            return 0;
        }
        for (uint32_t image = 0; image < image_count; image++) {
            g_xr.images[eye][image].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        }
        if (!xr_check(xrEnumerateSwapchainImages(g_xr.swapchain[eye], image_count, &image_count,
                                                 (XrSwapchainImageBaseHeader*)g_xr.images[eye]),
                      "xrEnumerateSwapchainImages list")) {
            return 0;
        }
        g_xr.image_count[eye] = image_count;

        glGenFramebuffers(1, &g_xr.fbo[eye]);
        glGenRenderbuffers(1, &g_xr.depth[eye]);
        glBindRenderbuffer(GL_RENDERBUFFER, g_xr.depth[eye]);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, (GLsizei)info.width, (GLsizei)info.height);
    }
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    return 1;
}

static void xr_handle_session_state(const XrEventDataSessionStateChanged* event) {
    if (event->session != g_xr.session) {
        return;
    }
    g_xr.session_state = event->state;
    __android_log_print(ANDROID_LOG_INFO, "moonlight-xr", "session state %d", (int)event->state);

    if (event->state == XR_SESSION_STATE_READY) {
        XrSessionBeginInfo begin;
        memset(&begin, 0, sizeof(begin));
        begin.type = XR_TYPE_SESSION_BEGIN_INFO;
        begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        if (xr_check(xrBeginSession(g_xr.session, &begin), "xrBeginSession")) {
            g_xr.session_running = 1;
        }
    } else if (event->state == XR_SESSION_STATE_STOPPING) {
        if (g_xr.session_running) {
            xrEndSession(g_xr.session);
            g_xr.session_running = 0;
        }
    } else if (event->state == XR_SESSION_STATE_EXITING || event->state == XR_SESSION_STATE_LOSS_PENDING) {
        g_xr.quit = 1;
        g_xr.exit_requested = 1;
    }
}

static void xr_poll_events(void) {
    for (;;) {
        XrEventDataBuffer event;
        memset(&event, 0, sizeof(event));
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
        XrResult result = xrPollEvent(g_xr.instance, &event);
        if (result == XR_EVENT_UNAVAILABLE) {
            return;
        }
        if (XR_FAILED(result)) {
            xr_log_result(result, "xrPollEvent");
            return;
        }
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            xr_handle_session_state((const XrEventDataSessionStateChanged*)&event);
        } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            g_xr.quit = 1;
            g_xr.exit_requested = 1;
        }
    }
}

static int xr_request_exit_if_needed(void) {
    if (!g_xr.quit || g_xr.exit_requested || g_xr.session == XR_NULL_HANDLE) {
        return 0;
    }
    XrResult result = xrRequestExitSession(g_xr.session);
    g_xr.exit_requested = 1;
    if (XR_FAILED(result)) {
        /* IDLE has nothing to request. Teardown follows. */
        return 1;
    }
    return 0;
}

static int xr_render_frame(const XrFrameState* frame_state) {
    XrView views[XR_MAX_VIEWS];
    XrCompositionLayerProjectionView projection_views[XR_MAX_VIEWS];
    memset(views, 0, sizeof(views));
    memset(projection_views, 0, sizeof(projection_views));
    for (uint32_t eye = 0; eye < g_xr.view_count; eye++) {
        views[eye].type = XR_TYPE_VIEW;
        projection_views[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
    }

    uint32_t located = 0;
    if (frame_state->shouldRender) {
        XrViewState view_state;
        memset(&view_state, 0, sizeof(view_state));
        view_state.type = XR_TYPE_VIEW_STATE;
        XrViewLocateInfo locate;
        memset(&locate, 0, sizeof(locate));
        locate.type = XR_TYPE_VIEW_LOCATE_INFO;
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = frame_state->predictedDisplayTime;
        locate.space = g_xr.ref_space;
        if (!xr_check(xrLocateViews(g_xr.session, &locate, &view_state, g_xr.view_count, &located, views),
                      "xrLocateViews")) {
            located = 0;
        }
    }

    uint32_t rendered = 0;
    for (uint32_t eye = 0; eye < located; eye++) {
        XrSwapchainImageAcquireInfo acquire;
        memset(&acquire, 0, sizeof(acquire));
        acquire.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
        uint32_t index = 0;
        if (!xr_check(xrAcquireSwapchainImage(g_xr.swapchain[eye], &acquire, &index), "xrAcquireSwapchainImage")) {
            continue;
        }
        XrSwapchainImageWaitInfo wait;
        memset(&wait, 0, sizeof(wait));
        wait.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
        wait.timeout = XR_INFINITE_DURATION;
        if (!xr_check(xrWaitSwapchainImage(g_xr.swapchain[eye], &wait), "xrWaitSwapchainImage")) {
            XrSwapchainImageReleaseInfo release;
            memset(&release, 0, sizeof(release));
            release.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
            xrReleaseSwapchainImage(g_xr.swapchain[eye], &release);
            continue;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, g_xr.fbo[eye]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               g_xr.images[eye][index].image, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_xr.depth[eye]);
        glViewport(0, 0, (GLsizei)g_xr.swap_w[eye], (GLsizei)g_xr.swap_h[eye]);
        xr_world_render(&views[eye], (int)eye, g_xr.swap_w[eye], g_xr.swap_h[eye]);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        XrSwapchainImageReleaseInfo release;
        memset(&release, 0, sizeof(release));
        release.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        xrReleaseSwapchainImage(g_xr.swapchain[eye], &release);

        projection_views[rendered].pose = views[eye].pose;
        projection_views[rendered].fov = views[eye].fov;
        projection_views[rendered].subImage.swapchain = g_xr.swapchain[eye];
        projection_views[rendered].subImage.imageRect.offset.x = 0;
        projection_views[rendered].subImage.imageRect.offset.y = 0;
        projection_views[rendered].subImage.imageRect.extent.width = (int32_t)g_xr.swap_w[eye];
        projection_views[rendered].subImage.imageRect.extent.height = (int32_t)g_xr.swap_h[eye];
        rendered++;
    }

    const XrCompositionLayerBaseHeader* layers[4];
    int layer_count = 0;
    layer_count = xr_world_push_underlays(layers, layer_count);

    XrCompositionLayerProjection projection;
    memset(&projection, 0, sizeof(projection));
    projection.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    projection.layerFlags = xr_world_layer_flags();
    projection.space = g_xr.ref_space;
    projection.viewCount = rendered;
    projection.views = projection_views;
    if (rendered > 0) {
        layers[layer_count++] = (const XrCompositionLayerBaseHeader*)&projection;
    }

    XrFrameEndInfo end;
    memset(&end, 0, sizeof(end));
    end.type = XR_TYPE_FRAME_END_INFO;
    end.displayTime = frame_state->predictedDisplayTime;
    end.environmentBlendMode = xr_world_blend_mode();
    end.layerCount = (uint32_t)layer_count;
    end.layers = layer_count > 0 ? layers : NULL;
    return xr_check(xrEndFrame(g_xr.session, &end), "xrEndFrame");
}

static void xr_frame(void) {
    XrFrameWaitInfo wait;
    memset(&wait, 0, sizeof(wait));
    wait.type = XR_TYPE_FRAME_WAIT_INFO;
    XrFrameState state;
    memset(&state, 0, sizeof(state));
    state.type = XR_TYPE_FRAME_STATE;
    if (!xr_check(xrWaitFrame(g_xr.session, &wait, &state), "xrWaitFrame")) {
        return;
    }

    xr_world_update(state.predictedDisplayTime, state.predictedDisplayPeriod);

    XrFrameBeginInfo begin;
    memset(&begin, 0, sizeof(begin));
    begin.type = XR_TYPE_FRAME_BEGIN_INFO;
    if (!xr_check(xrBeginFrame(g_xr.session, &begin), "xrBeginFrame")) {
        return;
    }
    xr_render_frame(&state);
}

static void xr_sleep_idle(void) {
    struct timespec delay;
    delay.tv_sec = 0;
    delay.tv_nsec = 5 * 1000 * 1000;
    nanosleep(&delay, NULL);
}

void xr_thread_main(void) {
    pthread_setname_np(pthread_self(), "moonlight-xr");
    g_xr.dpy = EGL_NO_DISPLAY;
    g_xr.ctx = EGL_NO_CONTEXT;
    g_xr.surf = EGL_NO_SURFACE;
    g_xr.blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

    int ready = xr_init_loader() && xr_create_instance() && xr_create_system() && xr_select_blend_mode() &&
                xr_create_session() && xr_create_spaces() && xr_create_swapchains();
    if (ready) {
        xr_world_init();
    }
    xr_signal_init(ready);
    if (!ready) {
        xr_teardown();
        if (g_xr.vm != NULL) {
            (*g_xr.vm)->DetachCurrentThread(g_xr.vm);
        }
        return;
    }

    int exit_frames = 0;
    for (;;) {
        xr_poll_events();
        if (g_xr.session_state == XR_SESSION_STATE_EXITING ||
            g_xr.session_state == XR_SESSION_STATE_LOSS_PENDING) {
            break;
        }
        if (xr_request_exit_if_needed()) {
            break;
        }
        if (!g_xr.session_running) {
            if (g_xr.exit_requested) {
                break;
            }
            xr_sleep_idle();
            continue;
        }
        /* Don't join forever if the runtime never reaches STOPPING. */
        if (g_xr.exit_requested && ++exit_frames > 120) {
            break;
        }
        xr_frame();
    }

    if (g_xr.session_running && g_xr.session != XR_NULL_HANDLE) {
        xrEndSession(g_xr.session);
        g_xr.session_running = 0;
    }
    xr_post_ended("session ended");
    xr_teardown();
    if (g_xr.vm != NULL) {
        (*g_xr.vm)->DetachCurrentThread(g_xr.vm);
    }
}
