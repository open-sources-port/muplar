// runtime/jni/jni_env.cpp
#include "jni_env.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace muplar::runtime::jni {

// ────────────────────────────────────────────────────────────────────────────
// JNI call numbers
// ────────────────────────────────────────────────────────────────────────────
static constexpr uint32_t JNI_GetVersion             = 0x1000;
static constexpr uint32_t JNI_FindClass              = 0x1001;
static constexpr uint32_t JNI_RegisterNatives        = 0x1002;
static constexpr uint32_t JNI_GetMethodID            = 0x1003;
static constexpr uint32_t JNI_CallVoidMethod         = 0x1004;
static constexpr uint32_t JNI_CallIntMethod          = 0x1005;
static constexpr uint32_t JNI_NewStringUTF           = 0x1006;
static constexpr uint32_t JNI_GetStringUTFChars      = 0x1007;
static constexpr uint32_t JNI_ReleaseStringUTFChars  = 0x1008;
static constexpr uint32_t JNI_NewByteArray           = 0x1009;
static constexpr uint32_t JNI_SetByteArrayRegion     = 0x100A;
static constexpr uint32_t JNI_GetByteArrayElements   = 0x100B;
static constexpr uint32_t JNI_ReleaseByteArrayElements = 0x100C;
static constexpr uint32_t JNI_ExceptionCheck         = 0x100D;
static constexpr uint32_t JNI_ExceptionClear         = 0x100E;
static constexpr uint32_t JNI_DeleteLocalRef         = 0x100F;
static constexpr uint32_t JNI_GetObjectClass         = 0x1010;
static constexpr uint32_t JNI_CallObjectMethod       = 0x1011;

// ────────────────────────────────────────────────────────────────────────────
JniEnv::JniEnv() = default;

