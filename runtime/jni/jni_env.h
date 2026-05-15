// runtime/jni/jni_env.h
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace muplar::runtime::jni {

// Opaque guest-side JNI pointer types (GPA values stored as uint64_t)
using jobject   = uint64_t;
using jclass    = uint64_t;
using jmethodID = uint64_t;
using jfieldID  = uint64_t;
using jstring   = uint64_t;
using jarray    = uint64_t;
using jbyteArray = uint64_t;
using jintArray  = uint64_t;
using jlongArray = uint64_t;
using jvalue    = uint64_t;

static constexpr jobject JNI_NULL = 0;

// Mirror of JNI version constants
static constexpr int JNI_VERSION_1_6 = 0x00010006;

// JNI return codes
static constexpr int JNI_OK  =  0;
static constexpr int JNI_ERR = -1;

// ────────────────────────────────────────────────────────────────────────────
// Method descriptor resolved on the host side
// ────────────────────────────────────────────────────────────────────────────
struct NativeMethod {
    std::string name;
    std::string signature;
    uint64_t    fnptr_guest = 0;  // GPA of the registered JNI function
};

// ────────────────────────────────────────────────────────────────────────────
// A host-side representation of a Java class seen through JNI
// ────────────────────────────────────────────────────────────────────────────
struct JClass {
    std::string          descriptor;   // e.g. "com/example/Foo"
    std::vector<NativeMethod> methods;
};

// ────────────────────────────────────────────────────────────────────────────
// JNI environment exposed to the guest
//
// The guest binary calls JNI functions through a JNINativeInterface* table
// that is laid out in guest memory.  Each slot is a GPA pointing into the
// shim's dispatch region; the shim converts it into an HVC call that lands
// in JniEnv::dispatch() below.
//
// JNI call numbers (passed in X8 on the HVC path, distinct from Linux
// syscall numbers which use a different range):
//   0x1000  GetVersion
//   0x1001  FindClass
//   0x1002  RegisterNatives
//   0x1003  GetMethodID
//   0x1004  CallVoidMethod
//   0x1005  CallIntMethod
//   0x1006  NewStringUTF
//   0x1007  GetStringUTFChars
//   0x1008  ReleaseStringUTFChars
//   0x1009  NewByteArray
//   0x100A  SetByteArrayRegion
//   0x100B  GetByteArrayElements
//   0x100C  ReleaseByteArrayElements
//   0x100D  ExceptionCheck
//   0x100E  ExceptionClear
//   0x100F  DeleteLocalRef
// ────────────────────────────────────────────────────────────────────────────
class JniEnv {
public:
    JniEnv();

    // Dispatch a JNI call from the guest.
    // call_nr  : JNI call number (0x1000 … 0x100F)
    // args[0..7]: guest X0..X7 at the point of the HVC
    // Returns the value to write back into guest X0.
    uint64_t dispatch(uint32_t call_nr, const uint64_t args[8]);

    // Return the GPA where the JNINativeInterface table was written.
    // Call after attaching to a guest_t so the table is in guest memory.
    uint64_t jni_interface_gpa() const { return jni_table_gpa_; }

    // Write the JNINativeInterface function-pointer table into guest memory
    // at the given GPA.  Each slot contains a small shim stub GPA that
    // converts a function-pointer call into an HVC.  Call once per guest
    // after the shim stubs have been placed.
    void install_table(uint64_t table_gpa, uint64_t stub_base_gpa);

    // ── Host-side helpers called by the Android framework layer ─────────
    void   register_class(const std::string& descriptor);
    jclass find_class(const std::string& descriptor);

    // Called by JniBridge to resolve char* GPAs before dispatch
    void intern_string(uint64_t gpa, const std::string& value);

    // Called by JniBridge after RegisterNatives to walk guest array
    void register_native(uint64_t class_handle,
                         const std::string& name,
                         const std::string& sig,
                         uint64_t fnptr_guest);

    // Look up a registered native GPA by class+name+sig
    uint64_t find_native(uint64_t class_handle,
                         const std::string& name,
                         const std::string& sig) const;

    // Called by JniBridge after SetByteArrayRegion to copy guest bytes in
    void set_byte_array_region(uint64_t handle,
                                size_t start,
                                const uint8_t* src,
                                size_t len);

    // Read back a byte array buffer (e.g. to pass to host code)
    const std::vector<uint8_t>* get_byte_array(uint64_t handle) const;

    // Returns (slot_index, stub_gpa) pairs for JniBridge::install()
    std::vector<std::pair<int,uint64_t>> install_entries(uint64_t stub_base_gpa) const;

private:
    // ── JNI call implementations ─────────────────────────────────────────
    uint64_t jni_GetVersion(const uint64_t* a);
    uint64_t jni_FindClass(const uint64_t* a);
    uint64_t jni_RegisterNatives(const uint64_t* a);
    uint64_t jni_GetMethodID(const uint64_t* a);
    uint64_t jni_CallVoidMethod(const uint64_t* a);
    uint64_t jni_CallIntMethod(const uint64_t* a);
    uint64_t jni_NewStringUTF(const uint64_t* a);
    uint64_t jni_GetStringUTFChars(const uint64_t* a);
    uint64_t jni_ReleaseStringUTFChars(const uint64_t* a);
    uint64_t jni_NewByteArray(const uint64_t* a);
    uint64_t jni_SetByteArrayRegion(const uint64_t* a);
    uint64_t jni_GetByteArrayElements(const uint64_t* a);
    uint64_t jni_ReleaseByteArrayElements(const uint64_t* a);
    uint64_t jni_ExceptionCheck(const uint64_t* a);
    uint64_t jni_ExceptionClear(const uint64_t* a);
    uint64_t jni_DeleteLocalRef(const uint64_t* a);

    // ── Object / handle tables ────────────────────────────────────────────
    // Classes indexed by a host-assigned handle (= jclass value seen by guest)
    std::unordered_map<uint64_t, JClass>  classes_;      // handle → JClass
    std::unordered_map<std::string, uint64_t> class_by_desc_; // desc → handle

    // Methods indexed by a host-assigned handle (= jmethodID seen by guest)
    struct MethodEntry { uint64_t class_handle; std::string name; std::string sig; };
    std::unordered_map<uint64_t, MethodEntry> methods_;

    // Strings: jstring handle → UTF-8 content
    std::unordered_map<uint64_t, std::string> strings_;

    // Byte arrays: jbyteArray handle → byte buffer
    std::unordered_map<uint64_t, std::vector<uint8_t>> byte_arrays_;

    bool pending_exception_ = false;

    // GPA of the installed JNINativeInterface table in guest memory
    uint64_t jni_table_gpa_ = 0;

    // Monotonically increasing handle allocator
    uint64_t next_handle_ = 0x7000'0001ULL;
    uint64_t alloc_handle() { return next_handle_++; }
};

} // namespace muplar::runtime::jni