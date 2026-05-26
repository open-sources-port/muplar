#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <stdbool.h>
#include <stdint.h>

struct StringRead {
    char buffer[64];
    int32_t length;
    int called;
    int null_seen;
};

static int g_string_ok;
static int g_on_transact_seen;
static int g_on_destroy_seen;

static int str_len(const char* s)
{
    int n = 0;
    while (s && s[n])
        ++n;
    return n;
}

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

static bool string_allocator(void* string_data, int32_t length, char** buffer)
{
    struct StringRead* out = (struct StringRead*)string_data;
    if (!out || !buffer)
        return false;

    out->called++;
    out->length = length;
    if (length < 0) {
        out->null_seen = 1;
        *buffer = 0;
        return true;
    }
    if (length <= 0 || length > (int32_t)sizeof(out->buffer))
        return false;

    *buffer = out->buffer;
    return true;
}

static void on_destroy(void* user_data)
{
    g_on_destroy_seen = user_data == (void*)(uintptr_t)0x517;
}

static binder_status_t on_transact(AIBinder* binder,
                                   transaction_code_t code,
                                   const AParcel* in,
                                   AParcel* out)
{
    struct StringRead input = { { 0 }, 0, 0, 0 };
    struct StringRead null_input = { { 0 }, 0, 0, 0 };
    AStatus* ok = AStatus_newOk();

    binder_status_t read_string =
        AParcel_readString(in, &input, string_allocator);
    binder_status_t read_null =
        AParcel_readString(in, &null_input, string_allocator);
    binder_status_t write_status = AParcel_writeStatusHeader(out, ok);
    binder_status_t write_string =
        AParcel_writeString(out, input.buffer, str_len(input.buffer));
    binder_status_t write_null = AParcel_writeString(out, 0, -1);
    if (ok)
        AStatus_delete(ok);

    g_on_transact_seen =
        binder &&
        code == FIRST_CALL_TRANSACTION + 17 &&
        read_string == STATUS_OK &&
        read_null == STATUS_OK &&
        input.called == 1 &&
        input.length == 7 &&
        str_eq(input.buffer, "phase5") &&
        null_input.called == 1 &&
        null_input.length == -1 &&
        null_input.null_seen &&
        write_status == STATUS_OK &&
        write_string == STATUS_OK &&
        write_null == STATUS_OK;

    return STATUS_OK;
}

static void on_activity_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libbinderstringtest",
                        "onDestroy string=%d transact=%d destroy=%d",
                        g_string_ok,
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
        __android_log_print(ANDROID_LOG_ERROR, "libbinderstringtest",
                            "invalid string binder bootstrap");
        return;
    }

    AIBinder_Class* clazz =
        AIBinder_Class_define("com.example.muplar.IString", 0,
                              on_destroy, on_transact);
    AIBinder* binder = AIBinder_new(clazz, (void*)(uintptr_t)0x517);

    AParcel* in = 0;
    AParcel* out = 0;
    AStatus* status = 0;
    struct StringRead reply = { { 0 }, 0, 0, 0 };
    struct StringRead null_reply = { { 0 }, 0, 0, 0 };

    binder_status_t prepare = AIBinder_prepareTransaction(binder, &in);
    binder_status_t write_string =
        in ? AParcel_writeString(in, "phase5", 6) : STATUS_BAD_VALUE;
    binder_status_t write_null =
        in ? AParcel_writeString(in, 0, -1) : STATUS_BAD_VALUE;
    binder_status_t transact =
        AIBinder_transact(binder, FIRST_CALL_TRANSACTION + 17, &in, &out, 0);
    binder_status_t read_status =
        out ? AParcel_readStatusHeader(out, &status) : STATUS_BAD_VALUE;
    int status_ok = status ? AStatus_isOk(status) : 0;
    binder_status_t read_reply =
        out ? AParcel_readString(out, &reply, string_allocator)
            : STATUS_BAD_VALUE;
    binder_status_t read_null_reply =
        out ? AParcel_readString(out, &null_reply, string_allocator)
            : STATUS_BAD_VALUE;

    if (status)
        AStatus_delete(status);
    if (out)
        AParcel_delete(out);
    if (binder)
        AIBinder_decStrong(binder);

    g_string_ok =
        clazz &&
        binder &&
        prepare == STATUS_OK &&
        write_string == STATUS_OK &&
        write_null == STATUS_OK &&
        transact == STATUS_OK &&
        in == 0 &&
        read_status == STATUS_OK &&
        status_ok &&
        read_reply == STATUS_OK &&
        reply.called == 1 &&
        reply.length == 7 &&
        str_eq(reply.buffer, "phase5") &&
        read_null_reply == STATUS_OK &&
        null_reply.called == 1 &&
        null_reply.length == -1 &&
        null_reply.null_seen &&
        g_on_transact_seen &&
        g_on_destroy_seen;

    activity->callbacks->onDestroy = on_activity_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libbinderstringtest",
                        "string reply=%s len=%d null=%d ok=%d",
                        reply.buffer,
                        reply.length,
                        null_reply.length,
                        g_string_ok);
    __android_log_print(ANDROID_LOG_INFO, "libbinderstringtest",
                        "status prep=%d write=%d trans=%d read=%d",
                        prepare,
                        write_string,
                        transact,
                        read_reply);
}
