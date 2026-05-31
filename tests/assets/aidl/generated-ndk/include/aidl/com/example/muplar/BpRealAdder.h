/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /tmp/android-15/aidl --lang=ndk -o /tmp/aidl-ndk-out -h /tmp/aidl-ndk-hdr -I /Users/dbaotrung/personal/muplar/tests/assets/aidl/real /Users/dbaotrung/personal/muplar/tests/assets/aidl/real/com/example/muplar/IRealAdder.aidl
 */
#pragma once

#include "aidl/com/example/muplar/IRealAdder.h"

#include <android/binder_ibinder.h>

namespace aidl {
namespace com {
namespace example {
namespace muplar {
class BpRealAdder : public ::ndk::BpCInterface<IRealAdder> {
public:
  explicit BpRealAdder(const ::ndk::SpAIBinder& binder);
  virtual ~BpRealAdder();

  ::ndk::ScopedAStatus add(int32_t in_lhs, int32_t in_rhs, int32_t* _aidl_return) override;
  ::ndk::ScopedAStatus echoBinder(const ::ndk::SpAIBinder& in_binder, ::ndk::SpAIBinder* _aidl_return) override;
};
}  // namespace muplar
}  // namespace example
}  // namespace com
}  // namespace aidl
