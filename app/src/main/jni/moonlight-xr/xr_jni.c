#include "xr_internal.h"

#include <android/log.h>

void xr_thread_main(void);

static void* xr_thread_entry(void* unused) {
    (void)unused;
    xr_thread_main();
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    g_xr.vm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT jboolean JNICALL
Java_com_limelight_vr_MoonlightXr_nativeStart(JNIEnv* env, jclass clazz, jobject activity) {
    (void)clazz;
    if (g_xr.thread_started || activity == NULL) {
        return JNI_FALSE;
    }

    g_xr.activity = (*env)->NewGlobalRef(env, activity);
    jclass activity_class = (*env)->GetObjectClass(env, activity);
    g_xr.on_session_ended = (*env)->GetMethodID(env, activity_class, "onSessionEnded", "(Ljava/lang/String;)V");
    if (g_xr.on_session_ended == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "VrActivity.onSessionEnded is missing");
        (*env)->ExceptionClear(env);
        (*env)->DeleteGlobalRef(env, g_xr.activity);
        g_xr.activity = NULL;
        return JNI_FALSE;
    }

    pthread_mutex_init(&g_xr.init_mu, NULL);
    pthread_cond_init(&g_xr.init_cv, NULL);
    g_xr.init_state = 0;
    g_xr.quit = 0;
    g_xr.exit_requested = 0;

    if (pthread_create(&g_xr.thread, NULL, xr_thread_entry, NULL) != 0) {
        (*env)->DeleteGlobalRef(env, g_xr.activity);
        g_xr.activity = NULL;
        pthread_mutex_destroy(&g_xr.init_mu);
        pthread_cond_destroy(&g_xr.init_cv);
        return JNI_FALSE;
    }
    g_xr.thread_started = 1;

    pthread_mutex_lock(&g_xr.init_mu);
    while (g_xr.init_state == 0) {
        pthread_cond_wait(&g_xr.init_cv, &g_xr.init_mu);
    }
    int ok = g_xr.init_state > 0;
    pthread_mutex_unlock(&g_xr.init_mu);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_limelight_vr_MoonlightXr_nativeRequestExit(JNIEnv* env, jclass clazz) {
    (void)env;
    (void)clazz;
    g_xr.quit = 1;
}

JNIEXPORT void JNICALL
Java_com_limelight_vr_MoonlightXr_nativeShutdown(JNIEnv* env, jclass clazz) {
    (void)clazz;
    if (!g_xr.thread_started) {
        return;
    }
    g_xr.quit = 1;
    pthread_join(g_xr.thread, NULL);
    g_xr.thread_started = 0;
    if (g_xr.activity != NULL) {
        (*env)->DeleteGlobalRef(env, g_xr.activity);
        g_xr.activity = NULL;
    }
    pthread_mutex_destroy(&g_xr.init_mu);
    pthread_cond_destroy(&g_xr.init_cv);
}