// ────────────────────────────────────────────────────────────────────────────
uint64_t JniEnv::dispatch(uint32_t call_nr, const uint64_t args[8])
{
    switch (call_nr) {
    case JNI_GetVersion:             return jni_GetVersion(args);
    case JNI_FindClass:              return jni_FindClass(args);
    case JNI_RegisterNatives:        return jni_RegisterNatives(args);
    case JNI_GetMethodID:            return jni_GetMethodID(args);
    case JNI_CallVoidMethod:         return jni_CallVoidMethod(args);
    case JNI_CallIntMethod:          return jni_CallIntMethod(args);
    case JNI_NewStringUTF:           return jni_NewStringUTF(args);
    case JNI_GetStringUTFChars:      return jni_GetStringUTFChars(args);
    case JNI_ReleaseStringUTFChars:  return jni_ReleaseStringUTFChars(args);
    case JNI_NewByteArray:           return jni_NewByteArray(args);
    case JNI_SetByteArrayRegion:     return jni_SetByteArrayRegion(args);
    case JNI_GetByteArrayElements:   return jni_GetByteArrayElements(args);
    case JNI_ReleaseByteArrayElements: return jni_ReleaseByteArrayElements(args);
    case JNI_ExceptionCheck:         return jni_ExceptionCheck(args);
    case JNI_ExceptionClear:         return jni_ExceptionClear(args);
    case JNI_DeleteLocalRef:         return jni_DeleteLocalRef(args);
    case JNI_GetObjectClass:         return jni_GetObjectClass(args);
    case JNI_CallObjectMethod:       return jni_CallObjectMethod(args);
    default:
        std::fprintf(stderr, "[JNI] Unknown call 0x%X\n", call_nr);
        return static_cast<uint64_t>(-1);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// install_table — writes function-pointer stubs into guest memory.
//
// The JNINativeInterface ABI on AArch64 Android is a pointer to a struct
// whose first ~230 slots are function pointers.  We only populate the slots
// we implement; unused slots are zeroed (→ crash-on-call, easy to diagnose).
//
// Layout we write at table_gpa (8 bytes per slot):
//   slot 0  : reserved (NULL) — mirrors real JNI layout (p->reserved0..3)
//   slot 1  : reserved
//   slot 2  : reserved
//   slot 3  : reserved
//   slot 4  : GetVersion        stub GPA
//   slot 6  : FindClass         stub GPA
//   slot 17 : ExceptionClear    stub GPA
//   slot 23 : DeleteLocalRef    stub GPA
//   slot 31 : GetObjectClass    stub GPA
//   slot 33 : GetMethodID       stub GPA
//   slot 34 : CallObjectMethod  stub GPA
//   slot 49 : CallIntMethod     stub GPA
//   slot 61 : CallVoidMethod    stub GPA
//   slot 167: NewStringUTF      stub GPA
//   slot 169: GetStringUTFChars stub GPA
//   slot 170: ReleaseStringUTFChars stub GPA
//   slot 176: NewByteArray      stub GPA
//   slot 184: GetByteArrayElements stub GPA
//   slot 192: ReleaseByteArrayElements stub GPA
//   slot 208: SetByteArrayRegion stub GPA
//   slot 215: RegisterNatives   stub GPA
//   slot 228: ExceptionCheck    stub GPA
//
// Stubs live at stub_base_gpa + (call_nr - 0x1000) * JNI_STUB_SIZE.
// Each stub is `mov x8, #call_nr ; hvc #6 ; ret`.
// ────────────────────────────────────────────────────────────────────────────
static constexpr uint64_t JNI_STUB_SIZE = 12;

void JniEnv::install_table(uint64_t table_gpa, uint64_t stub_base_gpa)
{
    jni_table_gpa_ = table_gpa;

    // Helper: compute the GPA of the stub for a given call number.
    auto stub = [&](uint32_t call_nr) -> uint64_t {
        return stub_base_gpa + (call_nr - 0x1000) * JNI_STUB_SIZE;
    };

    // We store the slot index → stub GPA pairs.  The table itself is in guest
    // memory; we return a flat array that guest_runner must memcpy into the
    // guest at table_gpa.  We expose this via a simple callback pattern so
    // JniEnv has no direct dependency on guest_t.
    //
    // The caller (JniBridge / GuestRunner) is responsible for writing
    // install_entries() into guest memory.  See jni_bridge.cpp.
    (void)stub; // used by install_entries() below — declared here for docs
    std::fprintf(stderr,
        "[JNI] install_table: table_gpa=0x%llx stub_base=0x%llx\n",
        (unsigned long long)table_gpa,
        (unsigned long long)stub_base_gpa);
}

// Return a flat vector of (slot_index, stub_gpa) pairs — the bridge writes
// these into the guest JNINativeInterface table.
std::vector<std::pair<int,uint64_t>>
JniEnv::install_entries(uint64_t stub_base_gpa) const
{
    auto stub = [&](uint32_t call_nr) -> uint64_t {
        return stub_base_gpa + (call_nr - 0x1000) * JNI_STUB_SIZE;
    };
    return {
        {  4, stub(JNI_GetVersion)              },
        {  6, stub(JNI_FindClass)               },
        { 17, stub(JNI_ExceptionClear)          },
        { 23, stub(JNI_DeleteLocalRef)          },
        { 31, stub(JNI_GetObjectClass)          },
        { 33, stub(JNI_GetMethodID)             },
        { 34, stub(JNI_CallObjectMethod)        },
        { 49, stub(JNI_CallIntMethod)           },
        { 61, stub(JNI_CallVoidMethod)          },
        {167, stub(JNI_NewStringUTF)            },
        {169, stub(JNI_GetStringUTFChars)       },
        {170, stub(JNI_ReleaseStringUTFChars)   },
        {176, stub(JNI_NewByteArray)            },
        {184, stub(JNI_GetByteArrayElements)    },
        {192, stub(JNI_ReleaseByteArrayElements)},
        {208, stub(JNI_SetByteArrayRegion)      },
        {215, stub(JNI_RegisterNatives)         },
        {228, stub(JNI_ExceptionCheck)          },
    };
}

// ────────────────────────────────────────────────────────────────────────────
// Host-side class registry
// ────────────────────────────────────────────────────────────────────────────
void JniEnv::register_class(const std::string& descriptor)
{
    if (class_by_desc_.count(descriptor)) return;
    uint64_t h = alloc_handle();
    JClass cls;
    cls.descriptor = descriptor;
    classes_[h]            = std::move(cls);
    class_by_desc_[descriptor] = h;
}

jclass JniEnv::find_class(const std::string& descriptor)
{
    auto it = class_by_desc_.find(descriptor);
    return (it != class_by_desc_.end()) ? it->second : JNI_NULL;
}

jobject JniEnv::register_object(const std::string& class_descriptor)
{
    register_class(class_descriptor);
    uint64_t cls = find_class(class_descriptor);
    uint64_t obj = alloc_handle();
    objects_[obj] = { cls };
    return obj;
}

void JniEnv::set_app_context(std::string package_name,
                             std::string package_code_path)
{
    if (!package_name.empty())
        package_name_ = std::move(package_name);
    package_code_path_ = std::move(package_code_path);
}

uint64_t JniEnv::make_string(const std::string& value)
{
    uint64_t h = alloc_handle();
    strings_[h] = value;
    return h;
}

const std::string* JniEnv::get_string(uint64_t handle) const
{
    auto it = strings_.find(handle);
    return it == strings_.end() ? nullptr : &it->second;
}

// ────────────────────────────────────────────────────────────────────────────
// JNI call implementations
// Convention: args[0] = JNIEnv* (ignored — we are the env), args[1..] = real args
// ────────────────────────────────────────────────────────────────────────────

uint64_t JniEnv::jni_GetVersion(const uint64_t* /*a*/)
{
    return static_cast<uint64_t>(JNI_VERSION_1_6);
}

// args[1] = GPA of class descriptor string in guest memory
// We resolve via the string table set up by jni_NewStringUTF or directly.
// For FindClass the descriptor arrives as a raw UTF-8 pointer in guest memory.
// The bridge must call set_string_ptr() to let us read guest memory.
// For now we key on the raw GPA treated as an opaque descriptor ID: callers
// that registered via register_class("com/example/Foo") get back the handle.
uint64_t JniEnv::jni_FindClass(const uint64_t* a)
{
    // a[1] = GPA of null-terminated UTF-8 descriptor
    // We cannot dereference guest memory directly from here; the bridge
    // layer must intercept and supply the resolved string.  If it has done
    // so by pushing it into strings_ under the GPA as handle, look it up.
    uint64_t str_gpa = a[1];
    // Fast path: did the bridge pre-resolve this GPA as a string?
    auto sit = strings_.find(str_gpa);
    if (sit != strings_.end()) {
        auto cit = class_by_desc_.find(sit->second);
        if (cit != class_by_desc_.end()) return cit->second;

        // Until a Java class loader exists, synthesize a placeholder class so
        // JNI_OnLoad can register native methods against the descriptor.
        register_class(sit->second);
        return class_by_desc_[sit->second];
    }
    std::fprintf(stderr, "[JNI] FindClass: unresolved GPA 0x%llx\n",
                 (unsigned long long)str_gpa);
    pending_exception_ = true;
    return JNI_NULL;
}

// args[1] = jclass, args[2] = name GPA, args[3] = sig GPA
uint64_t JniEnv::jni_GetMethodID(const uint64_t* a)
{
    uint64_t cls_h    = a[1];
    uint64_t name_gpa = a[2];
    uint64_t sig_gpa  = a[3];

    auto cit = classes_.find(cls_h);
    if (cit == classes_.end()) {
        std::fprintf(stderr, "[JNI] GetMethodID: unknown class 0x%llx\n",
                     (unsigned long long)cls_h);
        pending_exception_ = true;
        return JNI_NULL;
    }

    // Resolve name and sig via string table
    std::string name, sig;
    auto ns = strings_.find(name_gpa);
    auto ss = strings_.find(sig_gpa);
    if (ns != strings_.end()) name = ns->second;
    if (ss != strings_.end()) sig  = ss->second;

    // Search existing methods first
    for (auto& [h, me] : methods_) {
        if (me.class_handle == cls_h && me.name == name && me.sig == sig)
            return h;
    }

    // Allocate a new method handle
    uint64_t mh = alloc_handle();
    methods_[mh] = { cls_h, name, sig };
    std::fprintf(stderr, "[JNI] GetMethodID: %s%s → 0x%llx\n",
                 name.c_str(), sig.c_str(), (unsigned long long)mh);
    return mh;
}

// args[1] = jclass, args[2] = GPA of JNINativeMethod array, args[3] = count
uint64_t JniEnv::jni_RegisterNatives(const uint64_t* a)
{
    uint64_t cls_h    = a[1];
    // args[2] and args[3]: the bridge must walk the guest JNINativeMethod[]
    // array and call register_native() for each entry.  We record the count.
    uint64_t count    = a[3];
    std::fprintf(stderr,
        "[JNI] RegisterNatives: class=0x%llx count=%llu (bridge must resolve)\n",
        (unsigned long long)cls_h, (unsigned long long)count);
    // Return JNI_OK; actual registration happens via register_native().
    return static_cast<uint64_t>(JNI_OK);
}

void JniEnv::register_native(uint64_t class_handle,
                              const std::string& name,
                              const std::string& sig,
                              uint64_t fnptr_guest)
{
    auto cit = classes_.find(class_handle);
    if (cit == classes_.end()) {
        std::fprintf(stderr, "[JNI] register_native: unknown class\n");
        return;
    }
    // Replace existing or add
    for (auto& m : cit->second.methods) {
        if (m.name == name && m.signature == sig) {
            m.fnptr_guest = fnptr_guest;
            return;
        }
    }
    cit->second.methods.push_back({ name, sig, fnptr_guest });
    std::fprintf(stderr, "[JNI] RegisterNative: %s.%s%s → GPA 0x%llx\n",
                 cit->second.descriptor.c_str(),
                 name.c_str(), sig.c_str(),
                 (unsigned long long)fnptr_guest);
}

uint64_t JniEnv::find_native(uint64_t class_handle,
                              const std::string& name,
                              const std::string& sig) const
{
    auto cit = classes_.find(class_handle);
    if (cit == classes_.end()) return 0;
    for (const auto& m : cit->second.methods) {
        if (m.name == name && m.signature == sig)
            return m.fnptr_guest;
    }
    return 0;
}

// args[1] = jobject, args[2] = jmethodID, args[3..] = method args
uint64_t JniEnv::jni_CallVoidMethod(const uint64_t* a)
{
    uint64_t mid = a[2];
    auto mit = methods_.find(mid);
    if (mit == methods_.end()) {
        std::fprintf(stderr, "[JNI] CallVoidMethod: unknown methodID\n");
        pending_exception_ = true;
        return 0;
    }
    std::fprintf(stderr, "[JNI] CallVoidMethod: %s (GPA dispatch TBD)\n",
                 mit->second.name.c_str());
    // TODO: push a call frame onto the guest vCPU via the bridge callback.
    return 0;
}

uint64_t JniEnv::jni_CallIntMethod(const uint64_t* a)
{
    uint64_t mid = a[2];
    auto mit = methods_.find(mid);
    if (mit == methods_.end()) {
        std::fprintf(stderr, "[JNI] CallIntMethod: unknown methodID\n");
        pending_exception_ = true;
        return static_cast<uint64_t>(-1);
    }
    std::fprintf(stderr, "[JNI] CallIntMethod: %s (GPA dispatch TBD)\n",
                 mit->second.name.c_str());
    return 0;
}

uint64_t JniEnv::jni_GetObjectClass(const uint64_t* a)
{
    uint64_t obj = a[1];
    auto oit = objects_.find(obj);
    if (oit == objects_.end()) {
        std::fprintf(stderr, "[JNI] GetObjectClass: unknown object 0x%llx\n",
                     (unsigned long long)obj);
        pending_exception_ = true;
        return JNI_NULL;
    }
    return oit->second.class_handle;
}

uint64_t JniEnv::jni_CallObjectMethod(const uint64_t* a)
{
    uint64_t mid = a[2];
    auto mit = methods_.find(mid);
    if (mit == methods_.end()) {
        std::fprintf(stderr, "[JNI] CallObjectMethod: unknown methodID\n");
        pending_exception_ = true;
        return JNI_NULL;
    }

    const MethodEntry& method = mit->second;
    if (method.name == "getPackageName" &&
        method.sig == "()Ljava/lang/String;") {
        return make_string(package_name_);
    }
    if ((method.name == "getPackageCodePath" ||
         method.name == "getPackageResourcePath") &&
        method.sig == "()Ljava/lang/String;") {
        return make_string(package_code_path_);
    }

    std::fprintf(stderr, "[JNI] CallObjectMethod: unsupported %s%s\n",
                 method.name.c_str(), method.sig.c_str());
    pending_exception_ = true;
    return JNI_NULL;
}

// args[1] = GPA of null-terminated UTF-8 string
uint64_t JniEnv::jni_NewStringUTF(const uint64_t* a)
{
    uint64_t str_gpa = a[1];
    // The bridge must have pre-loaded the string content into a side buffer.
    // We reuse the GPA as the handle; if the bridge called intern_string(),
    // the entry already exists.
    if (!strings_.count(str_gpa)) {
        strings_[str_gpa] = "(unresolved)";
    }
    return make_string(strings_[str_gpa]);
}

void JniEnv::intern_string(uint64_t gpa, const std::string& value)
{
    strings_[gpa] = value;
}

// args[1] = jstring, args[2] = isCopy* (ignored)
uint64_t JniEnv::jni_GetStringUTFChars(const uint64_t* a)
{
    uint64_t jstr = a[1];
    auto it = strings_.find(jstr);
    if (it == strings_.end()) {
        std::fprintf(stderr, "[JNI] GetStringUTFChars: unknown jstring\n");
        return JNI_NULL;
    }
    // Return the jstring handle itself; the bridge maps it to a host buffer GPA.
    std::fprintf(stderr, "[JNI] GetStringUTFChars: \"%s\"\n",
                 it->second.c_str());
    return jstr; // bridge resolves to a guest-readable GPA
}

uint64_t JniEnv::jni_ReleaseStringUTFChars(const uint64_t* /*a*/)
{
    // No-op for our arena-style allocator
    return 0;
}

// args[1] = length
uint64_t JniEnv::jni_NewByteArray(const uint64_t* a)
{
    uint64_t len = a[1];
    uint64_t h   = alloc_handle();
    byte_arrays_[h].assign(static_cast<size_t>(len), 0);
    return h;
}

// args[1] = jbyteArray, args[2] = start, args[3] = len, args[4] = GPA of src
uint64_t JniEnv::jni_SetByteArrayRegion(const uint64_t* a)
{
    uint64_t h     = a[1];
    uint64_t start = a[2];
    uint64_t len   = a[3];
    // args[4] is a guest pointer; the bridge must copy the bytes first.
    auto it = byte_arrays_.find(h);
    if (it == byte_arrays_.end()) {
        std::fprintf(stderr, "[JNI] SetByteArrayRegion: unknown array\n");
        pending_exception_ = true;
        return 1; // non-zero = error
    }
    if (start + len > it->second.size()) {
        std::fprintf(stderr, "[JNI] SetByteArrayRegion: out of bounds\n");
        pending_exception_ = true;
        return 1;
    }
    // The bridge fills the buffer via set_byte_array_region(); we just validate.
    std::fprintf(stderr, "[JNI] SetByteArrayRegion: handle=0x%llx start=%llu len=%llu\n",
                 (unsigned long long)h,
                 (unsigned long long)start,
                 (unsigned long long)len);
    return 0;
}

void JniEnv::set_byte_array_region(uint64_t handle,
                                    size_t start,
                                    const uint8_t* src,
                                    size_t len)
{
    auto it = byte_arrays_.find(handle);
    if (it == byte_arrays_.end()) return;
    if (start + len > it->second.size()) return;
    std::memcpy(it->second.data() + start, src, len);
}

const std::vector<uint8_t>* JniEnv::get_byte_array(uint64_t handle) const
{
    auto it = byte_arrays_.find(handle);
    return (it != byte_arrays_.end()) ? &it->second : nullptr;
}

// args[1] = jbyteArray, args[2] = isCopy* (ignored)
uint64_t JniEnv::jni_GetByteArrayElements(const uint64_t* a)
{
    // Returns handle; bridge maps to host data pointer
    return a[1];
}

uint64_t JniEnv::jni_ReleaseByteArrayElements(const uint64_t* /*a*/)
{
    return 0;
}

uint64_t JniEnv::jni_ExceptionCheck(const uint64_t* /*a*/)
{
    return pending_exception_ ? 1u : 0u;
}

uint64_t JniEnv::jni_ExceptionClear(const uint64_t* /*a*/)
{
    pending_exception_ = false;
    return 0;
}

uint64_t JniEnv::jni_DeleteLocalRef(const uint64_t* /*a*/)
{
    // No-op for arena allocator; future: reference-count if needed.
    return 0;
}

} // namespace muplar::runtime::jni
