#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <stdbool.h>
#include <stdint.h>

static int g_weak_ok;
static int g_extension_ok;
static int g_owner_create_seen;
static int g_owner_destroy_seen;
static int g_extension_create_seen;
static int g_extension_destroy_seen;

static void* on_owner_create(void* args)
{
    g_owner_create_seen = args == (void*)(uintptr_t)0x6024;
    return (void*)(uintptr_t)0x6025;
}

static void on_owner_destroy(void* user_data)
{
    g_owner_destroy_seen = user_data == (void*)(uintptr_t)0x6025;
}

static void* on_extension_create(void* args)
{
    g_extension_create_seen = args == (void*)(uintptr_t)0x6124;
    return (void*)(uintptr_t)0x6125;
}

static void on_extension_destroy(void* user_data)
{
    g_extension_destroy_seen = user_data == (void*)(uintptr_t)0x6125;
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
    return STATUS_UNKNOWN_TRANSACTION;
}

static void on_activity_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libbinderweaktest",
                        "onDestroy weak=%d extension=%d owner=%d/%d",
                        g_weak_ok,
                        g_extension_ok,
                        g_owner_create_seen,
                        g_owner_destroy_seen);
    __android_log_print(ANDROID_LOG_INFO, "libbinderweaktest",
                        "onDestroy ext=%d/%d",
                        g_extension_create_seen,
                        g_extension_destroy_seen);
}

void ANativeActivity_onCreate(ANativeActivity* activity,
                              void* saved_state,
                              size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    if (!activity || !activity->callbacks) {
        __android_log_print(ANDROID_LOG_ERROR, "libbinderweaktest",
                            "invalid weak binder bootstrap");
        return;
    }

    AIBinder_Class* owner_class =
        AIBinder_Class_define("com.example.muplar.IWeakOwner",
                              on_owner_create,
                              on_owner_destroy,
                              on_transact);
    AIBinder_Class* extension_class =
        AIBinder_Class_define("com.example.muplar.IWeakExtension",
                              on_extension_create,
                              on_extension_destroy,
                              on_transact);
    AIBinder* owner = owner_class
        ? AIBinder_new(owner_class, (void*)(uintptr_t)0x6024)
        : 0;
    AIBinder* extension = extension_class
        ? AIBinder_new(extension_class, (void*)(uintptr_t)0x6124)
        : 0;

    int initial_ref = owner && AIBinder_debugGetRefCount(owner) == 1;
    AIBinder_Weak* weak = owner ? AIBinder_Weak_new(owner) : 0;
    AIBinder_Weak* clone = weak ? AIBinder_Weak_clone(weak) : 0;
    AIBinder* promoted = weak ? AIBinder_Weak_promote(weak) : 0;
    int promote_ok =
        promoted == owner && owner && AIBinder_debugGetRefCount(owner) == 2;
    if (promoted)
        AIBinder_decStrong(promoted);
    int promote_ref_released =
        owner && AIBinder_debugGetRefCount(owner) == 1;
    int weak_order_ok =
        weak &&
        clone &&
        !AIBinder_Weak_lt(weak, clone) &&
        !AIBinder_Weak_lt(clone, weak);
    int binder_order_ok =
        owner &&
        extension &&
        AIBinder_lt(owner, extension) != AIBinder_lt(extension, owner);

    binder_status_t set_extension = owner && extension
        ? AIBinder_setExtension(owner, extension)
        : STATUS_BAD_VALUE;
    int extension_ref_after_set =
        extension && AIBinder_debugGetRefCount(extension) == 2;
    AIBinder* got_extension = 0;
    binder_status_t get_extension = owner
        ? AIBinder_getExtension(owner, &got_extension)
        : STATUS_BAD_VALUE;
    int extension_same =
        got_extension == extension &&
        extension &&
        AIBinder_debugGetRefCount(extension) == 3;
    if (got_extension)
        AIBinder_decStrong(got_extension);
    int extension_ref_after_get_release =
        extension && AIBinder_debugGetRefCount(extension) == 2;

    AIBinder* null_extension = 0;
    binder_status_t null_get = AIBinder_getExtension(0, &null_extension);
    binder_status_t null_set = owner
        ? AIBinder_setExtension(owner, 0)
        : STATUS_BAD_VALUE;

    if (owner)
        AIBinder_decStrong(owner);
    AIBinder* promoted_after_destroy =
        weak ? AIBinder_Weak_promote(weak) : (AIBinder*)(uintptr_t)1;
    AIBinder* clone_after_destroy =
        clone ? AIBinder_Weak_promote(clone) : (AIBinder*)(uintptr_t)1;
    int weak_dead_ok =
        promoted_after_destroy == 0 && clone_after_destroy == 0;
    int extension_ref_after_owner_destroy =
        extension && AIBinder_debugGetRefCount(extension) == 1;

    if (weak)
        AIBinder_Weak_delete(weak);
    if (clone)
        AIBinder_Weak_delete(clone);
    if (extension)
        AIBinder_decStrong(extension);

    g_weak_ok =
        initial_ref &&
        weak &&
        clone &&
        promote_ok &&
        promote_ref_released &&
        weak_order_ok &&
        binder_order_ok &&
        weak_dead_ok &&
        g_owner_create_seen &&
        g_owner_destroy_seen;
    g_extension_ok =
        extension &&
        set_extension == STATUS_OK &&
        get_extension == STATUS_OK &&
        extension_ref_after_set &&
        extension_same &&
        extension_ref_after_get_release &&
        extension_ref_after_owner_destroy &&
        null_get == STATUS_UNEXPECTED_NULL &&
        null_set == STATUS_UNEXPECTED_NULL &&
        g_extension_create_seen &&
        g_extension_destroy_seen;

    activity->callbacks->onDestroy = on_activity_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libbinderweaktest",
                        "weak initial=%d promote=%d dead=%d ok=%d",
                        initial_ref,
                        promote_ok,
                        weak_dead_ok,
                        g_weak_ok);
    __android_log_print(ANDROID_LOG_INFO, "libbinderweaktest",
                        "weak ref=%d order=%d owner=%d/%d",
                        promote_ref_released,
                        weak_order_ok && binder_order_ok,
                        g_owner_create_seen,
                        g_owner_destroy_seen);
    __android_log_print(ANDROID_LOG_INFO, "libbinderweaktest",
                        "extension set=%d get=%d same=%d ok=%d",
                        set_extension,
                        get_extension,
                        extension_same,
                        g_extension_ok);
    __android_log_print(ANDROID_LOG_INFO, "libbinderweaktest",
                        "extension refs=%d/%d/%d null_get=%d",
                        extension_ref_after_set,
                        extension_ref_after_get_release,
                        extension_ref_after_owner_destroy,
                        null_get);
    __android_log_print(ANDROID_LOG_INFO, "libbinderweaktest",
                        "extension null_set=%d",
                        null_set);
}
