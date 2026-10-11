# Optional OpenXR VR mode. The Khronos loader (Apache-2.0, see third_party/openxr/LICENSE)
# is prebuilt per ABI with 16KB max page size. Nothing in the 2D app loads this module
# until VrActivity calls System.loadLibrary("moonlight-xr").

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := openxr_loader
LOCAL_SRC_FILES := third_party/openxr/loader/$(TARGET_ARCH_ABI)/libopenxr_loader.so
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/third_party/openxr/include
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := moonlight-xr
LOCAL_SRC_FILES := moonlight_xr_stub.c
LOCAL_C_INCLUDES := $(LOCAL_PATH)/third_party/openxr/include
LOCAL_SHARED_LIBRARIES := openxr_loader
LOCAL_LDLIBS := -llog
include $(BUILD_SHARED_LIBRARY)
