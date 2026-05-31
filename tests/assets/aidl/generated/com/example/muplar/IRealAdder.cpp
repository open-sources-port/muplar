#include "com/example/muplar/IRealAdder.h"

namespace aidl_generated {
namespace com {
namespace example {
namespace muplar {

namespace {

AIBinder_Class* g_real_adder_class = nullptr;

void* on_create(void* args)
{
    return args;
}

void on_destroy(void* user_data)
{
    RealAdderCallbacks* callbacks =
        static_cast<RealAdderCallbacks*>(user_data);
    if (callbacks && callbacks->onDestroy)
        callbacks->onDestroy(callbacks->cookie);
}

binder_status_t write_status(AParcel* out, binder_status_t status)
{
    AStatus* aidl_status = status == STATUS_OK
        ? AStatus_newOk()
        : AStatus_fromStatus(status);
    binder_status_t rc = aidl_status
        ? AParcel_writeStatusHeader(out, aidl_status)
        : STATUS_NO_MEMORY;
    if (aidl_status)
        AStatus_delete(aidl_status);
    return rc;
}

binder_status_t on_transact(AIBinder* binder,
                            transaction_code_t code,
                            const AParcel* in,
                            AParcel* out)
{
    RealAdderCallbacks* callbacks =
        static_cast<RealAdderCallbacks*>(AIBinder_getUserData(binder));
    if (!callbacks)
        return STATUS_BAD_VALUE;

    if (code == IRealAdder::TRANSACTION_add) {
        int32_t lhs = 0;
        int32_t rhs = 0;
        int32_t sum = 0;
        binder_status_t rc = AParcel_readInt32(in, &lhs);
        if (rc != STATUS_OK)
            return rc;
        rc = AParcel_readInt32(in, &rhs);
        if (rc != STATUS_OK)
            return rc;
        rc = callbacks->add
            ? callbacks->add(callbacks->cookie, lhs, rhs, &sum)
            : STATUS_UNKNOWN_TRANSACTION;
        binder_status_t status_rc = write_status(out, rc);
        if (status_rc != STATUS_OK)
            return status_rc;
        if (rc != STATUS_OK)
            return STATUS_OK;
        return AParcel_writeInt32(out, sum);
    }

    if (code == IRealAdder::TRANSACTION_echoBinder) {
        AIBinder* in_binder = nullptr;
        AIBinder* out_binder = nullptr;
        binder_status_t rc = AParcel_readStrongBinder(in, &in_binder);
        if (rc != STATUS_OK)
            return rc;
        rc = callbacks->echoBinder
            ? callbacks->echoBinder(callbacks->cookie,
                                    in_binder,
                                    &out_binder)
            : STATUS_UNKNOWN_TRANSACTION;
        binder_status_t status_rc = write_status(out, rc);
        if (status_rc == STATUS_OK && rc == STATUS_OK)
            status_rc = AParcel_writeStrongBinder(out, out_binder);
        if (in_binder)
            AIBinder_decStrong(in_binder);
        return status_rc;
    }

    return STATUS_UNKNOWN_TRANSACTION;
}

} // namespace

const char* IRealAdder::descriptor()
{
    return "com.example.muplar.IRealAdder";
}

const AIBinder_Class* BnRealAdder::clazz()
{
    if (!g_real_adder_class) {
        g_real_adder_class =
            AIBinder_Class_define(IRealAdder::descriptor(),
                                  on_create,
                                  on_destroy,
                                  on_transact);
    }
    return g_real_adder_class;
}

AIBinder* BnRealAdder::create(RealAdderCallbacks* callbacks)
{
    return AIBinder_new(clazz(), callbacks);
}

BpRealAdder::BpRealAdder(AIBinder* binder)
    : binder_(binder)
{
    if (binder_)
        AIBinder_incStrong(binder_);
}

BpRealAdder::~BpRealAdder()
{
    if (binder_)
        AIBinder_decStrong(binder_);
}

binder_status_t BpRealAdder::add(int32_t lhs,
                                 int32_t rhs,
                                 int32_t* out_sum)
{
    if (!binder_ || !out_sum)
        return STATUS_BAD_VALUE;

    AParcel* in = nullptr;
    AParcel* out = nullptr;
    AStatus* status = nullptr;
    binder_status_t rc = AIBinder_prepareTransaction(binder_, &in);
    if (rc != STATUS_OK)
        goto done;
    rc = AParcel_writeInt32(in, lhs);
    if (rc != STATUS_OK)
        goto done;
    rc = AParcel_writeInt32(in, rhs);
    if (rc != STATUS_OK)
        goto done;
    rc = AIBinder_transact(binder_, IRealAdder::TRANSACTION_add,
                           &in, &out, 0);
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

binder_status_t BpRealAdder::echoBinder(AIBinder* binder,
                                        AIBinder** out_binder)
{
    if (!binder_ || !out_binder)
        return STATUS_BAD_VALUE;

    AParcel* in = nullptr;
    AParcel* out = nullptr;
    AStatus* status = nullptr;
    binder_status_t rc = AIBinder_prepareTransaction(binder_, &in);
    if (rc != STATUS_OK)
        goto done;
    rc = AParcel_writeStrongBinder(in, binder);
    if (rc != STATUS_OK)
        goto done;
    rc = AIBinder_transact(binder_, IRealAdder::TRANSACTION_echoBinder,
                           &in, &out, 0);
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
    rc = AParcel_readStrongBinder(out, out_binder);

done:
    if (status)
        AStatus_delete(status);
    if (out)
        AParcel_delete(out);
    if (in)
        AParcel_delete(in);
    return rc;
}

} // namespace muplar
} // namespace example
} // namespace com
} // namespace aidl_generated
