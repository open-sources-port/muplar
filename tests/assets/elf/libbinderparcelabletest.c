#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

struct Point {
    int32_t x;
    int32_t y;
};

struct PointArray {
    struct Point values[4];
    int32_t length;
    int allocator_called;
    int element_calls;
    int null_seen;
};

static int g_payload_ok;
static int g_on_transact_seen;
static int g_on_destroy_seen;

static bool point_array_allocator(void* array_data, int32_t length)
{
    struct PointArray* out = (struct PointArray*)array_data;
    if (!out)
        return false;

    out->allocator_called++;
    out->length = length;
    if (length < 0) {
        out->null_seen = 1;
        return true;
    }
    return length <= (int32_t)(sizeof(out->values) / sizeof(out->values[0]));
}

static binder_status_t point_writer(AParcel* parcel,
                                    const void* array_data,
                                    size_t index)
{
    const struct PointArray* points = (const struct PointArray*)array_data;
    if (!parcel || !points || index >= (size_t)points->length)
        return STATUS_BAD_VALUE;

    binder_status_t write_x = AParcel_writeInt32(parcel, points->values[index].x);
    if (write_x != STATUS_OK)
        return write_x;
    return AParcel_writeInt32(parcel, points->values[index].y);
}

static binder_status_t point_reader(const AParcel* parcel,
                                    void* array_data,
                                    size_t index)
{
    struct PointArray* points = (struct PointArray*)array_data;
    if (!parcel || !points || index >= 4)
        return STATUS_BAD_VALUE;

    binder_status_t read_x = AParcel_readInt32(parcel, &points->values[index].x);
    if (read_x != STATUS_OK)
        return read_x;
    binder_status_t read_y = AParcel_readInt32(parcel, &points->values[index].y);
    if (read_y == STATUS_OK)
        points->element_calls++;
    return read_y;
}

static void on_destroy(void* user_data)
{
    g_on_destroy_seen = user_data == (void*)(uintptr_t)0x519;
}

static binder_status_t on_transact(AIBinder* binder,
                                   transaction_code_t code,
                                   const AParcel* in,
                                   AParcel* out)
{
    struct PointArray points = { { { 0, 0 } }, 0, 0, 0, 0 };
    struct PointArray null_points = { { { 0, 0 } }, 0, 0, 0, 0 };
    struct PointArray reply_points = { { { 0, 0 } }, 0, 0, 0, 0 };
    int fd = -2;
    int null_fd = -2;
    AStatus* ok = AStatus_newOk();

    binder_status_t read_points =
        AParcel_readParcelableArray(in, &points, point_array_allocator,
                                    point_reader);
    binder_status_t read_null_points =
        AParcel_readParcelableArray(in, &null_points, point_array_allocator,
                                    point_reader);
    binder_status_t read_fd = AParcel_readParcelFileDescriptor(in, &fd);
    binder_status_t read_null_fd =
        AParcel_readParcelFileDescriptor(in, &null_fd);

    reply_points.length = 2;
    reply_points.values[0].x = points.values[0].x + points.values[1].x;
    reply_points.values[0].y = points.values[0].y + points.values[1].y;
    reply_points.values[1].x = points.length;
    reply_points.values[1].y = (int32_t)(code - FIRST_CALL_TRANSACTION);

    binder_status_t write_status = AParcel_writeStatusHeader(out, ok);
    binder_status_t write_points =
        AParcel_writeParcelableArray(out, &reply_points,
                                     reply_points.length, point_writer);
    binder_status_t write_null_points =
        AParcel_writeParcelableArray(out, 0, -1, point_writer);
    binder_status_t write_fd =
        AParcel_writeParcelFileDescriptor(out, fd);
    binder_status_t write_null_fd =
        AParcel_writeParcelFileDescriptor(out, -1);

    if (ok)
        AStatus_delete(ok);

    g_on_transact_seen =
        binder &&
        code == FIRST_CALL_TRANSACTION + 19 &&
        read_points == STATUS_OK &&
        points.allocator_called == 1 &&
        points.element_calls == 2 &&
        points.length == 2 &&
        points.values[0].x == 11 &&
        points.values[0].y == 12 &&
        points.values[1].x == 13 &&
        points.values[1].y == 14 &&
        read_null_points == STATUS_OK &&
        null_points.allocator_called == 1 &&
        null_points.length == -1 &&
        null_points.null_seen &&
        read_fd == STATUS_OK &&
        fd >= 0 &&
        read_null_fd == STATUS_OK &&
        null_fd == -1 &&
        write_status == STATUS_OK &&
        write_points == STATUS_OK &&
        write_null_points == STATUS_OK &&
        write_fd == STATUS_OK &&
        write_null_fd == STATUS_OK;

    return STATUS_OK;
}

