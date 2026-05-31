#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <stdbool.h>
#include <stdint.h>

extern AIBinder* AServiceManager_checkService(const char* instance);
extern AIBinder* AServiceManager_getService(const char* instance);
extern binder_status_t AServiceManager_addService(AIBinder* binder,
                                                  const char* instance);
extern binder_status_t __muplar_binder_kill(AIBinder* binder);

static int g_driver_ok;
static int g_local_create_seen;
static int g_local_destroy_seen;
static int g_died_seen;
static int g_remote_unlinked;
static int g_dead_link_unlinked;

static void on_binder_died(void* cookie)
{
    if ((uintptr_t)cookie == 0x5211)
        g_died_seen++;
}

static void on_binder_unlinked(void* cookie)
{
    uintptr_t value = (uintptr_t)cookie;
    if (value == 0x5211)
        g_remote_unlinked++;
    else if (value == 0x5212)
        g_dead_link_unlinked++;
}

static void* on_local_create(void* args)
{
    g_local_create_seen = args == (void*)(uintptr_t)0x53;
    return (void*)(uintptr_t)0x5300;
}

static void on_local_destroy(void* user_data)
{
    g_local_destroy_seen = user_data == (void*)(uintptr_t)0x5300;
}

static binder_status_t on_local_transact(AIBinder* binder,
                                         transaction_code_t code,
                                         const AParcel* in,
                                         AParcel* out)
{
    (void)binder;
    (void)code;
    (void)in;
    (void)out;
    return STATUS_UNKNOWN_TRANSACTION;
}

static void on_activity_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libbinderdrivertest",
                        "onDestroy driver=%d create=%d destroy=%d",
                        g_driver_ok,
                        g_local_create_seen,
                        g_local_destroy_seen);
}

void ANativeActivity_onCreate(ANativeActivity* activity,
                              void* saved_state,
                              size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    if (!activity || !activity->callbacks) {
        __android_log_print(ANDROID_LOG_ERROR, "libbinderdrivertest",
                            "invalid driver binder bootstrap");
        return;
    }

    AIBinder* remote = AServiceManager_getService("activity");
    AIBinder_DeathRecipient* recipient =
        AIBinder_DeathRecipient_new(on_binder_died);
    AIBinder_DeathRecipient* dead_recipient =
        AIBinder_DeathRecipient_new(on_binder_died);
    if (recipient)
        AIBinder_DeathRecipient_setOnUnlinked(recipient,
                                              on_binder_unlinked);
    if (dead_recipient)
        AIBinder_DeathRecipient_setOnUnlinked(dead_recipient,
                                              on_binder_unlinked);

    AParcel* in = 0;
    AParcel* out = 0;
    binder_status_t prepare_before =
        remote ? AIBinder_prepareTransaction(remote, &in) : STATUS_BAD_VALUE;
    binder_status_t write_before =
        in ? AParcel_writeInt32(in, 39) : STATUS_BAD_VALUE;
    binder_status_t link_before =
        remote && recipient
            ? AIBinder_linkToDeath(remote, recipient,
                                   (void*)(uintptr_t)0x5211)
            : STATUS_BAD_VALUE;
    binder_status_t kill_status =
        remote ? __muplar_binder_kill(remote) : STATUS_BAD_VALUE;
    int alive_after = remote ? AIBinder_isAlive(remote) : 1;
    int remote_after = remote ? AIBinder_isRemote(remote) : 0;
    binder_status_t ping_after =
        remote ? AIBinder_ping(remote) : STATUS_BAD_VALUE;
    binder_status_t transact_after =
        remote
            ? AIBinder_transact(remote, FIRST_CALL_TRANSACTION + 7,
                                &in, &out, 0)
            : STATUS_BAD_VALUE;
    int in_cleared_after_failure = in == 0;
    int out_zero_after_failure = out == 0;
    binder_status_t prepare_after =
        remote ? AIBinder_prepareTransaction(remote, &in) : STATUS_BAD_VALUE;
    binder_status_t link_after =
        remote && dead_recipient
            ? AIBinder_linkToDeath(remote, dead_recipient,
                                   (void*)(uintptr_t)0x5212)
            : STATUS_BAD_VALUE;

    if (in)
        AParcel_delete(in);
    if (out)
        AParcel_delete(out);
    if (recipient)
        AIBinder_DeathRecipient_delete(recipient);
    if (dead_recipient)
        AIBinder_DeathRecipient_delete(dead_recipient);

    AIBinder* restarted = AServiceManager_getService("activity");
    int restarted_ok =
        restarted &&
        restarted != remote &&
        AIBinder_isAlive(restarted) &&
        AIBinder_ping(restarted) == STATUS_OK;

    AIBinder_Class* clazz =
        AIBinder_Class_define("com.example.muplar.IDriver", on_local_create,
                              on_local_destroy, on_local_transact);
    AIBinder* local = clazz ? AIBinder_new(clazz, (void*)(uintptr_t)0x53) : 0;
    const char* local_name = "com.example.muplar.driver.local";
    binder_status_t add_local =
        local ? AServiceManager_addService(local, local_name) : STATUS_BAD_VALUE;
    AIBinder* seen_local = AServiceManager_checkService(local_name);
    int seen_same = seen_local == local;
    if (local)
        AIBinder_decStrong(local);
    AIBinder* after_cleanup = AServiceManager_checkService(local_name);
    int cleanup_empty = after_cleanup == 0;

    g_driver_ok =
        remote &&
        recipient &&
        dead_recipient &&
        prepare_before == STATUS_OK &&
        write_before == STATUS_OK &&
        link_before == STATUS_OK &&
        kill_status == STATUS_OK &&
        alive_after == 0 &&
        remote_after == 1 &&
        ping_after == STATUS_DEAD_OBJECT &&
        transact_after == STATUS_DEAD_OBJECT &&
        in_cleared_after_failure &&
        out_zero_after_failure &&
        prepare_after == STATUS_DEAD_OBJECT &&
        link_after == STATUS_DEAD_OBJECT &&
        g_died_seen == 1 &&
        g_remote_unlinked == 1 &&
        g_dead_link_unlinked == 1 &&
        restarted_ok &&
        clazz &&
        local &&
        add_local == STATUS_OK &&
        seen_same &&
        cleanup_empty &&
        g_local_create_seen &&
        g_local_destroy_seen;

    activity->callbacks->onDestroy = on_activity_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libbinderdrivertest",
                        "dead alive=%d remote=%d ping=%d trans=%d ok=%d",
                        alive_after,
                        remote_after,
                        ping_after,
                        transact_after,
                        g_driver_ok);
    __android_log_print(ANDROID_LOG_INFO, "libbinderdrivertest",
                        "death link=%d kill=%d died=%d unlinked=%d post=%d",
                        link_before,
                        kill_status,
                        g_died_seen,
                        g_remote_unlinked,
                        link_after);
    __android_log_print(ANDROID_LOG_INFO, "libbinderdrivertest",
                        "service restart=%d add=%d seen=%d cleanup=%d",
                        restarted_ok,
                        add_local,
                        seen_same,
                        cleanup_empty);
}
