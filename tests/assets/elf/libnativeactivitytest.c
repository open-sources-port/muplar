#include <android/log.h>
#include <android/native_activity.h>
#include <stdint.h>

typedef struct {
    int magic;
} AppState;

static void on_start(ANativeActivity *activity) {
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest", "onStart");
}

static void on_resume(ANativeActivity *activity) {
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest", "onResume");
}

static void on_pause(ANativeActivity *activity) {
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest", "onPause");
}

static void on_stop(ANativeActivity *activity) {
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest", "onStop");
}

static void on_destroy(ANativeActivity *activity) {
    AppState *state = (AppState *)activity->instance;

    if (state && state->magic == 0x12345678) {
        __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest", "onDestroy state OK");
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "libnativeactivitytest", "onDestroy state BAD");
    }
}

void ANativeActivity_onCreate(ANativeActivity *activity, void *saved_state, size_t saved_state_size) {
    (void)saved_state;
    (void)saved_state_size;

    static AppState state;

    if (!activity || !activity->callbacks || !activity->vm || !activity->env) {
        __android_log_print(ANDROID_LOG_ERROR, "libnativeactivitytest", "invalid NativeActivity bootstrap");
        return;
    }

    state.magic = 0x12345678;
    activity->instance = &state;

    activity->callbacks->onStart = on_start;
    activity->callbacks->onResume = on_resume;
    activity->callbacks->onPause = on_pause;
    activity->callbacks->onStop = on_stop;
    activity->callbacks->onDestroy = on_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest", "ANativeActivity_onCreate ok sdk=%d data=%s", activity->sdkVersion, activity->internalDataPath ? activity->internalDataPath : "");
}
