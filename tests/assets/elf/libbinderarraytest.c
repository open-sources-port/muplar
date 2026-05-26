#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <stdbool.h>
#include <stdint.h>

struct IntArrayRead {
    int32_t values[8];
    int32_t length;
    int called;
    int null_seen;
};

struct StringArrayRead {
    char values[4][32];
    int32_t element_lengths[4];
    int32_t length;
    int array_called;
    int element_called;
    int null_array_seen;
    int null_elements;
};

struct StringArraySource {
    const char* values[4];
    int32_t lengths[4];
};

static int g_array_ok;
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

static bool int32_array_allocator(void* array_data,
                                  int32_t length,
                                  int32_t** out_buffer)
{
    struct IntArrayRead* out = (struct IntArrayRead*)array_data;
    if (!out || !out_buffer)
        return false;

    out->called++;
    out->length = length;
    if (length < 0) {
        out->null_seen = 1;
        *out_buffer = 0;
        return true;
    }
    if (length > (int32_t)(sizeof(out->values) / sizeof(out->values[0])))
        return false;

    *out_buffer = length > 0 ? out->values : 0;
    return true;
}

static const char* string_array_getter(const void* array_data,
                                       size_t index,
                                       int32_t* out_length)
{
    const struct StringArraySource* source =
        (const struct StringArraySource*)array_data;
    if (!source || !out_length || index >= 4)
        return 0;

    *out_length = source->lengths[index];
    return *out_length < 0 ? 0 : source->values[index];
}

static bool string_array_allocator(void* array_data, int32_t length)
{
    struct StringArrayRead* out = (struct StringArrayRead*)array_data;
    if (!out)
        return false;

    out->array_called++;
    out->length = length;
    if (length < 0) {
        out->null_array_seen = 1;
        return true;
    }
    return length <= 4;
}

static bool string_array_element_allocator(void* array_data,
                                           size_t index,
                                           int32_t length,
                                           char** buffer)
{
    struct StringArrayRead* out = (struct StringArrayRead*)array_data;
    if (!out || !buffer || index >= 4)
        return false;

    out->element_called++;
    out->element_lengths[index] = length;
    if (length < 0) {
        out->null_elements++;
        *buffer = 0;
        return true;
    }
    if (length <= 0 || length > (int32_t)sizeof(out->values[index]))
        return false;

    *buffer = out->values[index];
    return true;
}

static void on_destroy(void* user_data)
{
    g_on_destroy_seen = user_data == (void*)(uintptr_t)0x518;
}

static binder_status_t on_transact(AIBinder* binder,
                                   transaction_code_t code,
                                   const AParcel* in,
                                   AParcel* out)
{
    struct IntArrayRead ints = { { 0 }, 0, 0, 0 };
    struct IntArrayRead null_ints = { { 0 }, 0, 0, 0 };
    struct StringArrayRead strings = { { { 0 } }, { 0 }, 0, 0, 0, 0, 0 };
    struct StringArrayRead null_strings = { { { 0 } }, { 0 }, 0, 0, 0, 0, 0 };
    int32_t reply_ints[2];
    struct StringArraySource reply_strings;
    AStatus* ok = AStatus_newOk();

    reply_strings.values[0] = "array";
    reply_strings.values[1] = 0;
    reply_strings.values[2] = "phase";
    reply_strings.values[3] = 0;
    reply_strings.lengths[0] = 5;
    reply_strings.lengths[1] = -1;
    reply_strings.lengths[2] = 5;
    reply_strings.lengths[3] = 0;

    binder_status_t read_ints =
        AParcel_readInt32Array(in, &ints, int32_array_allocator);
    binder_status_t read_null_ints =
        AParcel_readInt32Array(in, &null_ints, int32_array_allocator);
    binder_status_t read_strings =
        AParcel_readStringArray(in, &strings, string_array_allocator,
                                string_array_element_allocator);
    binder_status_t read_null_strings =
        AParcel_readStringArray(in, &null_strings, string_array_allocator,
                                string_array_element_allocator);

    reply_ints[0] = ints.values[0] + ints.values[1] + ints.values[2];
    reply_ints[1] = ints.length;

    binder_status_t write_status = AParcel_writeStatusHeader(out, ok);
    binder_status_t write_ints = AParcel_writeInt32Array(out, reply_ints, 2);
    binder_status_t write_strings =
        AParcel_writeStringArray(out, &reply_strings, 3, string_array_getter);
    binder_status_t write_null_strings =
        AParcel_writeStringArray(out, 0, -1, string_array_getter);
    if (ok)
        AStatus_delete(ok);

    g_on_transact_seen =
        binder &&
        code == FIRST_CALL_TRANSACTION + 18 &&
        read_ints == STATUS_OK &&
        ints.called == 1 &&
        ints.length == 3 &&
        ints.values[0] == 7 &&
        ints.values[1] == 8 &&
        ints.values[2] == 9 &&
        read_null_ints == STATUS_OK &&
        null_ints.called == 1 &&
        null_ints.length == -1 &&
        null_ints.null_seen &&
        read_strings == STATUS_OK &&
        strings.array_called == 1 &&
        strings.length == 3 &&
        strings.element_called == 3 &&
        str_eq(strings.values[0], "alpha") &&
        strings.element_lengths[1] == -1 &&
        strings.null_elements == 1 &&
        str_eq(strings.values[2], "beta") &&
        read_null_strings == STATUS_OK &&
        null_strings.array_called == 1 &&
        null_strings.length == -1 &&
        null_strings.null_array_seen &&
        write_status == STATUS_OK &&
        write_ints == STATUS_OK &&
        write_strings == STATUS_OK &&
        write_null_strings == STATUS_OK;

    return STATUS_OK;
}

