#include "com/example/muplar/IRealAdder.h"

#include <android/binder_ibinder.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <stdint.h>

extern "C" AIBinder* AServiceManager_checkService(const char* instance);
extern "C" binder_status_t AServiceManager_addService(AIBinder* binder,
                                                       const char* instance);

using aidl_generated::com::example::muplar::BnRealAdder;
using aidl_generated::com::example::muplar::BpRealAdder;
using aidl_generated::com::example::muplar::IRealAdder;
using aidl_generated::com::example::muplar::RealAdderCallbacks;

static const char* kServiceName =
    "com.example.muplar.IRealAdder/default";

static int g_real_source_ok;
static int g_add_seen;
static int g_echo_seen;
static int g_destroy_seen;

static int str_eq(const char* lhs, const char* rhs)
{
    if (!lhs || !rhs)
        return 0;
    while (*lhs && *rhs && *lhs == *rhs) {
        ++lhs;
        ++rhs;
    }
    return *lhs == '\0' && *rhs == '\0';
}

static binder_status_t real_add(void* cookie,
                                int32_t lhs,
                                int32_t rhs,
                                int32_t* out_sum)
{
    g_add_seen =
        cookie == reinterpret_cast<void*>(static_cast<uintptr_t>(0x5525)) &&
        lhs == 13 &&
        rhs == 29 &&
        out_sum;
    if (out_sum)
        *out_sum = lhs + rhs;
    return g_add_seen ? STATUS_OK : STATUS_BAD_VALUE;
}

static binder_status_t real_echo_binder(void* cookie,
                                        AIBinder* binder,
                                        AIBinder** out_binder)
{
    g_echo_seen =
        cookie == reinterpret_cast<void*>(static_cast<uintptr_t>(0x5525)) &&
        binder &&
        out_binder;
    if (out_binder)
        *out_binder = binder;
    return g_echo_seen ? STATUS_OK : STATUS_BAD_VALUE;
}

static void real_destroy(void* cookie)
{
    g_destroy_seen =
        cookie == reinterpret_cast<void*>(static_cast<uintptr_t>(0x5525));
}

static void on_activity_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libaidlrealsource",
                        "onDestroy real=%d add=%d echo=%d destroy=%d",
                        g_real_source_ok,
                        g_add_seen,
                        g_echo_seen,
                        g_destroy_seen);
}

extern "C" void ANativeActivity_onCreate(ANativeActivity* activity,
                                         void* saved_state,
                                         size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    if (!activity || !activity->callbacks) {
        __android_log_print(ANDROID_LOG_ERROR, "libaidlrealsource",
                            "invalid real AIDL source bootstrap");
        return;
    }

    RealAdderCallbacks callbacks = {
        reinterpret_cast<void*>(static_cast<uintptr_t>(0x5525)),
        real_add,
        real_echo_binder,
        real_destroy,
    };

    AIBinder* service = BnRealAdder::create(&callbacks);
    binder_status_t add_service = service
        ? AServiceManager_addService(service, kServiceName)
        : STATUS_BAD_VALUE;
    AIBinder* service_lookup = AServiceManager_checkService(kServiceName);

    int32_t sum = 0;
    binder_status_t add_call = STATUS_BAD_VALUE;
    binder_status_t echo_call = STATUS_BAD_VALUE;
    int echoed_same = 0;
    int descriptor_ok =
        str_eq(IRealAdder::descriptor(), "com.example.muplar.IRealAdder");

    {
        BpRealAdder proxy(service_lookup);
        add_call = proxy.add(13, 29, &sum);
        AIBinder* echoed = nullptr;
        echo_call = proxy.echoBinder(service, &echoed);
        echoed_same = echoed == service;
        if (echoed)
            AIBinder_decStrong(echoed);
    }

    if (service)
        AIBinder_decStrong(service);
    AIBinder* after_destroy = AServiceManager_checkService(kServiceName);

    g_real_source_ok =
        service &&
        add_service == STATUS_OK &&
        service_lookup == service &&
        descriptor_ok &&
        add_call == STATUS_OK &&
        echo_call == STATUS_OK &&
        sum == 42 &&
        echoed_same &&
        g_add_seen &&
        g_echo_seen &&
        g_destroy_seen &&
        after_destroy == nullptr;

    activity->callbacks->onDestroy = on_activity_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libaidlrealsource",
                        "real add=%d echo=%d sum=%d same=%d ok=%d",
                        add_call,
                        echo_call,
                        sum,
                        echoed_same,
                        g_real_source_ok);
    __android_log_print(ANDROID_LOG_INFO, "libaidlrealsource",
                        "service add=%d lookup=%d cleanup=%d desc=%d",
                        add_service,
                        service_lookup == service,
                        after_destroy == nullptr,
                        descriptor_ok);
}
