#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <stdint.h>

extern AIBinder* AServiceManager_checkService(const char* instance);
extern binder_status_t AServiceManager_addService(AIBinder* binder,
                                                  const char* instance);

static const char* kDescriptor = "com.example.muplar.aidl.IAdder";
static const char* kServiceName = "com.example.muplar.aidl.IAdder/default";
static const transaction_code_t kAddCode = FIRST_CALL_TRANSACTION;

static int g_aidl_ok;
static int g_on_create_seen;
static int g_on_destroy_seen;
static int g_on_transact_seen;
static int g_token_consumed;

static int str_eq(const char* a, const char* b)
{
    if (!a || !b)
        return 0;
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static void* on_create(void* args)
{
    g_on_create_seen = args == (void*)(uintptr_t)0x5220;
    return (void*)(uintptr_t)0x5221;
}

static void on_destroy(void* user_data)
{
    g_on_destroy_seen = user_data == (void*)(uintptr_t)0x5221;
}

static binder_status_t on_transact(AIBinder* binder,
                                   transaction_code_t code,
                                   const AParcel* in,
                                   AParcel* out)
{
    int32_t lhs = 0;
    int32_t rhs = 0;
    AStatus* ok = AStatus_newOk();
    binder_status_t read_lhs = AParcel_readInt32(in, &lhs);
    binder_status_t read_rhs = AParcel_readInt32(in, &rhs);
    binder_status_t write_status = ok
        ? AParcel_writeStatusHeader(out, ok)
        : STATUS_NO_MEMORY;
    binder_status_t write_reply =
        AParcel_writeInt32(out, lhs + rhs);
    if (ok)
        AStatus_delete(ok);

    g_token_consumed =
        read_lhs == STATUS_OK &&
        read_rhs == STATUS_OK &&
        lhs == 40 &&
        rhs == 2;
    g_on_transact_seen =
        binder &&
        code == kAddCode &&
        g_token_consumed &&
        write_status == STATUS_OK &&
        write_reply == STATUS_OK;

    return g_on_transact_seen ? STATUS_OK : STATUS_BAD_VALUE;
}

static binder_status_t aidl_proxy_add(AIBinder* binder,
                                      int32_t lhs,
                                      int32_t rhs,
                                      int32_t* out_sum)
{
    AParcel* in = 0;
    AParcel* out = 0;
    AStatus* status = 0;
    binder_status_t rc = AIBinder_prepareTransaction(binder, &in);
    if (rc != STATUS_OK)
        goto done;

    rc = AParcel_writeInt32(in, lhs);
    if (rc != STATUS_OK)
        goto done;
    rc = AParcel_writeInt32(in, rhs);
    if (rc != STATUS_OK)
        goto done;

    rc = AIBinder_transact(binder, kAddCode, &in, &out, 0);
    if (rc != STATUS_OK)
        goto done;
    rc = out ? AParcel_readStatusHeader(out, &status) : STATUS_BAD_VALUE;
    if (rc != STATUS_OK)
        goto done;
    if (!status || !AStatus_isOk(status)) {
        rc = status ? AStatus_getStatus(status) : STATUS_BAD_VALUE;
        if (rc == STATUS_OK)
            rc = STATUS_UNKNOWN_ERROR;
        goto done;
    }
    rc = out ? AParcel_readInt32(out, out_sum) : STATUS_BAD_VALUE;

done:
    if (status)
        AStatus_delete(status);
    if (out)
        AParcel_delete(out);
    if (in)
        AParcel_delete(in);
    return rc;
}

static void on_activity_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libaidlsmoketest",
                        "onDestroy aidl=%d create=%d destroy=%d transact=%d",
                        g_aidl_ok,
                        g_on_create_seen,
                        g_on_destroy_seen,
                        g_on_transact_seen);
}

void ANativeActivity_onCreate(ANativeActivity* activity,
                              void* saved_state,
                              size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    if (!activity || !activity->callbacks) {
        __android_log_print(ANDROID_LOG_ERROR, "libaidlsmoketest",
                            "invalid AIDL smoke bootstrap");
        return;
    }

    AIBinder_Class* clazz =
        AIBinder_Class_define(kDescriptor, on_create, on_destroy, on_transact);
    const char* actual_descriptor = AIBinder_Class_getDescriptor(clazz);
    AIBinder* service = clazz
        ? AIBinder_new(clazz, (void*)(uintptr_t)0x5220)
        : 0;
    binder_status_t add_status = service
        ? AServiceManager_addService(service, kServiceName)
        : STATUS_BAD_VALUE;
    AIBinder* proxy = AServiceManager_checkService(kServiceName);

    int32_t sum = 0;
    binder_status_t call_status = proxy
        ? aidl_proxy_add(proxy, 40, 2, &sum)
        : STATUS_BAD_VALUE;

    if (service)
        AIBinder_decStrong(service);
    AIBinder* after_destroy = AServiceManager_checkService(kServiceName);

    g_aidl_ok =
        clazz &&
        str_eq(actual_descriptor, kDescriptor) &&
        service &&
        add_status == STATUS_OK &&
        proxy == service &&
        call_status == STATUS_OK &&
        sum == 42 &&
        g_token_consumed &&
        g_on_create_seen &&
        g_on_transact_seen &&
        g_on_destroy_seen &&
        after_destroy == 0;

    activity->callbacks->onDestroy = on_activity_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libaidlsmoketest",
                        "aidl add=%d call=%d sum=%d token=%d ok=%d",
                        add_status,
                        call_status,
                        sum,
                        g_token_consumed,
                        g_aidl_ok);
    __android_log_print(ANDROID_LOG_INFO, "libaidlsmoketest",
                        "service proxy=%d cleanup=%d descriptor=%d",
                        proxy == service,
                        after_destroy == 0,
                        str_eq(actual_descriptor, kDescriptor));
}
