// platform/android-aarch64/jni/jni_onload.h
//
// JniOnLoad handles the two-step .so boot sequence:
//
//   1. dlopen  — the guest calls dlopen("libfoo.so"), which lands as a
//                linux syscall (openat + mmap chain).  elfuse handles the
//                file I/O; we intercept the *result* and record the load base.
//
//   2. JNI_OnLoad — after the dynamic linker resolves the .so, the guest
//                   calls JNI_OnLoad(JavaVM*, void*).  We synthesise a
//                   JavaVM** and a JNIEnv** in guest memory, set PC to
//                   JNI_OnLoad's GPA, let the guest run, and collect
//                   RegisterNatives calls that come back out as HVC 0x1002.
//
// Integration point: call JniOnLoad::try_intercept() from the HVC exit
// handler in guest_runner.cpp *before* the normal Linux syscall dispatcher.
// It returns true (and fills X0) when it consumed the call.
#pragma once

#include "jni_bridge.h"
#include "jni_env.h"

extern "C" {
#include "core/guest.h"
#include "core/bootstrap.h"
}

#include <Hypervisor/Hypervisor.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace muplar::runtime::jni
{

// ── JavaVM layout in guest memory ───────────────────────────────────────────
//
// JNI_OnLoad's first argument is JavaVM* — a pointer to a pointer to a
// JNIInvokeInterface function table.  We write a minimal table with only the
// slots JNI_OnLoad actually touches:
//
//   slot 0  : reserved
//   slot 1  : reserved
//   slot 2  : reserved
//   slot 3  : DestroyJavaVM   (stub — returns JNI_OK)
//   slot 4  : AttachCurrentThread  → returns our JNIEnv*
//   slot 5  : DetachCurrentThread  (stub)
//   slot 6  : GetEnv               → returns our JNIEnv*
//   slot 7  : AttachCurrentThreadAsDaemon (stub)
//
// GPA layout written by JniOnLoad::install():
//   java_vm_table_gpa  : 8 * 8 bytes  — the JNIInvokeInterface table
//   java_vm_ptr_gpa    : 8 bytes      — points to java_vm_table_gpa
//                                       (JavaVM* = &java_vm_ptr_gpa)
//   jni_env_ptr_gpa    : 8 bytes      — points to JNIEnv table (from JniBridge)
//
static constexpr int JAVA_VM_TABLE_SLOTS = 8;

class JniOnLoad
{
public:
    // guest       : the running guest (must outlive JniOnLoad)
    // bridge      : already-installed JniBridge (JNIEnv* table is live)
    // env         : the JniEnv that bridge dispatches into
    // arena_gpa   : base GPA of a free scratch region for JavaVM tables.
    //               Needs at least 128 bytes.  Typically placed just after
    //               the JNINativeInterface table.
    JniOnLoad(guest_t *guest,
              JniBridge *bridge,
              JniEnv *env,
              uint64_t arena_gpa);

    // Write the JavaVM table and pointer into guest memory.
    // Must be called after JniBridge::install().
    void install();

    // Returns the GPA the guest should see as JavaVM* (first arg to
    // JNI_OnLoad).
    uint64_t java_vm_ptr_gpa() const { return java_vm_ptr_gpa_; }

    // Returns the GPA the guest sees as JNIEnv** (second arg to JNI_OnLoad).
    uint64_t jni_env_ptr_gpa() const { return jni_env_ptr_gpa_; }

    // Return stub used as LR when the host calls a guest function directly.
    uint64_t return_sentinel_gpa() const { return sentinel_stub_gpa_; }

    // Last value captured by the return sentinel.
    uint64_t last_return_value() const { return onload_retval_; }

    // Locate JNI_OnLoad in a loaded .so.
    // so_load_base : the GPA at which the .so was loaded by the dynamic linker.
    // so_path      : path on the host to the .so file (for symbol lookup).
    // Returns 0 if JNI_OnLoad is not exported.
    uint64_t find_jni_onload(uint64_t so_load_base, const std::string &so_path);

    // Locate any exported dynamic symbol in a loaded .so.
    uint64_t find_symbol(uint64_t so_load_base,
                         const std::string &so_path,
                         const std::string &symbol_name,
                         bool quiet = false);

    // Set up the vCPU to call JNI_OnLoad and run until it returns.
    //
    // Call this after find_jni_onload() returns a non-zero GPA.
    // Sets X0 = JavaVM*, X1 = reserved (0), PC = jni_onload_gpa,
    // SP = current guest SP, LR = a sentinel HVC so we know when it returns.
    //
    // vcpu / vexit must be the live vCPU handles from
    // guest_bootstrap_create_vcpu. run_loop_cb  : a callable that re-enters
    // vcpu_run_loop until our sentinel
    //               HVC fires.  Signature: int(hv_vcpu_t, hv_vcpu_exit_t*,
    //               guest_t*).
    //
    // Returns the int returned by JNI_OnLoad (should be JNI_VERSION_1_6).
    int call_jni_onload(
        uint64_t jni_onload_gpa,
        hv_vcpu_t vcpu,
        hv_vcpu_exit_t *vexit,
        std::function<int(hv_vcpu_t, hv_vcpu_exit_t *, guest_t *)> run_loop_cb);

    // Generic guest function call helper. Sets X0..X7 from args and uses the
    // same sentinel return path as JNI_OnLoad.
    int64_t call_guest_function(
        uint64_t entry_gpa,
        const std::vector<uint64_t> &args,
        hv_vcpu_t vcpu,
        hv_vcpu_exit_t *vexit,
        std::function<int(hv_vcpu_t, hv_vcpu_exit_t *, guest_t *)> run_loop_cb);

    // Smoke helper for registered JNI methods with signature (II)I.
    int call_native_int2(
        uint64_t native_gpa,
        uint64_t thiz,
        int a,
        int b,
        hv_vcpu_t vcpu,
        hv_vcpu_exit_t *vexit,
        std::function<int(hv_vcpu_t, hv_vcpu_exit_t *, guest_t *)> run_loop_cb);

    // Generic helper for registered/exported JNI methods with primitive
    // integer-like arguments. X0=JNIEnv*, X1=thiz, X2..X7=args.
    int64_t call_native(
        uint64_t native_gpa,
        uint64_t thiz,
        const std::vector<int64_t> &args,
        hv_vcpu_t vcpu,
        hv_vcpu_exit_t *vexit,
        std::function<int(hv_vcpu_t, hv_vcpu_exit_t *, guest_t *)> run_loop_cb);

    // HVC interceptor — call from the HVC exit handler before the normal
    // Linux syscall dispatcher.  Returns true and writes *x0_out if the HVC
    // was a JNI call (0x1000–0x10FF) or a JNI_OnLoad return sentinel (0x1FFF).
    bool try_intercept(uint32_t hvc_nr,
                       const uint64_t regs[8],
                       uint64_t *x0_out);

private:
    void write_u64(uint64_t gpa, uint64_t value);

    guest_t *guest_;
    JniBridge *bridge_;
    JniEnv *env_;

    uint64_t arena_gpa_;
    uint64_t java_vm_table_gpa_ = 0;
    uint64_t java_vm_ptr_gpa_ = 0;
    uint64_t jni_env_ptr_gpa_ = 0;

    // Sentinel HVC number the shim stub fires on LR to detect JNI_OnLoad return
    static constexpr uint32_t HVC_JNI_ONLOAD_RETURN = 0x1FFF;

    bool onload_returned_ = false;
    uint64_t onload_retval_ = 0;
    uint64_t sentinel_stub_gpa_ = 0;
};

}  // namespace muplar::runtime::jni
