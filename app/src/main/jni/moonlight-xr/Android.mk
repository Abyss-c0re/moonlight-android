# Optional OpenXR VR mode. The Khronos loader (Apache-2.0, see third_party/openxr/LICENSE)
# is prebuilt per ABI with 16KB max page size. Nothing in the 2D app loads this module
# until VrActivity calls System.loadLibrary("moonlight-xr").

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := openxr_loader
LOCAL_SRC_FILES := third_party/openxr/loader/$(TARGET_ARCH_ABI)/libopenxr_loader.so
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/third_party/openxr/include
include $(PREBUILT_SHARED_LIBRARY)

# Stand-in for the soname in Pico's active_runtime.json. moonlight-xr links it
# so ndk-build packages it; the Khronos loader dlopens that soname.
include $(CLEAR_VARS)
LOCAL_MODULE := pxr_runtime_shim
# The prefix "lib" is not added when LOCAL_MODULE_FILENAME is set.
LOCAL_MODULE_FILENAME := libruntime.pxr
LOCAL_SRC_FILES := pico_runtime_shim.c
LOCAL_CFLAGS := -std=c11 -Wall -fvisibility=hidden
LOCAL_LDFLAGS := -Wl,-z,max-page-size=16384
LOCAL_LDLIBS := -ldl -llog
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := moonlight-xr
LOCAL_SRC_FILES := xr_jni.c xr_session.c xr_world.c xr_geom.c xr_input.c xr_screens.c xr_keyboard.c xr_render.c xr_library.c xr_passthrough.c
LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/third_party/openxr/include
LOCAL_SHARED_LIBRARIES := openxr_loader pxr_runtime_shim
LOCAL_LDLIBS := -llog -lEGL -lGLESv3 -landroid -lm
LOCAL_CFLAGS := -std=c11 -Wall -Wno-unused-function
include $(BUILD_SHARED_LIBRARY)
