#include <android/log.h>
#include <openxr/openxr.h>

/* Referenced so the linker keeps libopenxr_loader.so (ndk uses --as-needed).
   The 2D process never calls this; VrActivity is the only loader of the module. */
uint32_t moonlight_xr_loader_present(void) {
    PFN_xrVoidFunction probe = NULL;
    XrResult result = xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", &probe);
    __android_log_print(ANDROID_LOG_INFO, "moonlight-xr",
                        "OpenXR loader API %u.%u.%u probe %d",
                        (unsigned)XR_VERSION_MAJOR(XR_CURRENT_API_VERSION),
                        (unsigned)XR_VERSION_MINOR(XR_CURRENT_API_VERSION),
                        (unsigned)XR_VERSION_PATCH(XR_CURRENT_API_VERSION),
                        (int)result);
    return (uint32_t)XR_VERSION_MAJOR(XR_CURRENT_API_VERSION);
}
