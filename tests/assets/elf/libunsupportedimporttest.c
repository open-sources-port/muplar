#include <android/log.h>
#include <android/native_activity.h>

extern int muplar_missing_native_import(int value);

static int g_missing_import_result = -1;

static void on_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libunsupportedimporttest",
                        "onDestroy missing=%d", g_missing_import_result);
}

void ANativeActivity_onCreate(ANativeActivity* activity,
                              void* saved_state,
                              size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    activity->callbacks->onDestroy = on_destroy;
    __android_log_print(ANDROID_LOG_INFO, "libunsupportedimporttest",
                        "before missing import");

    g_missing_import_result = muplar_missing_native_import(1234);

    __android_log_print(ANDROID_LOG_INFO, "libunsupportedimporttest",
                        "after missing import result=%d",
                        g_missing_import_result);
}
