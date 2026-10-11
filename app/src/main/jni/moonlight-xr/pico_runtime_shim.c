/* Pico's active runtime manifest dlopens the bare soname libruntime.pxr.so.
 * That file is not installed. The Khronos Android loader also ignores
 * XR_RUNTIME_JSON, so the manifest name has to resolve in the app namespace.
 * libopenxr_forwardloader.so is the public runtime entry and loads
 * com.pico.xr.openxr_runtime. Quest resolves its runtime through the broker
 * and does not dlopen this soname.
 */
#include <android/log.h>
#include <dlfcn.h>
#include <stdint.h>

typedef int32_t (*xr_init_loader_fn)(const void* info);
typedef int32_t (*xr_negotiate_fn)(const void* loader_info, void* runtime_request, void* runtime_interface);

/* XR_ERROR_RUNTIME_UNAVAILABLE. Kept numeric so this file does not include OpenXR. */
enum { XR_SHIM_RUNTIME_UNAVAILABLE = -51 };

static void* open_forward(void) {
    static void* handle;
    static int tried;
    if (tried) {
        return handle;
    }
    tried = 1;
    dlerror();
    handle = dlopen("libopenxr_forwardloader.so", RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr",
                            "libruntime.pxr.so could not open libopenxr_forwardloader.so: %s", dlerror());
        return NULL;
    }
    __android_log_print(ANDROID_LOG_INFO, "moonlight-xr",
                        "libruntime.pxr.so forwarding to libopenxr_forwardloader.so");
    return handle;
}

__attribute__((visibility("default"))) int32_t xrInitializeLoaderKHR(const void* info) {
    void* lib = open_forward();
    xr_init_loader_fn fn;
    if (lib == NULL) {
        return XR_SHIM_RUNTIME_UNAVAILABLE;
    }
    fn = (xr_init_loader_fn)dlsym(lib, "xrInitializeLoaderKHR");
    if (fn == NULL) {
        return XR_SHIM_RUNTIME_UNAVAILABLE;
    }
    return fn(info);
}

__attribute__((visibility("default"))) int32_t xrNegotiateLoaderRuntimeInterface(const void* loader_info,
                                                                                void* runtime_request,
                                                                                void* runtime_interface) {
    void* lib = open_forward();
    xr_negotiate_fn fn;
    if (lib == NULL) {
        return XR_SHIM_RUNTIME_UNAVAILABLE;
    }
    fn = (xr_negotiate_fn)dlsym(lib, "xrNegotiateLoaderRuntimeInterface");
    if (fn == NULL) {
        return XR_SHIM_RUNTIME_UNAVAILABLE;
    }
    return fn(loader_info, runtime_request, runtime_interface);
}
