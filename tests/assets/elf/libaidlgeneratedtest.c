#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <stdint.h>

extern AIBinder* AServiceManager_checkService(const char* instance);
extern binder_status_t AServiceManager_addService(AIBinder* binder,
                                                  const char* instance);

static const char* kDescriptor = "com.example.muplar.generated.IAdder";
static const char* kCallbackDescriptor =
    "com.example.muplar.generated.ICallback";
static const char* kServiceName =
    "com.example.muplar.generated.IAdder/default";
static const char* kServiceErrorMessage = "generated-service-error";
static const char* kExceptionMessage = "generated-exception";

static const transaction_code_t kAddCode = FIRST_CALL_TRANSACTION;
static const transaction_code_t kEchoBinderCode = FIRST_CALL_TRANSACTION + 1;
static const transaction_code_t kFailServiceCode = FIRST_CALL_TRANSACTION + 2;
static const transaction_code_t kFailExceptionCode = FIRST_CALL_TRANSACTION + 3;

static int g_generated_ok;
static int g_main_create_seen;
static int g_main_destroy_seen;
static int g_callback_create_seen;
static int g_callback_destroy_seen;
static int g_add_seen;
static int g_echo_seen;
static int g_service_error_seen;
static int g_exception_seen;

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

static int str_contains(const char* text, const char* needle)
{
    if (!text || !needle)
        return 0;
    if (*needle == '\0')
        return 1;

    for (const char* start = text; *start; ++start) {
        const char* p = start;
        const char* q = needle;
        while (*p && *q && *p == *q) {
            ++p;
            ++q;
        }
        if (*q == '\0')
            return 1;
    }
    return 0;
}

static void* main_on_create(void* args)
{
    g_main_create_seen = args == (void*)(uintptr_t)0x5230;
    return (void*)(uintptr_t)0x5231;
}

static void main_on_destroy(void* user_data)
{
    g_main_destroy_seen = user_data == (void*)(uintptr_t)0x5231;
}

static void* callback_on_create(void* args)
{
    g_callback_create_seen = args == (void*)(uintptr_t)0x5240;
    return (void*)(uintptr_t)0x5241;
}

static void callback_on_destroy(void* user_data)
{
    g_callback_destroy_seen = user_data == (void*)(uintptr_t)0x5241;
}

static binder_status_t write_ok_status(AParcel* out)
{
    AStatus* ok = AStatus_newOk();
    binder_status_t rc = ok ? AParcel_writeStatusHeader(out, ok)
                            : STATUS_NO_MEMORY;
    if (ok)
        AStatus_delete(ok);
    return rc;
}

static binder_status_t main_on_transact(AIBinder* binder,
                                        transaction_code_t code,
                                        const AParcel* in,
                                        AParcel* out)
{
    if (!binder)
        return STATUS_BAD_VALUE;

    if (code == kAddCode) {
        int32_t lhs = 0;
        int32_t rhs = 0;
        binder_status_t read_lhs = AParcel_readInt32(in, &lhs);
        binder_status_t read_rhs = AParcel_readInt32(in, &rhs);
        binder_status_t write_status = write_ok_status(out);
        binder_status_t write_sum = AParcel_writeInt32(out, lhs + rhs);
        g_add_seen =
            read_lhs == STATUS_OK &&
            read_rhs == STATUS_OK &&
            lhs == 40 &&
            rhs == 2 &&
            write_status == STATUS_OK &&
            write_sum == STATUS_OK;
        return g_add_seen ? STATUS_OK : STATUS_BAD_VALUE;
    }

    if (code == kEchoBinderCode) {
        AIBinder* callback = 0;
        binder_status_t read_binder = AParcel_readStrongBinder(in, &callback);
        int32_t ref_during = callback ? AIBinder_debugGetRefCount(callback) : 0;
        binder_status_t write_status = write_ok_status(out);
        binder_status_t write_binder = AParcel_writeStrongBinder(out, callback);
        if (callback)
            AIBinder_decStrong(callback);
        g_echo_seen =
            read_binder == STATUS_OK &&
            callback &&
            ref_during >= 2 &&
            write_status == STATUS_OK &&
            write_binder == STATUS_OK;
        return g_echo_seen ? STATUS_OK : STATUS_BAD_VALUE;
    }

    if (code == kFailServiceCode) {
        AStatus* fail =
            AStatus_fromServiceSpecificErrorWithMessage(
                77, kServiceErrorMessage);
        binder_status_t write_status = fail
            ? AParcel_writeStatusHeader(out, fail)
            : STATUS_NO_MEMORY;
        if (fail)
            AStatus_delete(fail);
        g_service_error_seen = write_status == STATUS_OK;
        return write_status;
    }

    if (code == kFailExceptionCode) {
        AStatus* fail =
            AStatus_fromExceptionCodeWithMessage(EX_ILLEGAL_STATE,
                                                 kExceptionMessage);
        binder_status_t write_status = fail
            ? AParcel_writeStatusHeader(out, fail)
            : STATUS_NO_MEMORY;
        if (fail)
            AStatus_delete(fail);
        g_exception_seen = write_status == STATUS_OK;
        return write_status;
    }

    return STATUS_UNKNOWN_TRANSACTION;
}

