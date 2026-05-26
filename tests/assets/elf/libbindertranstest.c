#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <android/native_activity.h>

extern AIBinder* AServiceManager_getService(const char* instance);

static int g_transaction_ok;

static void on_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libbindertranstest",
                        "onDestroy tx=%d", g_transaction_ok);
}

void ANativeActivity_onCreate(ANativeActivity* activity,
                              void* saved_state,
                              size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    if (!activity || !activity->callbacks) {
        __android_log_print(ANDROID_LOG_ERROR, "libbindertranstest",
                            "invalid transaction bootstrap");
        return;
    }

    AIBinder* service = AServiceManager_getService("activity");
    AParcel* in = 0;
    AParcel* out = 0;
    AStatus* status = 0;
    int32_t echo = 0;
    int32_t reply = 0;

    binder_status_t prepare = AIBinder_prepareTransaction(service, &in);
    binder_status_t write = in ? AParcel_writeInt32(in, 35) : STATUS_BAD_VALUE;
    int pos_after_write = in ? AParcel_getDataPosition(in) : -1;
    binder_status_t rewind = in ? AParcel_setDataPosition(in, 0) : STATUS_BAD_VALUE;
    binder_status_t read_echo = in ? AParcel_readInt32(in, &echo) : STATUS_BAD_VALUE;
    if (in)
        AParcel_setDataPosition(in, 0);

    binder_status_t transact =
        AIBinder_transact(service, FIRST_CALL_TRANSACTION + 6, &in, &out, 0);
    int in_cleared = in == 0;
    binder_status_t read_status =
        out ? AParcel_readStatusHeader(out, &status) : STATUS_BAD_VALUE;
    int status_ok = status ? AStatus_isOk(status) : 0;
    binder_status_t read_reply =
        out ? AParcel_readInt32(out, &reply) : STATUS_BAD_VALUE;
    int pos_after_reply = out ? AParcel_getDataPosition(out) : -1;

    if (status)
        AStatus_delete(status);
    if (out)
        AParcel_delete(out);

    g_transaction_ok =
        service &&
        prepare == STATUS_OK &&
        write == STATUS_OK &&
        pos_after_write > 0 &&
        rewind == STATUS_OK &&
        read_echo == STATUS_OK &&
        echo == 35 &&
        transact == STATUS_OK &&
        in_cleared &&
        read_status == STATUS_OK &&
        status_ok &&
        read_reply == STATUS_OK &&
        reply == 42 &&
        pos_after_reply > 0;

    activity->callbacks->onDestroy = on_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libbindertranstest",
                        "tx prep=%d write=%d trans=%d cleared=%d ok=%d",
                        prepare,
                        write,
                        transact,
                        in_cleared,
                        g_transaction_ok);
    __android_log_print(ANDROID_LOG_INFO, "libbindertranstest",
                        "parcel echo=%d reply=%d status=%d pos=%d",
                        echo,
                        reply,
                        status_ok,
                        pos_after_reply);
}
