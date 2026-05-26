#include <android/binder_ibinder.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <stdbool.h>
#include <stdint.h>

extern AIBinder* AServiceManager_getService(const char* instance);

static int g_lifecycle_ok;
static int g_on_create_seen;
static int g_on_destroy_seen;
static int g_on_transact_seen;
static int g_remote_unlinked;
static int g_delete_unlinked;
static int g_local_error_unlinked;
static int g_died_seen;

static void on_binder_died(void* cookie)
{
    (void)cookie;
    g_died_seen++;
}

static void on_binder_unlinked(void* cookie)
{
    uintptr_t value = (uintptr_t)cookie;
    if (value == 0x5201)
        g_remote_unlinked++;
    else if (value == 0x5202)
        g_delete_unlinked++;
    else if (value == 0x5203)
        g_local_error_unlinked++;
}

static void* on_create(void* args)
{
    g_on_create_seen = args == (void*)(uintptr_t)0x52;
    return (void*)(uintptr_t)0x5200;
}

static void on_destroy(void* user_data)
{
    g_on_destroy_seen = user_data == (void*)(uintptr_t)0x5200;
}

static binder_status_t on_transact(AIBinder* binder,
                                   transaction_code_t code,
                                   const AParcel* in,
                                   AParcel* out)
{
    (void)binder;
    (void)code;
    (void)in;
    (void)out;
    g_on_transact_seen++;
    return STATUS_UNKNOWN_TRANSACTION;
}

static void on_activity_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libbinderlifecycletest",
                        "onDestroy lifecycle=%d create=%d destroy=%d died=%d",
                        g_lifecycle_ok,
                        g_on_create_seen,
                        g_on_destroy_seen,
                        g_died_seen);
}

