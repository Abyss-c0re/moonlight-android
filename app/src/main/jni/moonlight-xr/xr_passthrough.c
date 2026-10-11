#include "xr_internal.h"

#include <android/log.h>
#include <string.h>

/* Quest keeps the opaque blend and draws a passthrough underlay. Pico also
 * switches the environment blend to alpha when the runtime lists that mode. */

static PFN_xrCreatePassthroughFB pfn_create;
static PFN_xrDestroyPassthroughFB pfn_destroy;
static PFN_xrPassthroughStartFB pfn_start;
static PFN_xrPassthroughPauseFB pfn_pause;
static PFN_xrCreatePassthroughLayerFB pfn_create_layer;
static PFN_xrDestroyPassthroughLayerFB pfn_destroy_layer;
static PFN_xrPassthroughLayerPauseFB pfn_layer_pause;
static PFN_xrPassthroughLayerResumeFB pfn_layer_resume;

static XrPassthroughFB passthrough = XR_NULL_HANDLE;
static XrPassthroughLayerFB passthrough_layer = XR_NULL_HANDLE;
static XrCompositionLayerPassthroughFB passthrough_composition;
static int fb_ready;
static int use_alpha;

static int xr_has_token(const char* hay, const char* needle) {
    size_t needle_len = strlen(needle);
    if (hay == NULL || needle_len == 0) {
        return 0;
    }
    for (size_t i = 0; hay[i] != '\0'; i++) {
        size_t j = 0;
        while (j < needle_len && hay[i + j] != '\0') {
            char left = hay[i + j];
            char right = needle[j];
            if (left >= 'A' && left <= 'Z') {
                left = (char)(left - 'A' + 'a');
            }
            if (right >= 'A' && right <= 'Z') {
                right = (char)(right - 'A' + 'a');
            }
            if (left != right) {
                break;
            }
            j++;
        }
        if (j == needle_len) {
            return 1;
        }
    }
    return 0;
}

static int xr_load_pfn(const char* name, PFN_xrVoidFunction* out) {
    *out = NULL;
    if (XR_FAILED(xrGetInstanceProcAddr(g_xr.instance, name, out)) || *out == NULL) {
        __android_log_print(ANDROID_LOG_WARN, "moonlight-xr", "missing %s", name);
        return 0;
    }
    return 1;
}

static void xr_destroy_fb(void) {
    if (passthrough_layer != XR_NULL_HANDLE && pfn_destroy_layer != NULL) {
        pfn_destroy_layer(passthrough_layer);
    }
    passthrough_layer = XR_NULL_HANDLE;
    if (passthrough != XR_NULL_HANDLE && pfn_destroy != NULL) {
        pfn_destroy(passthrough);
    }
    passthrough = XR_NULL_HANDLE;
    fb_ready = 0;
}

static int xr_create_fb(void) {
    if (!xr_load_pfn("xrCreatePassthroughFB", (PFN_xrVoidFunction*)&pfn_create) ||
        !xr_load_pfn("xrDestroyPassthroughFB", (PFN_xrVoidFunction*)&pfn_destroy) ||
        !xr_load_pfn("xrPassthroughStartFB", (PFN_xrVoidFunction*)&pfn_start) ||
        !xr_load_pfn("xrPassthroughPauseFB", (PFN_xrVoidFunction*)&pfn_pause) ||
        !xr_load_pfn("xrCreatePassthroughLayerFB", (PFN_xrVoidFunction*)&pfn_create_layer) ||
        !xr_load_pfn("xrDestroyPassthroughLayerFB", (PFN_xrVoidFunction*)&pfn_destroy_layer) ||
        !xr_load_pfn("xrPassthroughLayerPauseFB", (PFN_xrVoidFunction*)&pfn_layer_pause) ||
        !xr_load_pfn("xrPassthroughLayerResumeFB", (PFN_xrVoidFunction*)&pfn_layer_resume)) {
        return 0;
    }

    XrPassthroughCreateInfoFB create_info;
    memset(&create_info, 0, sizeof(create_info));
    create_info.type = XR_TYPE_PASSTHROUGH_CREATE_INFO_FB;
    create_info.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
    if (XR_FAILED(pfn_create(g_xr.session, &create_info, &passthrough))) {
        __android_log_print(ANDROID_LOG_WARN, "moonlight-xr", "xrCreatePassthroughFB failed");
        passthrough = XR_NULL_HANDLE;
        return 0;
    }

    XrPassthroughLayerCreateInfoFB layer_info;
    memset(&layer_info, 0, sizeof(layer_info));
    layer_info.type = XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB;
    layer_info.passthrough = passthrough;
    layer_info.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
    layer_info.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
    if (XR_FAILED(pfn_create_layer(g_xr.session, &layer_info, &passthrough_layer))) {
        __android_log_print(ANDROID_LOG_WARN, "moonlight-xr", "xrCreatePassthroughLayerFB failed");
        xr_destroy_fb();
        return 0;
    }
    if (XR_FAILED(pfn_layer_pause(passthrough_layer)) || XR_FAILED(pfn_pause(passthrough))) {
        __android_log_print(ANDROID_LOG_WARN, "moonlight-xr", "passthrough pause failed");
        xr_destroy_fb();
        return 0;
    }
    return 1;
}

