#include <android/log.h>
#include <android/native_activity.h>

extern int muplar_apk_dep_value(void);

static int g_dep_value;

static void on_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libapkdeptest",
                        "onDestroy dep=%d", g_dep_value);
}

void ANativeActivity_onCreate(ANativeActivity* activity,
                              void* saved_state,
                              size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    g_dep_value = muplar_apk_dep_value();
    activity->callbacks->onDestroy = on_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libapkdeptest",
                        "onCreate dep=%d", g_dep_value);
}
