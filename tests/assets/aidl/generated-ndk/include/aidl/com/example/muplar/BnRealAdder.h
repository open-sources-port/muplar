/*
 * This file is auto-generated.  DO NOT MODIFY.
 * Using: /tmp/android-15/aidl --lang=ndk -o /tmp/aidl-ndk-out -h /tmp/aidl-ndk-hdr -I /Users/dbaotrung/personal/muplar/tests/assets/aidl/real /Users/dbaotrung/personal/muplar/tests/assets/aidl/real/com/example/muplar/IRealAdder.aidl
 */
#pragma once

#include "aidl/com/example/muplar/IRealAdder.h"

#include <android/binder_ibinder.h>
#include <cassert>

#ifndef __BIONIC__
#ifndef __assert2
#define __assert2(a,b,c,d) ((void)0)
#endif
#endif

namespace aidl {
namespace com {
namespace example {
namespace muplar {
class BnRealAdder : public ::ndk::BnCInterface<IRealAdder> {
public:
  BnRealAdder();
  virtual ~BnRealAdder();
protected:
  ::ndk::SpAIBinder createBinder() override;
private:
};
class IRealAdderDelegator : public BnRealAdder {
public:
  explicit IRealAdderDelegator(const std::shared_ptr<IRealAdder> &impl) : _impl(impl) {
  }

  ::ndk::ScopedAStatus add(int32_t in_lhs, int32_t in_rhs, int32_t* _aidl_return) override {
    return _impl->add(in_lhs, in_rhs, _aidl_return);
  }
  ::ndk::ScopedAStatus echoBinder(const ::ndk::SpAIBinder& in_binder, ::ndk::SpAIBinder* _aidl_return) override {
    return _impl->echoBinder(in_binder, _aidl_return);
  }
protected:
private:
  std::shared_ptr<IRealAdder> _impl;
};

}  // namespace muplar
}  // namespace example
}  // namespace com
}  // namespace aidl
