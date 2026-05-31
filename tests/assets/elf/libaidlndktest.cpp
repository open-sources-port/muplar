#include <aidl/com/example/muplar/BnRealAdder.h>
#include <aidl/com/example/muplar/IRealAdder.h>

#include <android/binder_ibinder.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <android/native_activity.h>

extern "C" AIBinder* AServiceManager_checkService(const char* instance);
extern "C" binder_status_t AServiceManager_addService(AIBinder* binder,
                                                       const char* instance);

using aidl::com::example::muplar::BnRealAdder;
using aidl::com::example::muplar::IRealAdder;

static const char* kServiceName =
    "com.example.muplar.IRealAdder/default";

static int g_ndk_aidl_ok;
static int g_add_seen;
static int g_echo_seen;

class RealAdderService : public BnRealAdder {
public:
    ::ndk::ScopedAStatus add(int32_t in_lhs,
                             int32_t in_rhs,
                             int32_t* _aidl_return) override
    {
        g_add_seen = in_lhs == 13 && in_rhs == 29 && _aidl_return;
        if (_aidl_return)
            *_aidl_return = in_lhs + in_rhs;
        return g_add_seen ? ::ndk::ScopedAStatus::ok()
                         : ::ndk::ScopedAStatus::fromStatus(STATUS_BAD_VALUE);
    }

    ::ndk::ScopedAStatus echoBinder(const ::ndk::SpAIBinder& in_binder,
                                    ::ndk::SpAIBinder* _aidl_return) override
    {
        g_echo_seen = in_binder.get() && _aidl_return;
        if (_aidl_return)
            *_aidl_return = in_binder;
        return g_echo_seen ? ::ndk::ScopedAStatus::ok()
                           : ::ndk::ScopedAStatus::fromStatus(STATUS_BAD_VALUE);
    }
};

static void on_activity_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libaidlndk",
                        "onDestroy ndk=%d add=%d echo=%d",
                        g_ndk_aidl_ok,
                        g_add_seen,
                        g_echo_seen);
}

extern "C" void ANativeActivity_onCreate(ANativeActivity* activity,
                                         void* saved_state,
                                         size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    __android_log_print(ANDROID_LOG_INFO, "libaidlndk", "onCreate enter");

    if (!activity || !activity->callbacks) {
        __android_log_print(ANDROID_LOG_ERROR, "libaidlndk",
                            "invalid NDK AIDL bootstrap");
        return;
    }

    std::shared_ptr<RealAdderService> service =
        ::ndk::SharedRefBase::make<RealAdderService>();
    ::ndk::SpAIBinder service_binder =
        service ? service->asBinder() : ::ndk::SpAIBinder(nullptr);
    binder_status_t add_service = service_binder.get()
        ? AServiceManager_addService(service_binder.get(), kServiceName)
        : STATUS_BAD_VALUE;
    ::ndk::SpAIBinder service_lookup(
        AServiceManager_checkService(kServiceName));

    int32_t sum = 0;
    binder_status_t add_call = STATUS_BAD_VALUE;
    binder_status_t echo_call = STATUS_BAD_VALUE;
    int echoed_same = 0;

    std::shared_ptr<IRealAdder> proxy =
        IRealAdder::fromBinder(service_lookup);
    int proxy_seen = proxy ? 1 : 0;
    if (proxy) {
        ::ndk::ScopedAStatus add_status = proxy->add(13, 29, &sum);
        add_call = add_status.getStatus();

        ::ndk::SpAIBinder echoed;
        ::ndk::ScopedAStatus echo_status =
            proxy->echoBinder(service_binder, &echoed);
        echo_call = echo_status.getStatus();
        echoed_same = echoed.get() == service_binder.get();
    }

    int lookup_same = service_lookup.get() == service_binder.get();
    proxy.reset();
    service_lookup.set(nullptr);
    service_binder.set(nullptr);
    AIBinder* after_destroy = AServiceManager_checkService(kServiceName);

    g_ndk_aidl_ok =
        service &&
        add_service == STATUS_OK &&
        lookup_same &&
        proxy_seen &&
        add_call == STATUS_OK &&
        echo_call == STATUS_OK &&
        sum == 42 &&
        echoed_same &&
        g_add_seen &&
        g_echo_seen &&
        after_destroy == nullptr;

    activity->callbacks->onDestroy = on_activity_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libaidlndk",
                        "ndk add=%d echo=%d sum=%d same=%d ok=%d",
                        add_call,
                        echo_call,
                        sum,
                        echoed_same,
                        g_ndk_aidl_ok);
    __android_log_print(ANDROID_LOG_INFO, "libaidlndk",
                        "service add=%d lookup=%d cleanup=%d",
                        add_service,
                        lookup_same,
                        after_destroy == nullptr);
}