void ANativeActivity_onCreate(ANativeActivity* activity,
                              void* saved_state,
                              size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    if (!activity || !activity->callbacks) {
        __android_log_print(ANDROID_LOG_ERROR, "libbinderlifecycletest",
                            "invalid lifecycle binder bootstrap");
        return;
    }

    AIBinder* remote = AServiceManager_getService("activity");
    AIBinder_Class* clazz =
        AIBinder_Class_define("com.example.muplar.ILifecycle",
                              on_create, on_destroy, on_transact);
    AIBinder* local = clazz ? AIBinder_new(clazz, (void*)(uintptr_t)0x52) : 0;
    AIBinder_DeathRecipient* recipient =
        AIBinder_DeathRecipient_new(on_binder_died);
    AIBinder_DeathRecipient* delete_recipient =
        AIBinder_DeathRecipient_new(on_binder_died);
    AIBinder_DeathRecipient* local_recipient =
        AIBinder_DeathRecipient_new(on_binder_died);
    AIBinder_DeathRecipient* null_recipient =
        AIBinder_DeathRecipient_new(0);

    if (recipient)
        AIBinder_DeathRecipient_setOnUnlinked(recipient, on_binder_unlinked);
    if (delete_recipient)
        AIBinder_DeathRecipient_setOnUnlinked(delete_recipient,
                                              on_binder_unlinked);
    if (local_recipient)
        AIBinder_DeathRecipient_setOnUnlinked(local_recipient,
                                              on_binder_unlinked);

    int null_ref = AIBinder_debugGetRefCount(0);
    int ping_null = AIBinder_ping(0);
    int remote_ref0 = remote ? AIBinder_debugGetRefCount(remote) : -99;
    if (remote)
        AIBinder_incStrong(remote);
    int remote_ref1 = remote ? AIBinder_debugGetRefCount(remote) : -99;
    if (remote)
        AIBinder_decStrong(remote);
    int remote_ref2 = remote ? AIBinder_debugGetRefCount(remote) : -99;

    int local_ref0 = local ? AIBinder_debugGetRefCount(local) : -99;
    if (local)
        AIBinder_incStrong(local);
    int local_ref1 = local ? AIBinder_debugGetRefCount(local) : -99;
    if (local)
        AIBinder_decStrong(local);
    int local_ref2 = local ? AIBinder_debugGetRefCount(local) : -99;

    binder_status_t link_remote =
        remote && recipient
            ? AIBinder_linkToDeath(remote, recipient, (void*)(uintptr_t)0x5201)
            : STATUS_BAD_VALUE;
    binder_status_t unlink_remote =
        remote && recipient
            ? AIBinder_unlinkToDeath(remote, recipient,
                                     (void*)(uintptr_t)0x5201)
            : STATUS_BAD_VALUE;
    binder_status_t unlink_missing =
        remote && recipient
            ? AIBinder_unlinkToDeath(remote, recipient,
                                     (void*)(uintptr_t)0x5201)
            : STATUS_BAD_VALUE;

    binder_status_t link_for_delete =
        remote && delete_recipient
            ? AIBinder_linkToDeath(remote, delete_recipient,
                                   (void*)(uintptr_t)0x5202)
            : STATUS_BAD_VALUE;
    if (delete_recipient)
        AIBinder_DeathRecipient_delete(delete_recipient);

    binder_status_t link_local =
        local && local_recipient
            ? AIBinder_linkToDeath(local, local_recipient,
                                   (void*)(uintptr_t)0x5203)
            : STATUS_BAD_VALUE;

    bool associate_remote = remote && clazz
        ? AIBinder_associateClass(remote, clazz)
        : false;
    int remote_after_associate = remote ? AIBinder_isRemote(remote) : 0;
    const AIBinder_Class* remote_class =
        remote ? AIBinder_getClass(remote) : 0;
    void* remote_user_data = remote ? AIBinder_getUserData(remote) : 0;
    int local_remote = local ? AIBinder_isRemote(local) : 1;
    const AIBinder_Class* local_class = local ? AIBinder_getClass(local) : 0;
    void* local_user_data = local ? AIBinder_getUserData(local) : 0;

    if (local)
        AIBinder_decStrong(local);
    if (recipient)
        AIBinder_DeathRecipient_delete(recipient);
    if (local_recipient)
        AIBinder_DeathRecipient_delete(local_recipient);

    g_lifecycle_ok =
        remote &&
        clazz &&
        local &&
        recipient &&
        delete_recipient &&
        local_recipient &&
        null_recipient == 0 &&
        null_ref == -1 &&
        ping_null == STATUS_BAD_VALUE &&
        remote_ref0 >= 1 &&
        remote_ref1 == remote_ref0 + 1 &&
        remote_ref2 == remote_ref0 &&
        local_ref0 == 1 &&
        local_ref1 == 2 &&
        local_ref2 == 1 &&
        link_remote == STATUS_OK &&
        unlink_remote == STATUS_OK &&
        unlink_missing == STATUS_NAME_NOT_FOUND &&
        link_for_delete == STATUS_OK &&
        link_local == STATUS_INVALID_OPERATION &&
        g_remote_unlinked == 1 &&
        g_delete_unlinked == 1 &&
        g_local_error_unlinked == 1 &&
        g_died_seen == 0 &&
        associate_remote &&
        remote_after_associate == 1 &&
        remote_class == clazz &&
        remote_user_data == 0 &&
        local_remote == 0 &&
        local_class == clazz &&
        local_user_data == (void*)(uintptr_t)0x5200 &&
        g_on_create_seen &&
        g_on_destroy_seen &&
        g_on_transact_seen == 0;

    activity->callbacks->onDestroy = on_activity_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libbinderlifecycletest",
                        "lifecycle remote=%d refs=%d/%d/%d ok=%d",
                        remote_after_associate,
                        remote_ref0,
                        remote_ref1,
                        remote_ref2,
                        g_lifecycle_ok);
    __android_log_print(ANDROID_LOG_INFO, "libbinderlifecycletest",
                        "unlink counts=%d/%d/%d",
                        g_remote_unlinked,
                        g_delete_unlinked,
                        g_local_error_unlinked);
    __android_log_print(ANDROID_LOG_INFO, "libbinderlifecycletest",
                        "status link=%d unlink=%d missing=%d local=%d nullref=%d",
                        link_remote,
                        unlink_remote,
                        unlink_missing,
                        link_local,
                        null_ref);
}