static void on_activity_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libbinderarraytest",
                        "onDestroy array=%d transact=%d destroy=%d",
                        g_array_ok,
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
        __android_log_print(ANDROID_LOG_ERROR, "libbinderarraytest",
                            "invalid array binder bootstrap");
        return;
    }

    int32_t input_ints[3];
    struct StringArraySource input_strings;
    struct IntArrayRead reply_ints = { { 0 }, 0, 0, 0 };
    struct StringArrayRead reply_strings = { { { 0 } }, { 0 }, 0, 0, 0, 0, 0 };
    struct StringArrayRead null_reply_strings = { { { 0 } }, { 0 }, 0, 0, 0, 0, 0 };

    input_ints[0] = 7;
    input_ints[1] = 8;
    input_ints[2] = 9;
    input_strings.values[0] = "alpha";
    input_strings.values[1] = 0;
    input_strings.values[2] = "beta";
    input_strings.values[3] = 0;
    input_strings.lengths[0] = 5;
    input_strings.lengths[1] = -1;
    input_strings.lengths[2] = 4;
    input_strings.lengths[3] = 0;

    AIBinder_Class* clazz =
        AIBinder_Class_define("com.example.muplar.IArray", 0,
                              on_destroy, on_transact);
    AIBinder* binder = AIBinder_new(clazz, (void*)(uintptr_t)0x518);

    AParcel* in = 0;
    AParcel* out = 0;
    AStatus* status = 0;

    binder_status_t prepare = AIBinder_prepareTransaction(binder, &in);
    binder_status_t write_ints =
        in ? AParcel_writeInt32Array(in, input_ints, 3) : STATUS_BAD_VALUE;
    binder_status_t write_null_ints =
        in ? AParcel_writeInt32Array(in, 0, -1) : STATUS_BAD_VALUE;
    binder_status_t write_strings =
        in ? AParcel_writeStringArray(in, &input_strings, 3,
                                      string_array_getter)
           : STATUS_BAD_VALUE;
    binder_status_t write_null_strings =
        in ? AParcel_writeStringArray(in, 0, -1, string_array_getter)
           : STATUS_BAD_VALUE;
    binder_status_t transact =
        AIBinder_transact(binder, FIRST_CALL_TRANSACTION + 18, &in, &out, 0);
    binder_status_t read_status =
        out ? AParcel_readStatusHeader(out, &status) : STATUS_BAD_VALUE;
    int status_ok = status ? AStatus_isOk(status) : 0;
    binder_status_t read_ints =
        out ? AParcel_readInt32Array(out, &reply_ints, int32_array_allocator)
            : STATUS_BAD_VALUE;
    binder_status_t read_strings =
        out ? AParcel_readStringArray(out, &reply_strings,
                                      string_array_allocator,
                                      string_array_element_allocator)
            : STATUS_BAD_VALUE;
    binder_status_t read_null_strings =
        out ? AParcel_readStringArray(out, &null_reply_strings,
                                      string_array_allocator,
                                      string_array_element_allocator)
            : STATUS_BAD_VALUE;

    if (status)
        AStatus_delete(status);
    if (out)
        AParcel_delete(out);
    if (binder)
        AIBinder_decStrong(binder);

    g_array_ok =
        clazz &&
        binder &&
        prepare == STATUS_OK &&
        write_ints == STATUS_OK &&
        write_null_ints == STATUS_OK &&
        write_strings == STATUS_OK &&
        write_null_strings == STATUS_OK &&
        transact == STATUS_OK &&
        in == 0 &&
        read_status == STATUS_OK &&
        status_ok &&
        read_ints == STATUS_OK &&
        reply_ints.called == 1 &&
        reply_ints.length == 2 &&
        reply_ints.values[0] == 24 &&
        reply_ints.values[1] == 3 &&
        read_strings == STATUS_OK &&
        reply_strings.array_called == 1 &&
        reply_strings.length == 3 &&
        reply_strings.element_called == 3 &&
        str_eq(reply_strings.values[0], "array") &&
        reply_strings.element_lengths[1] == -1 &&
        reply_strings.null_elements == 1 &&
        str_eq(reply_strings.values[2], "phase") &&
        read_null_strings == STATUS_OK &&
        null_reply_strings.array_called == 1 &&
        null_reply_strings.length == -1 &&
        null_reply_strings.null_array_seen &&
        g_on_transact_seen &&
        g_on_destroy_seen;

    activity->callbacks->onDestroy = on_activity_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libbinderarraytest",
                        "array ints=%d/%d strings=%s/%s ok=%d",
                        reply_ints.values[0],
                        reply_ints.values[1],
                        reply_strings.values[0],
                        reply_strings.values[2],
                        g_array_ok);
    __android_log_print(ANDROID_LOG_INFO, "libbinderarraytest",
                        "status prep=%d write=%d trans=%d read=%d",
                        prepare,
                        write_ints,
                        transact,
                        read_ints);
}