static void on_activity_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libbinderparcelabletest",
                        "onDestroy payload=%d transact=%d destroy=%d",
                        g_payload_ok,
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
        __android_log_print(ANDROID_LOG_ERROR, "libbinderparcelabletest",
                            "invalid parcelable binder bootstrap");
        return;
    }

    struct PointArray input_points = { { { 0, 0 } }, 0, 0, 0, 0 };
    struct PointArray reply_points = { { { 0, 0 } }, 0, 0, 0, 0 };
    struct PointArray null_reply_points = { { { 0, 0 } }, 0, 0, 0, 0 };
    int pipe_fds[2];
    unsigned char input_byte = 0x5a;
    pipe_fds[0] = -1;
    pipe_fds[1] = -1;
    int pipe_ok = pipe(pipe_fds) == 0 &&
                  write(pipe_fds[1], &input_byte, 1) == 1;
    int input_fd = pipe_fds[0];
    int reply_fd = -2;
    int null_reply_fd = -2;
    unsigned char byte = 0;

    input_points.length = 2;
    input_points.values[0].x = 11;
    input_points.values[0].y = 12;
    input_points.values[1].x = 13;
    input_points.values[1].y = 14;

    AIBinder_Class* clazz =
        AIBinder_Class_define("com.example.muplar.IParcelable", 0,
                              on_destroy, on_transact);
    AIBinder* binder = AIBinder_new(clazz, (void*)(uintptr_t)0x519);

    AParcel* in = 0;
    AParcel* out = 0;
    AStatus* status = 0;

    binder_status_t prepare = AIBinder_prepareTransaction(binder, &in);
    binder_status_t write_points =
        in ? AParcel_writeParcelableArray(in, &input_points,
                                          input_points.length, point_writer)
           : STATUS_BAD_VALUE;
    binder_status_t write_null_points =
        in ? AParcel_writeParcelableArray(in, 0, -1, point_writer)
           : STATUS_BAD_VALUE;
    binder_status_t write_fd =
        in && pipe_ok ? AParcel_writeParcelFileDescriptor(in, input_fd)
                      : STATUS_BAD_VALUE;
    binder_status_t write_null_fd =
        in ? AParcel_writeParcelFileDescriptor(in, -1) : STATUS_BAD_VALUE;
    binder_status_t transact =
        AIBinder_transact(binder, FIRST_CALL_TRANSACTION + 19, &in, &out, 0);
    binder_status_t read_status =
        out ? AParcel_readStatusHeader(out, &status) : STATUS_BAD_VALUE;
    int status_ok = status ? AStatus_isOk(status) : 0;
    binder_status_t read_points =
        out ? AParcel_readParcelableArray(out, &reply_points,
                                          point_array_allocator,
                                          point_reader)
            : STATUS_BAD_VALUE;
    binder_status_t read_null_points =
        out ? AParcel_readParcelableArray(out, &null_reply_points,
                                          point_array_allocator,
                                          point_reader)
            : STATUS_BAD_VALUE;
    binder_status_t read_fd =
        out ? AParcel_readParcelFileDescriptor(out, &reply_fd)
            : STATUS_BAD_VALUE;
    binder_status_t read_null_fd =
        out ? AParcel_readParcelFileDescriptor(out, &null_reply_fd)
            : STATUS_BAD_VALUE;
    int fd_read_ok = reply_fd >= 0 && read(reply_fd, &byte, 1) == 1;

    if (status)
        AStatus_delete(status);
    if (out)
        AParcel_delete(out);
    if (binder)
        AIBinder_decStrong(binder);
    if (pipe_fds[1] >= 0)
        close(pipe_fds[1]);
    if (reply_fd >= 0 && reply_fd != input_fd)
        close(reply_fd);
    if (input_fd >= 0)
        close(input_fd);

    g_payload_ok =
        clazz &&
        binder &&
        pipe_ok &&
        prepare == STATUS_OK &&
        write_points == STATUS_OK &&
        write_null_points == STATUS_OK &&
        write_fd == STATUS_OK &&
        write_null_fd == STATUS_OK &&
        transact == STATUS_OK &&
        in == 0 &&
        read_status == STATUS_OK &&
        status_ok &&
        read_points == STATUS_OK &&
        reply_points.allocator_called == 1 &&
        reply_points.element_calls == 2 &&
        reply_points.length == 2 &&
        reply_points.values[0].x == 24 &&
        reply_points.values[0].y == 26 &&
        reply_points.values[1].x == 2 &&
        reply_points.values[1].y == 19 &&
        read_null_points == STATUS_OK &&
        null_reply_points.allocator_called == 1 &&
        null_reply_points.length == -1 &&
        null_reply_points.null_seen &&
        read_fd == STATUS_OK &&
        fd_read_ok &&
        read_null_fd == STATUS_OK &&
        null_reply_fd == -1 &&
        g_on_transact_seen &&
        g_on_destroy_seen;

    activity->callbacks->onDestroy = on_activity_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libbinderparcelabletest",
                        "parcelable sum=%d/%d fd=%d nullfd=%d ok=%d",
                        reply_points.values[0].x,
                        reply_points.values[0].y,
                        fd_read_ok,
                        null_reply_fd,
                        g_payload_ok);
}
