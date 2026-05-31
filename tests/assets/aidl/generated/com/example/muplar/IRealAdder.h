#pragma once

#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <stdint.h>

namespace aidl_generated {
namespace com {
namespace example {
namespace muplar {

struct IRealAdder {
    enum : transaction_code_t {
        TRANSACTION_add = FIRST_CALL_TRANSACTION,
        TRANSACTION_echoBinder = FIRST_CALL_TRANSACTION + 1,
    };

    static const char* descriptor();
};

struct RealAdderCallbacks {
    void* cookie;
    binder_status_t (*add)(void* cookie,
                           int32_t lhs,
                           int32_t rhs,
                           int32_t* out_sum);
    binder_status_t (*echoBinder)(void* cookie,
                                  AIBinder* binder,
                                  AIBinder** out_binder);
    void (*onDestroy)(void* cookie);
};

class BnRealAdder {
public:
    static AIBinder* create(RealAdderCallbacks* callbacks);
    static const AIBinder_Class* clazz();
};

class BpRealAdder {
public:
    explicit BpRealAdder(AIBinder* binder);
    ~BpRealAdder();

    BpRealAdder(const BpRealAdder&) = delete;
    BpRealAdder& operator=(const BpRealAdder&) = delete;

    binder_status_t add(int32_t lhs, int32_t rhs, int32_t* out_sum);
    binder_status_t echoBinder(AIBinder* binder, AIBinder** out_binder);

    AIBinder* asBinder() const { return binder_; }

private:
    AIBinder* binder_;
};

} // namespace muplar
} // namespace example
} // namespace com
} // namespace aidl_generated
