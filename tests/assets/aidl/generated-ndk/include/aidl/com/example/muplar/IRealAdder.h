/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /tmp/android-15/aidl --lang=ndk -o /tmp/aidl-ndk-out -h /tmp/aidl-ndk-hdr -I /Users/dbaotrung/personal/muplar/tests/assets/aidl/real /Users/dbaotrung/personal/muplar/tests/assets/aidl/real/com/example/muplar/IRealAdder.aidl
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <android/binder_interface_utils.h>
#ifdef BINDER_STABILITY_SUPPORT
#include <android/binder_stability.h>
#endif  // BINDER_STABILITY_SUPPORT

namespace aidl {
namespace com {
namespace example {
namespace muplar {
class IRealAdderDelegator;

class IRealAdder : public ::ndk::ICInterface {
public:
  typedef IRealAdderDelegator DefaultDelegator;
  static const char* descriptor;
  IRealAdder();
  virtual ~IRealAdder();

  static constexpr uint32_t TRANSACTION_add = FIRST_CALL_TRANSACTION + 0;
  static constexpr uint32_t TRANSACTION_echoBinder = FIRST_CALL_TRANSACTION + 1;

  static std::shared_ptr<IRealAdder> fromBinder(const ::ndk::SpAIBinder& binder);
  static binder_status_t writeToParcel(AParcel* parcel, const std::shared_ptr<IRealAdder>& instance);
  static binder_status_t readFromParcel(const AParcel* parcel, std::shared_ptr<IRealAdder>* instance);
  static bool setDefaultImpl(const std::shared_ptr<IRealAdder>& impl);
  static const std::shared_ptr<IRealAdder>& getDefaultImpl();
  virtual ::ndk::ScopedAStatus add(int32_t in_lhs, int32_t in_rhs, int32_t* _aidl_return) = 0;
  virtual ::ndk::ScopedAStatus echoBinder(const ::ndk::SpAIBinder& in_binder, ::ndk::SpAIBinder* _aidl_return) = 0;
private:
  static std::shared_ptr<IRealAdder> default_impl;
};
class IRealAdderDefault : public IRealAdder {
public:
  ::ndk::ScopedAStatus add(int32_t in_lhs, int32_t in_rhs, int32_t* _aidl_return) override;
  ::ndk::ScopedAStatus echoBinder(const ::ndk::SpAIBinder& in_binder, ::ndk::SpAIBinder* _aidl_return) override;
  ::ndk::SpAIBinder asBinder() override;
  bool isRemote() override;
};
}  // namespace muplar
}  // namespace example
}  // namespace com
}  // namespace aidl