static binder_status_t generated_proxy_add(AIBinder* binder,
                                           int32_t* out_sum)
{
    AParcel* in = 0;
    AParcel* out = 0;
    AStatus* status = 0;
    binder_status_t rc = AIBinder_prepareTransaction(binder, &in);
    if (rc != STATUS_OK)
        goto done;

    rc = AParcel_writeInt32(in, 40);
    if (rc != STATUS_OK)
        goto done;
    rc = AParcel_writeInt32(in, 2);
    if (rc != STATUS_OK)
        goto done;

    rc = AIBinder_transact(binder, kAddCode, &in, &out, 0);
    if (rc != STATUS_OK)
        goto done;
    rc = out ? AParcel_readStatusHeader(out, &status) : STATUS_BAD_VALUE;
    if (rc != STATUS_OK)
        goto done;
    if (!status || !AStatus_isOk(status)) {
        rc = STATUS_UNKNOWN_ERROR;
        goto done;
    }
    rc = AParcel_readInt32(out, out_sum);

done:
    if (status)
        AStatus_delete(status);
    if (out)
        AParcel_delete(out);
    if (in)
        AParcel_delete(in);
    return rc;
}

static binder_status_t generated_proxy_echo_binder(AIBinder* binder,
                                                   AIBinder* callback,
                                                   AIBinder** out_callback)
{
    AParcel* in = 0;
    AParcel* out = 0;
    AStatus* status = 0;
    binder_status_t rc = AIBinder_prepareTransaction(binder, &in);
    if (rc != STATUS_OK)
        goto done;

    rc = AParcel_writeStrongBinder(in, callback);
    if (rc != STATUS_OK)
        goto done;

    rc = AIBinder_transact(binder, kEchoBinderCode, &in, &out, 0);
    if (rc != STATUS_OK)
        goto done;
    rc = out ? AParcel_readStatusHeader(out, &status) : STATUS_BAD_VALUE;
    if (rc != STATUS_OK)
        goto done;
    if (!status || !AStatus_isOk(status)) {
        rc = STATUS_UNKNOWN_ERROR;
        goto done;
    }
    rc = AParcel_readStrongBinder(out, out_callback);

done:
    if (status)
        AStatus_delete(status);
    if (out)
        AParcel_delete(out);
    if (in)
        AParcel_delete(in);
    return rc;
}

static binder_status_t generated_proxy_expect_service_error(AIBinder* binder)
{
    AParcel* in = 0;
    AParcel* out = 0;
    AStatus* status = 0;
    const char* description = 0;
    binder_status_t rc = AIBinder_prepareTransaction(binder, &in);
    if (rc != STATUS_OK)
        goto done;

    rc = AIBinder_transact(binder, kFailServiceCode, &in, &out, 0);
    if (rc != STATUS_OK)
        goto done;
    rc = out ? AParcel_readStatusHeader(out, &status) : STATUS_BAD_VALUE;
    if (rc != STATUS_OK)
        goto done;
    description = status ? AStatus_getDescription(status) : 0;
    if (!status ||
        AStatus_isOk(status) ||
        AStatus_getExceptionCode(status) != EX_SERVICE_SPECIFIC ||
        AStatus_getServiceSpecificError(status) != 77 ||
        AStatus_getStatus(status) != STATUS_OK ||
        !str_eq(AStatus_getMessage(status), kServiceErrorMessage) ||
        !str_contains(description, kServiceErrorMessage)) {
        rc = STATUS_BAD_VALUE;
        goto done;
    }
    rc = STATUS_OK;

done:
    if (description)
        AStatus_deleteDescription(description);
    if (status)
        AStatus_delete(status);
    if (out)
        AParcel_delete(out);
    if (in)
        AParcel_delete(in);
    return rc;
}

