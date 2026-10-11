#include "xr_internal.h"

#include <android/log.h>
#include <string.h>

static JNIEnv* xr_env(void) {
    JNIEnv* env = NULL;
    if (g_xr.vm == NULL) {
        return NULL;
    }
    if ((*g_xr.vm)->AttachCurrentThread(g_xr.vm, &env, NULL) != JNI_OK) {
        return NULL;
    }
    return env;
}

void xr_jni_post_pick(int action, int arg) {
    JNIEnv* env = xr_env();
    if (env == NULL || g_xr.activity == NULL) {
        return;
    }
    if (g_xr.on_library_pick == NULL) {
        jclass cls = (*env)->GetObjectClass(env, g_xr.activity);
        g_xr.on_library_pick = (*env)->GetMethodID(env, cls, "onLibraryPick", "(II)V");
        if (g_xr.on_library_pick == NULL) {
            (*env)->ExceptionClear(env);
            return;
        }
    }
    (*env)->CallVoidMethod(env, g_xr.activity, g_xr.on_library_pick, (jint)action, (jint)arg);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

void xr_jni_post_pointer(int phase, float u, float v, int button) {
    static int pointer_missing = 0;
    JNIEnv* env = xr_env();
    if (pointer_missing || env == NULL || g_xr.activity == NULL) {
        return;
    }
    if (g_xr.on_pointer == NULL) {
        jclass cls = (*env)->GetObjectClass(env, g_xr.activity);
        g_xr.on_pointer = (*env)->GetMethodID(env, cls, "onPointer", "(IFFI)V");
        if (g_xr.on_pointer == NULL) {
            (*env)->ExceptionClear(env);
            pointer_missing = 1;
            return;
        }
    }
    (*env)->CallVoidMethod(env, g_xr.activity, g_xr.on_pointer, (jint)phase, (jfloat)u, (jfloat)v, (jint)button);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

static void xr_copy_label(char* dest, JNIEnv* env, jstring text) {
    dest[0] = '\0';
    if (text == NULL) {
        return;
    }
    const char* utf = (*env)->GetStringUTFChars(env, text, NULL);
    if (utf == NULL) {
        return;
    }
    strncpy(dest, utf, XR_ROW_CHARS - 1);
    dest[XR_ROW_CHARS - 1] = '\0';
    (*env)->ReleaseStringUTFChars(env, text, utf);
}

JNIEXPORT void JNICALL
Java_com_limelight_vr_MoonlightXr_nativeSetLibrary(JNIEnv* env, jclass clazz, jobjectArray labels, jintArray kinds,
                                                   jintArray ids) {
    (void)clazz;
    if (!g_xr.lib_mu_ready) {
        return;
    }
    pthread_mutex_lock(&g_xr.lib_mu);
    g_xr.row_count = 0;
    if (labels != NULL && kinds != NULL && ids != NULL) {
        jsize count = (*env)->GetArrayLength(env, labels);
        jsize kind_count = (*env)->GetArrayLength(env, kinds);
        jsize id_count = (*env)->GetArrayLength(env, ids);
        if (count > kind_count) {
            count = kind_count;
        }
        if (count > id_count) {
            count = id_count;
        }
        if (count > XR_MAX_ROWS) {
            count = XR_MAX_ROWS;
        }
        jint* kind_values = (*env)->GetIntArrayElements(env, kinds, NULL);
        jint* id_values = (*env)->GetIntArrayElements(env, ids, NULL);
        if (kind_values != NULL && id_values != NULL) {
            for (jsize i = 0; i < count; i++) {
                jstring label = (jstring)(*env)->GetObjectArrayElement(env, labels, i);
                XrRow* row = &g_xr.rows[g_xr.row_count++];
                memset(row, 0, sizeof(*row));
                xr_copy_label(row->label, env, label);
                row->kind = kind_values[i];
                row->id = id_values[i];
                if (label != NULL) {
                    (*env)->DeleteLocalRef(env, label);
                }
            }
        }
        if (kind_values != NULL) {
            (*env)->ReleaseIntArrayElements(env, kinds, kind_values, JNI_ABORT);
        }
        if (id_values != NULL) {
            (*env)->ReleaseIntArrayElements(env, ids, id_values, JNI_ABORT);
        }
    }
    g_xr.menu_dirty = 1;
    pthread_mutex_unlock(&g_xr.lib_mu);
}

JNIEXPORT void JNICALL
Java_com_limelight_vr_MoonlightXr_nativeSetStatus(JNIEnv* env, jclass clazz, jstring status) {
    (void)clazz;
    if (!g_xr.lib_mu_ready) {
        return;
    }
    const char* utf = status != NULL ? (*env)->GetStringUTFChars(env, status, NULL) : NULL;
    pthread_mutex_lock(&g_xr.lib_mu);
    g_xr.status[0] = '\0';
    if (utf != NULL) {
        strncpy(g_xr.status, utf, sizeof(g_xr.status) - 1);
        g_xr.status[sizeof(g_xr.status) - 1] = '\0';
    }
    g_xr.menu_dirty = 1;
    pthread_mutex_unlock(&g_xr.lib_mu);
    if (utf != NULL) {
        (*env)->ReleaseStringUTFChars(env, status, utf);
    }
}

static jmethodID video_texture_method;
static jmethodID video_frame_method;
static jmethodID video_release_method;
static jfloatArray video_matrix_array;
static int video_methods_ready;

static jmethodID xr_activity_method(JNIEnv* env, const char* name, const char* sig) {
    jclass cls = (*env)->GetObjectClass(env, g_xr.activity);
    if (cls == NULL) {
        return NULL;
    }
    jmethodID method = (*env)->GetMethodID(env, cls, name, sig);
    if (method == NULL) {
        (*env)->ExceptionClear(env);
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "VrActivity.%s%s is missing", name, sig);
    }
    (*env)->DeleteLocalRef(env, cls);
    return method;
}

static int xr_video_methods(JNIEnv* env) {
    if (video_methods_ready) {
        return video_frame_method != NULL && video_texture_method != NULL && video_release_method != NULL;
    }
    if (g_xr.activity == NULL) {
        return 0;
    }
    video_methods_ready = 1;
    video_texture_method = xr_activity_method(env, "onVideoTexture", "(I)V");
    video_frame_method = xr_activity_method(env, "onVideoFrame", "([F)I");
    video_release_method = xr_activity_method(env, "onVideoRelease", "()V");
    return video_frame_method != NULL && video_texture_method != NULL && video_release_method != NULL;
}

void xr_jni_bind_video_texture(int tex_id) {
    JNIEnv* env = xr_env();
    if (env == NULL || !xr_video_methods(env)) {
        return;
    }
    (*env)->CallVoidMethod(env, g_xr.activity, video_texture_method, (jint)tex_id);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

int xr_jni_pull_video_frame(float matrix[16]) {
    JNIEnv* env = xr_env();
    if (matrix == NULL || env == NULL || !xr_video_methods(env)) {
        return -1;
    }
    if (video_matrix_array == NULL) {
        jfloatArray local = (*env)->NewFloatArray(env, 16);
        if (local == NULL) {
            (*env)->ExceptionClear(env);
            return 0;
        }
        video_matrix_array = (*env)->NewGlobalRef(env, local);
        (*env)->DeleteLocalRef(env, local);
        if (video_matrix_array == NULL) {
            return 0;
        }
    }
    jint state = (*env)->CallIntMethod(env, g_xr.activity, video_frame_method, video_matrix_array);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    if (state > 0) {
        (*env)->GetFloatArrayRegion(env, video_matrix_array, 0, 16, matrix);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            return 0;
        }
    }
    return (int)state;
}

void xr_jni_release_video(void) {
    JNIEnv* env = xr_env();
    if (env == NULL || g_xr.activity == NULL || !xr_video_methods(env)) {
        return;
    }
    (*env)->CallVoidMethod(env, g_xr.activity, video_release_method);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

JNIEXPORT void JNICALL
Java_com_limelight_vr_MoonlightXr_nativeClearVideo(JNIEnv* env, jclass clazz) {
    (void)env;
    (void)clazz;
    __atomic_add_fetch(&g_xr.video_epoch, 1, __ATOMIC_RELEASE);
}
