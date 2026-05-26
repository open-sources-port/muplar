#include <android/binder_ibinder.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <android/native_activity.h>

extern AIBinder* AServiceManager_checkService(const char* instance);
extern AIBinder* AServiceManager_getService(const char* instance);
extern AIBinder* AServiceManager_waitForService(const char* instance);
extern bool AServiceManager_isDeclared(const char* instance);

static int g_binder_ok;

static void on_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libbindertest",
                        "onDestroy binder=%d", g_binder_ok);
}

void ANativeActivity_onCreate(ANativeActivity* activity,
                              void* saved_state,
                              size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    if (!activity || !activity->callbacks) {
        __android_log_print(ANDROID_LOG_ERROR, "libbindertest",
                            "invalid binder bootstrap");
        return;
    }

    AIBinder* activity_service = AServiceManager_getService("activity");
    AIBinder* activity_again = AServiceManager_waitForService("activity");
    AIBinder* package_service = AServiceManager_checkService("package");

    int same = activity_service && activity_service == activity_again;
    int alive = activity_service ? AIBinder_isAlive(activity_service) : 0;
    int remote = activity_service ? AIBinder_isRemote(activity_service) : 0;
    int ping = activity_service ? AIBinder_ping(activity_service) : STATUS_BAD_VALUE;
    int declared = AServiceManager_isDeclared("activity");

    if (activity_service)
        AIBinder_incStrong(activity_service);
    int ref_count = activity_service
        ? AIBinder_debugGetRefCount(activity_service)
        : 0;
    if (activity_service)
        AIBinder_decStrong(activity_service);

    g_binder_ok =
        activity_service &&
        package_service &&
        same &&
        alive &&
        remote &&
        ping == STATUS_OK &&
        declared &&
        ref_count >= 2;

    activity->callbacks->onDestroy = on_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libbindertest",
                        "services activity=%p package=%p same=%d ok=%d",
                        activity_service,
                        package_service,
                        same,
                        g_binder_ok);
    __android_log_print(ANDROID_LOG_INFO, "libbindertest",
                        "state alive=%d remote=%d ping=%d declared=%d ref=%d",
                        alive,
                        remote,
                        ping,
                        declared,
                        ref_count);
}