static binder_status_t generated_proxy_expect_exception(AIBinder* binder)
{
    AParcel* in = 0;
    AParcel* out = 0;
    AStatus* status = 0;
    binder_status_t rc = AIBinder_prepareTransaction(binder, &in);
    if (rc != STATUS_OK)
        goto done;

    rc = AIBinder_transact(binder, kFailExceptionCode, &in, &out, 0);
    if (rc != STATUS_OK)
        goto done;
    rc = out ? AParcel_readStatusHeader(out, &status) : STATUS_BAD_VALUE;
    if (rc != STATUS_OK)
        goto done;
    if (!status ||
        AStatus_isOk(status) ||
        AStatus_getExceptionCode(status) != EX_ILLEGAL_STATE ||
        AStatus_getServiceSpecificError(status) != 0 ||
        AStatus_getStatus(status) != STATUS_OK ||
        !str_eq(AStatus_getMessage(status), kExceptionMessage)) {
        rc = STATUS_BAD_VALUE;
        goto done;
    }
    rc = STATUS_OK;

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
    __android_log_print(ANDROID_LOG_INFO, "libaidlgeneratedtest",
                        "onDestroy generated=%d create=%d destroy=%d cb=%d/%d",
                        g_generated_ok,
                        g_main_create_seen,
                        g_main_destroy_seen,
                        g_callback_create_seen,
                        g_callback_destroy_seen);
}

void ANativeActivity_onCreate(ANativeActivity* activity,
                              void* saved_state,
                              size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    if (!activity || !activity->callbacks) {
        __android_log_print(ANDROID_LOG_ERROR, "libaidlgeneratedtest",
                            "invalid generated AIDL bootstrap");
        return;
    }

    AIBinder_Class* clazz =
        AIBinder_Class_define(kDescriptor, main_on_create,
                              main_on_destroy, main_on_transact);
    AIBinder_Class* callback_clazz =
        AIBinder_Class_define(kCallbackDescriptor, callback_on_create,
                              callback_on_destroy, main_on_transact);
    const char* actual_descriptor = AIBinder_Class_getDescriptor(clazz);
    AIBinder* service = clazz
        ? AIBinder_new(clazz, (void*)(uintptr_t)0x5230)
        : 0;
    AIBinder* callback = callback_clazz
        ? AIBinder_new(callback_clazz, (void*)(uintptr_t)0x5240)
        : 0;

    binder_status_t add_status = service
        ? AServiceManager_addService(service, kServiceName)
        : STATUS_BAD_VALUE;
    AIBinder* proxy = AServiceManager_checkService(kServiceName);
    int associate_ok = proxy ? AIBinder_associateClass(proxy, clazz) : 0;

    int32_t sum = 0;
    AIBinder* echoed_callback = 0;
    binder_status_t add_call =
        proxy ? generated_proxy_add(proxy, &sum) : STATUS_BAD_VALUE;
    binder_status_t echo_call =
        proxy && callback
            ? generated_proxy_echo_binder(proxy, callback, &echoed_callback)
            : STATUS_BAD_VALUE;
    int echoed_same = echoed_callback == callback;
    if (echoed_callback)
        AIBinder_decStrong(echoed_callback);

    binder_status_t service_error_call = proxy
        ? generated_proxy_expect_service_error(proxy)
        : STATUS_BAD_VALUE;
    binder_status_t exception_call = proxy
        ? generated_proxy_expect_exception(proxy)
        : STATUS_BAD_VALUE;

    if (service)
        AIBinder_decStrong(service);
    AIBinder* after_destroy = AServiceManager_checkService(kServiceName);
    if (callback)
        AIBinder_decStrong(callback);

    g_generated_ok =
        clazz &&
        callback_clazz &&
        str_eq(actual_descriptor, kDescriptor) &&
        service &&
        callback &&
        add_status == STATUS_OK &&
        proxy == service &&
        associate_ok &&
        add_call == STATUS_OK &&
        sum == 42 &&
        echo_call == STATUS_OK &&
        echoed_same &&
        service_error_call == STATUS_OK &&
        exception_call == STATUS_OK &&
        g_add_seen &&
        g_echo_seen &&
        g_service_error_seen &&
        g_exception_seen &&
        g_main_create_seen &&
        g_main_destroy_seen &&
        g_callback_create_seen &&
        g_callback_destroy_seen &&
        after_destroy == 0;

    activity->callbacks->onDestroy = on_activity_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libaidlgeneratedtest",
                        "generated add=%d sum=%d echo=%d same=%d ok=%d",
                        add_call,
                        sum,
                        echo_call,
                        echoed_same,
                        g_generated_ok);
    __android_log_print(ANDROID_LOG_INFO, "libaidlgeneratedtest",
                        "status service=%d exception=%d associate=%d cleanup=%d",
                        service_error_call,
                        exception_call,
                        associate_ok,
                        after_destroy == 0);
}
