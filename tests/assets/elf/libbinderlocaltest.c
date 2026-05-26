#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <stdint.h>

static int g_local_ok;
static int g_on_create_seen;
static int g_on_transact_seen;
static int g_on_destroy_seen;

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
    g_on_create_seen = args == (void*)(uintptr_t)0x51;
    return (void*)(uintptr_t)0x1234;
}

static void on_destroy(void* user_data)
{
    g_on_destroy_seen = user_data == (void*)(uintptr_t)0x1234;
}

static binder_status_t on_transact(AIBinder* binder,
                                   transaction_code_t code,
                                   const AParcel* in,
                                   AParcel* out)
{
    int32_t value = 0;
    AStatus* ok = AStatus_newOk();
    binder_status_t read = AParcel_readInt32(in, &value);
    binder_status_t write_status = AParcel_writeStatusHeader(out, ok);
    binder_status_t write_reply = AParcel_writeInt32(out, value + (int32_t)code);
    if (ok)
        AStatus_delete(ok);

    g_on_transact_seen =
        binder &&
        read == STATUS_OK &&
        write_status == STATUS_OK &&
        write_reply == STATUS_OK &&
        value == 31 &&
        code == FIRST_CALL_TRANSACTION + 10;

    return STATUS_OK;
}

static void on_activity_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libbinderlocaltest",
                        "onDestroy local=%d transact=%d destroy=%d",
                        g_local_ok,
                        g_on_transact_seen,
                        g_on_destroy_seen);
}

void ANativeActivity_onCreate(ANativeActivity* activity,
                              void* saved_state,
                              size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    if (!activity || !activity->callbacks) {
        __android_log_print(ANDROID_LOG_ERROR, "libbinderlocaltest",
                            "invalid local binder bootstrap");
        return;
    }

    const char* descriptor = "com.example.muplar.ILocal";
    AIBinder_Class* clazz =
        AIBinder_Class_define(descriptor, on_create, on_destroy, on_transact);
    const char* actual_descriptor = AIBinder_Class_getDescriptor(clazz);
    AIBinder* binder = AIBinder_new(clazz, (void*)(uintptr_t)0x51);
    int remote = binder ? AIBinder_isRemote(binder) : 1;
    const AIBinder_Class* actual_class = binder ? AIBinder_getClass(binder) : 0;
    void* user_data = binder ? AIBinder_getUserData(binder) : 0;

    AParcel* in = 0;
    AParcel* out = 0;
    AStatus* status = 0;
    int32_t reply = 0;

    binder_status_t prepare = AIBinder_prepareTransaction(binder, &in);
    binder_status_t write = in ? AParcel_writeInt32(in, 31) : STATUS_BAD_VALUE;
    binder_status_t transact =
        AIBinder_transact(binder, FIRST_CALL_TRANSACTION + 10, &in, &out, 0);
    binder_status_t read_status =
        out ? AParcel_readStatusHeader(out, &status) : STATUS_BAD_VALUE;
    int status_ok = status ? AStatus_isOk(status) : 0;
    binder_status_t read_reply =
        out ? AParcel_readInt32(out, &reply) : STATUS_BAD_VALUE;

    if (status)
        AStatus_delete(status);
    if (out)
        AParcel_delete(out);
    if (binder)
        AIBinder_decStrong(binder);

    g_local_ok =
        clazz &&
        str_eq(actual_descriptor, descriptor) &&
        binder &&
        remote == 0 &&
        actual_class == clazz &&
        user_data == (void*)(uintptr_t)0x1234 &&
        g_on_create_seen &&
        g_on_transact_seen &&
        g_on_destroy_seen &&
        prepare == STATUS_OK &&
        write == STATUS_OK &&
        transact == STATUS_OK &&
        in == 0 &&
        read_status == STATUS_OK &&
        status_ok &&
        read_reply == STATUS_OK &&
        reply == 42;

    activity->callbacks->onDestroy = on_activity_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libbinderlocaltest",
                        "local remote=%d user=%p reply=%d ok=%d",
                        remote,
                        user_data,
                        reply,
                        g_local_ok);
    __android_log_print(ANDROID_LOG_INFO, "libbinderlocaltest",
                        "status prep=%d write=%d trans=%d cb=%d",
                        prepare,
                        write,
                        transact,
                        g_on_transact_seen);
}
