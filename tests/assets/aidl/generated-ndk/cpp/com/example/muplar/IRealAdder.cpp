/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /tmp/android-15/aidl --lang=ndk -o /tmp/aidl-ndk-out -h /tmp/aidl-ndk-hdr -I /Users/dbaotrung/personal/muplar/tests/assets/aidl/real /Users/dbaotrung/personal/muplar/tests/assets/aidl/real/com/example/muplar/IRealAdder.aidl
 */
#include "aidl/com/example/muplar/IRealAdder.h"

#include <android/binder_parcel_utils.h>
#include <aidl/com/example/muplar/BnRealAdder.h>
#include <aidl/com/example/muplar/BpRealAdder.h>

namespace aidl {
namespace com {
namespace example {
namespace muplar {
static binder_status_t _aidl_com_example_muplar_IRealAdder_onTransact(AIBinder* _aidl_binder, transaction_code_t _aidl_code, const AParcel* _aidl_in, AParcel* _aidl_out) {
  (void)_aidl_in;
  (void)_aidl_out;
  binder_status_t _aidl_ret_status = STATUS_UNKNOWN_TRANSACTION;
  std::shared_ptr<BnRealAdder> _aidl_impl = std::static_pointer_cast<BnRealAdder>(::ndk::ICInterface::asInterface(_aidl_binder));
  switch (_aidl_code) {
    case (FIRST_CALL_TRANSACTION + 0 /*add*/): {
      int32_t in_lhs;
      int32_t in_rhs;
      int32_t _aidl_return;

      _aidl_ret_status = ::ndk::AParcel_readData(_aidl_in, &in_lhs);
      if (_aidl_ret_status != STATUS_OK) break;

      _aidl_ret_status = ::ndk::AParcel_readData(_aidl_in, &in_rhs);
      if (_aidl_ret_status != STATUS_OK) break;

      ::ndk::ScopedAStatus _aidl_status = _aidl_impl->add(in_lhs, in_rhs, &_aidl_return);
      _aidl_ret_status = AParcel_writeStatusHeader(_aidl_out, _aidl_status.get());
      if (_aidl_ret_status != STATUS_OK) break;

      if (!AStatus_isOk(_aidl_status.get())) break;

      _aidl_ret_status = ::ndk::AParcel_writeData(_aidl_out, _aidl_return);
      if (_aidl_ret_status != STATUS_OK) break;

      break;
    }
    case (FIRST_CALL_TRANSACTION + 1 /*echoBinder*/): {
      ::ndk::SpAIBinder in_binder;
      ::ndk::SpAIBinder _aidl_return;

      _aidl_ret_status = ::ndk::AParcel_readData(_aidl_in, &in_binder);
      if (_aidl_ret_status != STATUS_OK) break;

      ::ndk::ScopedAStatus _aidl_status = _aidl_impl->echoBinder(in_binder, &_aidl_return);
      _aidl_ret_status = AParcel_writeStatusHeader(_aidl_out, _aidl_status.get());
      if (_aidl_ret_status != STATUS_OK) break;

      if (!AStatus_isOk(_aidl_status.get())) break;

      _aidl_ret_status = ::ndk::AParcel_writeData(_aidl_out, _aidl_return);
      if (_aidl_ret_status != STATUS_OK) break;

      break;
    }
  }
  return _aidl_ret_status;
}

static AIBinder_Class* _g_aidl_com_example_muplar_IRealAdder_clazz = ::ndk::ICInterface::defineClass(IRealAdder::descriptor, _aidl_com_example_muplar_IRealAdder_onTransact);

BpRealAdder::BpRealAdder(const ::ndk::SpAIBinder& binder) : BpCInterface(binder) {}
BpRealAdder::~BpRealAdder() {}

::ndk::ScopedAStatus BpRealAdder::add(int32_t in_lhs, int32_t in_rhs, int32_t* _aidl_return) {
  binder_status_t _aidl_ret_status = STATUS_OK;
  ::ndk::ScopedAStatus _aidl_status;
  ::ndk::ScopedAParcel _aidl_in;
  ::ndk::ScopedAParcel _aidl_out;

  _aidl_ret_status = AIBinder_prepareTransaction(asBinder().get(), _aidl_in.getR());
  if (_aidl_ret_status != STATUS_OK) goto _aidl_error;

  _aidl_ret_status = ::ndk::AParcel_writeData(_aidl_in.get(), in_lhs);
  if (_aidl_ret_status != STATUS_OK) goto _aidl_error;

  _aidl_ret_status = ::ndk::AParcel_writeData(_aidl_in.get(), in_rhs);
  if (_aidl_ret_status != STATUS_OK) goto _aidl_error;

  _aidl_ret_status = AIBinder_transact(
    asBinder().get(),
    (FIRST_CALL_TRANSACTION + 0 /*add*/),
    _aidl_in.getR(),
    _aidl_out.getR(),
    0
    #ifdef BINDER_STABILITY_SUPPORT
    | FLAG_PRIVATE_LOCAL
    #endif  // BINDER_STABILITY_SUPPORT
    );
  if (_aidl_ret_status == STATUS_UNKNOWN_TRANSACTION && IRealAdder::getDefaultImpl()) {
    _aidl_status = IRealAdder::getDefaultImpl()->add(in_lhs, in_rhs, _aidl_return);
    goto _aidl_status_return;
  }
  if (_aidl_ret_status != STATUS_OK) goto _aidl_error;

  _aidl_ret_status = AParcel_readStatusHeader(_aidl_out.get(), _aidl_status.getR());
  if (_aidl_ret_status != STATUS_OK) goto _aidl_error;

  if (!AStatus_isOk(_aidl_status.get())) goto _aidl_status_return;
  _aidl_ret_status = ::ndk::AParcel_readData(_aidl_out.get(), _aidl_return);
  if (_aidl_ret_status != STATUS_OK) goto _aidl_error;

  _aidl_error:
  _aidl_status.set(AStatus_fromStatus(_aidl_ret_status));
  _aidl_status_return:
  return _aidl_status;
}
::ndk::ScopedAStatus BpRealAdder::echoBinder(const ::ndk::SpAIBinder& in_binder, ::ndk::SpAIBinder* _aidl_return) {
  binder_status_t _aidl_ret_status = STATUS_OK;
  ::ndk::ScopedAStatus _aidl_status;
  ::ndk::ScopedAParcel _aidl_in;
  ::ndk::ScopedAParcel _aidl_out;

  _aidl_ret_status = AIBinder_prepareTransaction(asBinder().get(), _aidl_in.getR());
  if (_aidl_ret_status != STATUS_OK) goto _aidl_error;

  _aidl_ret_status = ::ndk::AParcel_writeData(_aidl_in.get(), in_binder);
  if (_aidl_ret_status != STATUS_OK) goto _aidl_error;

  _aidl_ret_status = AIBinder_transact(
    asBinder().get(),
    (FIRST_CALL_TRANSACTION + 1 /*echoBinder*/),
    _aidl_in.getR(),
    _aidl_out.getR(),
    0
    #ifdef BINDER_STABILITY_SUPPORT
    | FLAG_PRIVATE_LOCAL
    #endif  // BINDER_STABILITY_SUPPORT
    );
  if (_aidl_ret_status == STATUS_UNKNOWN_TRANSACTION && IRealAdder::getDefaultImpl()) {
    _aidl_status = IRealAdder::getDefaultImpl()->echoBinder(in_binder, _aidl_return);
    goto _aidl_status_return;
  }
  if (_aidl_ret_status != STATUS_OK) goto _aidl_error;

  _aidl_ret_status = AParcel_readStatusHeader(_aidl_out.get(), _aidl_status.getR());
  if (_aidl_ret_status != STATUS_OK) goto _aidl_error;

  if (!AStatus_isOk(_aidl_status.get())) goto _aidl_status_return;
  _aidl_ret_status = ::ndk::AParcel_readData(_aidl_out.get(), _aidl_return);
  if (_aidl_ret_status != STATUS_OK) goto _aidl_error;

  _aidl_error:
  _aidl_status.set(AStatus_fromStatus(_aidl_ret_status));
  _aidl_status_return:
  return _aidl_status;
}
// Source for BnRealAdder
BnRealAdder::BnRealAdder() {}
BnRealAdder::~BnRealAdder() {}
::ndk::SpAIBinder BnRealAdder::createBinder() {
  AIBinder* binder = AIBinder_new(_g_aidl_com_example_muplar_IRealAdder_clazz, static_cast<void*>(this));
  #ifdef BINDER_STABILITY_SUPPORT
  AIBinder_markCompilationUnitStability(binder);
  #endif  // BINDER_STABILITY_SUPPORT
  return ::ndk::SpAIBinder(binder);
}
// Source for IRealAdder
const char* IRealAdder::descriptor = "com.example.muplar.IRealAdder";
IRealAdder::IRealAdder() {}
IRealAdder::~IRealAdder() {}


std::shared_ptr<IRealAdder> IRealAdder::fromBinder(const ::ndk::SpAIBinder& binder) {
  if (!AIBinder_associateClass(binder.get(), _g_aidl_com_example_muplar_IRealAdder_clazz)) {
    #if __ANDROID_API__ >= 31
    const AIBinder_Class* originalClass = AIBinder_getClass(binder.get());
    if (originalClass == nullptr) return nullptr;
    if (0 == strcmp(AIBinder_Class_getDescriptor(originalClass), descriptor)) {
      return ::ndk::SharedRefBase::make<BpRealAdder>(binder);
    }
    #endif
    return nullptr;
  }
  std::shared_ptr<::ndk::ICInterface> interface = ::ndk::ICInterface::asInterface(binder.get());
  if (interface) {
    return std::static_pointer_cast<IRealAdder>(interface);
  }
  return ::ndk::SharedRefBase::make<BpRealAdder>(binder);
}

binder_status_t IRealAdder::writeToParcel(AParcel* parcel, const std::shared_ptr<IRealAdder>& instance) {
  return AParcel_writeStrongBinder(parcel, instance ? instance->asBinder().get() : nullptr);
}
binder_status_t IRealAdder::readFromParcel(const AParcel* parcel, std::shared_ptr<IRealAdder>* instance) {
  ::ndk::SpAIBinder binder;
  binder_status_t status = AParcel_readStrongBinder(parcel, binder.getR());
  if (status != STATUS_OK) return status;
  *instance = IRealAdder::fromBinder(binder);
  return STATUS_OK;
}
bool IRealAdder::setDefaultImpl(const std::shared_ptr<IRealAdder>& impl) {
  // Only one user of this interface can use this function
  // at a time. This is a heuristic to detect if two different
  // users in the same process use this function.
  assert(!IRealAdder::default_impl);
  if (impl) {
    IRealAdder::default_impl = impl;
    return true;
  }
  return false;
}
const std::shared_ptr<IRealAdder>& IRealAdder::getDefaultImpl() {
  return IRealAdder::default_impl;
}
std::shared_ptr<IRealAdder> IRealAdder::default_impl = nullptr;
::ndk::ScopedAStatus IRealAdderDefault::add(int32_t /*in_lhs*/, int32_t /*in_rhs*/, int32_t* /*_aidl_return*/) {
  ::ndk::ScopedAStatus _aidl_status;
  _aidl_status.set(AStatus_fromStatus(STATUS_UNKNOWN_TRANSACTION));
  return _aidl_status;
}
::ndk::ScopedAStatus IRealAdderDefault::echoBinder(const ::ndk::SpAIBinder& /*in_binder*/, ::ndk::SpAIBinder* /*_aidl_return*/) {
  ::ndk::ScopedAStatus _aidl_status;
  _aidl_status.set(AStatus_fromStatus(STATUS_UNKNOWN_TRANSACTION));
  return _aidl_status;
}
::ndk::SpAIBinder IRealAdderDefault::asBinder() {
  return ::ndk::SpAIBinder();
}
bool IRealAdderDefault::isRemote() {
  return false;
}
}  // namespace muplar
}  // namespace example
}  // namespace com
}  // namespace aidl