static int xr_set_fb_running(int running) {
    if (!fb_ready) {
        return 1;
    }
    if (running) {
        return XR_SUCCEEDED(pfn_start(passthrough)) && XR_SUCCEEDED(pfn_layer_resume(passthrough_layer));
    }
    return XR_SUCCEEDED(pfn_layer_pause(passthrough_layer)) && XR_SUCCEEDED(pfn_pause(passthrough));
}

void xr_passthrough_init(void) {
    g_xr.passthrough_on = 0;
    fb_ready = 0;
    use_alpha = 0;
    int pico = xr_has_token(g_xr.system_name, "pico") || xr_has_token(g_xr.system_name, "bytedance");
    int quest = !pico && (xr_has_token(g_xr.system_name, "quest") || xr_has_token(g_xr.system_name, "oculus") ||
                          xr_has_token(g_xr.system_name, "meta"));
    if (g_xr.ext_fb_passthrough && g_xr.fb_supports_passthrough) {
        fb_ready = xr_create_fb();
    }
    if (quest) {
        use_alpha = 0;
    } else if (pico) {
        use_alpha = g_xr.alpha_blend;
    } else {
        use_alpha = g_xr.alpha_blend && !fb_ready;
    }
    g_xr.passthrough_supported = fb_ready || use_alpha;
    __android_log_print(ANDROID_LOG_INFO, "moonlight-xr", "passthrough fb=%d alpha=%d supported=%d system=%s",
                        fb_ready, use_alpha, g_xr.passthrough_supported, g_xr.system_name);
}

void xr_passthrough_destroy(void) {
    xr_destroy_fb();
    use_alpha = 0;
    g_xr.passthrough_on = 0;
    pfn_create = NULL;
    pfn_destroy = NULL;
    pfn_start = NULL;
    pfn_pause = NULL;
    pfn_create_layer = NULL;
    pfn_destroy_layer = NULL;
    pfn_layer_pause = NULL;
    pfn_layer_resume = NULL;
}

int xr_passthrough_toggle(void) {
    if (!g_xr.passthrough_supported) {
        return -1;
    }
    int next = !g_xr.passthrough_on;
    if (!xr_set_fb_running(next)) {
        xr_set_fb_running(0);
        g_xr.passthrough_on = 0;
        return 0;
    }
    g_xr.passthrough_on = next;
    return g_xr.passthrough_on ? 1 : 0;
}

XrEnvironmentBlendMode xr_passthrough_blend_mode(void) {
    if (g_xr.passthrough_on && use_alpha) {
        return XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
    }
    return g_xr.blend_mode;
}

XrCompositionLayerFlags xr_passthrough_layer_flags(void) {
    if (g_xr.passthrough_on) {
        return XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    }
    return 0;
}

int xr_passthrough_push_underlay(const XrCompositionLayerBaseHeader** layers, int count) {
    if (!g_xr.passthrough_on || !fb_ready || passthrough_layer == XR_NULL_HANDLE || count < 0 || count >= 4) {
        return count;
    }
    memset(&passthrough_composition, 0, sizeof(passthrough_composition));
    passthrough_composition.type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB;
    passthrough_composition.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    passthrough_composition.space = XR_NULL_HANDLE;
    passthrough_composition.layerHandle = passthrough_layer;
    layers[count] = (const XrCompositionLayerBaseHeader*)&passthrough_composition;
    return count + 1;
}
