// libnativeactivitytest.c — minimal NativeActivity bootstrap smoke test

#include <android/log.h>
#include <android/native_activity.h>
#include <stdint.h>

void ANativeActivity_onCreate(ANativeActivity* activity,
                              void* saved_state,
                              size_t saved_state_size) {
    (void)saved_state;
    (void)saved_state_size;

    if (!activity || !activity->callbacks || !activity->vm || !activity->env) {
        __android_log_print(ANDROID_LOG_ERROR, "libnativeactivitytest",
                            "invalid NativeActivity bootstrap");
        return;
    }

    activity->instance = (void*)(uintptr_t)0x12345678;
    __android_log_print(ANDROID_LOG_INFO, "libnativeactivitytest",
                        "ANativeActivity_onCreate ok sdk=%d data=%s",
                        activity->sdkVersion,
                        activity->internalDataPath ? activity->internalDataPath : "");
}
