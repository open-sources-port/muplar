#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <android/native_activity.h>

extern AIBinder* AServiceManager_checkService(const char* instance);

static int g_transaction_ok;

static void on_destroy(ANativeActivity* activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "libbindermuplardtest",
                        "onDestroy daemon_tx=%d", g_transaction_ok);
}

void ANativeActivity_onCreate(ANativeActivity* activity,
                              void* saved_state,
                              size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    AIBinder* service = AServiceManager_checkService("muplar.test.echo");
    AParcel* input = 0;
    AParcel* output = 0;
    AStatus* status = 0;
    int32_t reply = 0;
    binder_status_t prepare = service
        ? AIBinder_prepareTransaction(service, &input) : STATUS_BAD_VALUE;
    binder_status_t write = input
        ? AParcel_writeInt32(input, 73) : STATUS_BAD_VALUE;
    binder_status_t transact = input
        ? AIBinder_transact(service, FIRST_CALL_TRANSACTION + 23,
                            &input, &output, 0)
        : STATUS_BAD_VALUE;
    binder_status_t read_status = output
        ? AParcel_readStatusHeader(output, &status) : STATUS_BAD_VALUE;
    binder_status_t read_reply = output
        ? AParcel_readInt32(output, &reply) : STATUS_BAD_VALUE;

    g_transaction_ok = service && prepare == STATUS_OK && write == STATUS_OK &&
        transact == STATUS_OK && input == 0 && read_status == STATUS_OK &&
        status && AStatus_isOk(status) && read_reply == STATUS_OK && reply == 73;
    if (status) AStatus_delete(status);
    if (output) AParcel_delete(output);
    if (activity && activity->callbacks)
        activity->callbacks->onDestroy = on_destroy;

    __android_log_print(ANDROID_LOG_INFO, "libbindermuplardtest",
                        "daemon transaction reply=%d ok=%d", reply,
                        g_transaction_ok);
}
