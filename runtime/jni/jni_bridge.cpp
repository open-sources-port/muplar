// runtime/jni/jni_bridge.cpp
#include "jni_bridge.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" {
    #include "core/guest.h"
}

namespace muplar::runtime::jni {

// ── JNINativeMethod layout in AArch64 guest memory ──────────────────────────
// sizeof(GuestJNINativeMethod) = 24 (3 × 8-byte pointers, no padding)
static constexpr uint64_t JNI_STUB_SIZE = 12;

static void encode_hvc_stub(uint8_t* out, uint32_t call_nr)
{
    uint32_t movz = 0xD2800008u | ((call_nr & 0xFFFFu) << 5);
    uint32_t hvc  = 0xD4000002u | (6u << 5);
    uint32_t ret  = 0xD65F03C0u;
    std::memcpy(out + 0, &movz, 4);
    std::memcpy(out + 4, &hvc,  4);
    std::memcpy(out + 8, &ret,  4);
}

JniBridge::JniBridge(guest_t* guest,
                     JniEnv*  jni_env,
                     uint64_t jni_table_gpa,
                     uint64_t jni_stub_base_gpa)
    : guest_(guest)
    , env_(jni_env)
    , table_gpa_(jni_table_gpa)
    , stub_base_gpa_(jni_stub_base_gpa)
    , string_chars_base_gpa_(jni_stub_base_gpa + 0x800)
{}

// ────────────────────────────────────────────────────────────────────────────
std::string JniBridge::read_str(uint64_t gpa)
{
    char buf[512] = {};
    guest_read_str(guest_, gpa, buf, sizeof(buf));
    return { buf };
}

void JniBridge::write_u64(uint64_t gpa, uint64_t value)
{
    guest_write(guest_, gpa, &value, sizeof(value));
}

uint64_t JniBridge::rebase_if_needed(uint64_t gpa) const
{
    if (!gpa || !guest_->elf_load_min) return gpa;
    return gpa < guest_->elf_load_min ? guest_->elf_load_min + gpa : gpa;
}

// ────────────────────────────────────────────────────────────────────────────
// install — write the JNINativeInterface slot table into guest memory.
//
// The JNINativeInterface is a struct of ~230 function pointers.  Android's
// ART lays them out in jni.h order.  We write only the slots we implement;
// all others remain zero (→ crash-on-call = diagnosable missing coverage).
//
// Table address at table_gpa_; each slot is 8 bytes.
// Stubs at stub_base_gpa_ + (call_nr - 0x1000) * JNI_STUB_SIZE.
// ────────────────────────────────────────────────────────────────────────────
void JniBridge::install()
{
    // Zero the Android JNI table through JNI 1.6's GetObjectRefType slot.
    static constexpr int SLOTS = 233;
    uint8_t zeroes[SLOTS * 8] = {};
    guest_write(guest_, table_gpa_, zeroes, sizeof(zeroes));

    // Write each implemented function stub and table slot.
    for (auto& [slot, stub_gpa] : env_->install_entries(stub_base_gpa_)) {
        uint32_t call_nr =
            0x1000u + static_cast<uint32_t>((stub_gpa - stub_base_gpa_) / JNI_STUB_SIZE);
        uint8_t stub[JNI_STUB_SIZE] = {};
        encode_hvc_stub(stub, call_nr);
        guest_write(guest_, stub_gpa, stub, sizeof(stub));
        write_u64(table_gpa_ + static_cast<uint64_t>(slot) * 8, stub_gpa);
    }

    // Let JniEnv record the GPA
    env_->install_table(table_gpa_, stub_base_gpa_);

    std::fprintf(stderr,
        "[JniBridge] JNINativeInterface table installed at GPA 0x%llx\n",
        (unsigned long long)table_gpa_);
}

// ────────────────────────────────────────────────────────────────────────────
// intern_char_arg — read a guest char* GPA and push it into JniEnv's string
// table so FindClass / GetMethodID / NewStringUTF can resolve it.
// ────────────────────────────────────────────────────────────────────────────
void JniBridge::intern_char_arg(uint64_t gpa)
{
    if (!gpa) return;
    std::string s = read_str(gpa);
    env_->intern_string(gpa, s);
}

uint64_t JniBridge::materialize_string_chars(uint64_t string_handle)
{
    const std::string* value = env_->get_string(string_handle);
    if (!value)
        return 0;

    constexpr uint64_t kStringScratchSize = 0x700;
    uint64_t len = static_cast<uint64_t>(value->size() + 1);
    if (len > kStringScratchSize)
        len = kStringScratchSize;
    if (string_chars_bump_ + len > kStringScratchSize)
        string_chars_bump_ = 0;

    uint64_t gpa = string_chars_base_gpa_ + string_chars_bump_;
    std::string tmp = value->substr(0, static_cast<size_t>(len - 1));
    tmp.push_back('\0');
    guest_write(guest_, gpa, tmp.data(), tmp.size());

    string_chars_bump_ += (len + 7) & ~7ULL;
    return gpa;
}

// ────────────────────────────────────────────────────────────────────────────
// register_natives_from_guest — walk a guest JNINativeMethod[] array.
//
// JNINativeMethod in guest memory (AArch64, jni.h):
//   +0  const char* name       (8 bytes)
//   +8  const char* signature  (8 bytes)
//  +16  void*       fnPtr      (8 bytes)
// Total = 24 bytes per entry.
// ────────────────────────────────────────────────────────────────────────────
void JniBridge::register_natives_from_guest(uint64_t class_handle,
                                             uint64_t array_gpa,
                                             uint64_t count)
{
    static constexpr uint64_t ENTRY_SIZE = 24;

    for (uint64_t i = 0; i < count; ++i) {
        uint64_t entry_gpa = array_gpa + i * ENTRY_SIZE;

        uint64_t name_gpa = 0, sig_gpa = 0, fn_gpa = 0;
        guest_read(guest_, entry_gpa + 0,  &name_gpa, 8);
        guest_read(guest_, entry_gpa + 8,  &sig_gpa,  8);
        guest_read(guest_, entry_gpa + 16, &fn_gpa,   8);

        name_gpa = rebase_if_needed(name_gpa);
        sig_gpa  = rebase_if_needed(sig_gpa);
        fn_gpa   = rebase_if_needed(fn_gpa);

        std::string name = read_str(name_gpa);
        std::string sig  = read_str(sig_gpa);

        env_->register_native(class_handle, name, sig, fn_gpa);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// handle_hvc — main dispatch called from the HVC exit handler.
//
// call_nr: X8 at the HVC site (0x1000–0x10FF)
// regs:    X0..X7 at the HVC site
// Returns: value to write back into X0.
// ────────────────────────────────────────────────────────────────────────────
uint64_t JniBridge::handle_hvc(uint32_t call_nr, uint64_t regs[8])
{
    // ── Pre-resolve char* arguments into JniEnv's string table ───────────
    // We resolve by call_nr so we know which args are pointers.
    switch (call_nr) {
    case 0x1001: // FindClass(env, name*)
        intern_char_arg(regs[1]);
        break;
    case 0x1003: // GetMethodID(env, class, name*, sig*)
        intern_char_arg(regs[2]);
        intern_char_arg(regs[3]);
        break;
    case 0x1006: // NewStringUTF(env, utf*)
        intern_char_arg(regs[1]);
        break;
    default:
        break;
    }

    // ── Dispatch ─────────────────────────────────────────────────────────
    uint64_t ret = env_->dispatch(call_nr, regs);

    if (call_nr == 0x1007) { // GetStringUTFChars(env, jstring, isCopy*)
        if (regs[2]) {
            uint8_t is_copy = 0;
            guest_write(guest_, regs[2], &is_copy, sizeof(is_copy));
        }
        ret = materialize_string_chars(ret);
    }

    // ── Post-dispatch: RegisterNatives needs guest memory walk ────────────
    if (call_nr == 0x1002) { // RegisterNatives(env, class, methods*, count)
        uint64_t class_handle = regs[1];
        uint64_t array_gpa    = regs[2];
        uint64_t count        = regs[3];
        register_natives_from_guest(class_handle, array_gpa, count);
    }

    // ── Post-dispatch: SetByteArrayRegion needs guest→host copy ──────────
    if (call_nr == 0x100A) { // SetByteArrayRegion(env, arr, start, len, buf*)
        uint64_t handle = regs[1];
        uint64_t start  = regs[2];
        uint64_t len    = regs[3];
        uint64_t buf_gpa = regs[4];

        if (len > 0 && buf_gpa) {
            std::vector<uint8_t> tmp(static_cast<size_t>(len));
            if (guest_read(guest_, buf_gpa, tmp.data(), len) == 0)
                env_->set_byte_array_region(handle,
                                             static_cast<size_t>(start),
                                             tmp.data(),
                                             static_cast<size_t>(len));
        }
    }

    return ret;
}

} // namespace muplar::runtime::jni
