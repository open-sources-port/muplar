// runtime/jni/jni_bridge.h
//
// JniBridge sits between the elfuse guest_t (which owns guest memory) and
// JniEnv (which owns the JNI object/method tables).
//
// Responsibilities:
//   • Read null-terminated UTF-8 strings out of guest memory and intern them
//     into JniEnv before each JNI call that takes a char* argument.
//   • Write the JNINativeInterface function-pointer table into guest memory.
//   • Intercept HVC exits with call_nr in [0x1000, 0x10FF] and route them
//     to JniEnv::dispatch().
//   • Walk guest JNINativeMethod[] arrays for RegisterNatives.
//   • Map jbyteArray / jstring handles to guest-readable GPAs.
//
// Integration with GuestRunner:
//   In vcpu_run_loop (elfuse) the HVC exit handler checks X8.  If X8 is in
//   the JNI range it calls jni_bridge_handle_hvc() instead of the Linux
//   syscall dispatcher.  Wire that in guest_runner.cpp (see TODO comment
//   there) once JniBridge is built.
#pragma once

#include "jni_env.h"

extern "C" {
    #include "core/guest.h"   // guest_t, guest_read_str, guest_read, guest_write
}

#include <cstdint>
#include <string>

namespace muplar::runtime::jni {

// JNINativeMethod as laid out in AArch64 guest memory (3 × 8-byte pointers).
struct GuestJNINativeMethod {
    uint64_t name_gpa;      // char*
    uint64_t signature_gpa; // char*
    uint64_t fnptr_gpa;     // void*
};

class JniBridge {
public:
    // guest   : the running guest (must outlive JniBridge)
    // jni_env : the JniEnv to dispatch into
    // jni_table_gpa    : where in guest memory the JNINativeInterface* table lives
    // jni_stub_base_gpa: where the shim stubs start (see jni_env.h)
    JniBridge(guest_t* guest,
              JniEnv*  jni_env,
              uint64_t jni_table_gpa,
              uint64_t jni_stub_base_gpa);

    // Write the JNINativeInterface table into guest memory and register the GPA
    // back into JniEnv.
    void install();

    // Called from the HVC exit handler in GuestRunner when X8 is in [0x1000, 0x10FF].
    // vcpu_regs[0..7] = X0..X7 at the HVC site.
    // Returns the value to write back into X0.
    uint64_t handle_hvc(uint32_t call_nr, uint64_t vcpu_regs[8]);

private:
    // ── guest memory helpers ──────────────────────────────────────────────
    std::string read_str(uint64_t gpa);
    void        write_u64(uint64_t gpa, uint64_t value);
    uint64_t    rebase_if_needed(uint64_t gpa) const;

    // Pre-resolve any GPA arguments that are char* pointers before the call.
    void intern_char_arg(uint64_t gpa);
    uint64_t materialize_string_chars(uint64_t string_handle);
    uint64_t materialize_byte_array(uint64_t array_handle);
    uint64_t materialize_int_array(uint64_t array_handle);
    uint64_t materialize_long_array(uint64_t array_handle);
    uint64_t materialize_float_array(uint64_t array_handle);

    // Walk a guest JNINativeMethod[] and register each entry.
    void register_natives_from_guest(uint64_t class_handle,
                                     uint64_t array_gpa,
                                     uint64_t count);

    guest_t*  guest_;
    JniEnv*   env_;
    uint64_t  table_gpa_;
    uint64_t  stub_base_gpa_;
    uint64_t  string_chars_base_gpa_;
    uint64_t  string_chars_bump_ = 0;
    uint64_t  array_elements_base_gpa_;
    uint64_t  array_elements_bump_ = 0;
};

} // namespace muplar::runtime::jni
