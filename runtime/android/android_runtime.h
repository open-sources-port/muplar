// runtime/android/android_runtime.h
//
// Phase 4 — Android Runtime Stubs
//
// Provides host-side stub implementations for the Android system library
// symbols that a real .so expects to find at runtime.  These are registered
// into the Linker's builtin symbol table (add_builtin) so that when linker64
// resolves DT_NEEDED for libc.so, libandroid.so, liblog.so, etc., each
// symbol points to an HVC shim stub in guest memory instead of crashing.
//
// HVC call numbers used (above JNI range 0x1000–0x10FF):
//   0x2000–0x20FF : libc stubs  (malloc, free, pthread_*, etc.)
//   0x2100–0x21FF : liblog      (__android_log_print, etc.)
//   0x2200–0x22FF : libandroid  (ALooper, AChoreographer, etc.)
//   0x2300–0x23FF : libdl       (dlopen, dlsym, dlclose, dlerror)
//
// Integration:
//   1. Construct AndroidRuntime with the guest and a free GPA arena.
//   2. Call install() — writes HVC shim stubs into guest memory.
//   3. Pass builtin_symbols() to Linker::add_builtin() for each soname.
//   4. In the HVC exit handler, call try_dispatch() before the Linux syscall
//      dispatcher (same pattern as JniBridge).
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <functional>
#include <optional>

extern "C" {
    #include "core/guest.h"
}

namespace muplar::runtime::android {

using BuiltinSymbols = std::unordered_map<std::string, uint64_t>;

// ── Stub descriptor ───────────────────────────────────────────────────────────
// Each Android API symbol maps to one HVC call number and one host handler.
// Handler signature: args[0..7] = X0..X7 at the HVC site → return X0.
using StubHandler = std::function<uint64_t(guest_t*, const uint64_t[8])>;

struct StubEntry {
    std::string  soname;      // which library exports this symbol
    std::string  symbol;      // symbol name (mangled for C++)
    uint32_t     hvc_nr;      // HVC call number (unique across all stubs)
    StubHandler  handler;     // host-side implementation
};

// ── AndroidRuntime ────────────────────────────────────────────────────────────
class AndroidRuntime {
public:
    // guest      : the running guest (must outlive AndroidRuntime)
    // stub_arena_gpa : base GPA of a free region for HVC shim stubs.
    //                  Each stub is 12 bytes (movz x8; hvc #6; ret).
    //                  Need at least num_stubs * 8 bytes — 4 KB is plenty.
    AndroidRuntime(guest_t* guest, uint64_t stub_arena_gpa);

    // Write HVC shim stubs into guest memory and build the symbol tables.
    // Call once before Linker::load().
    void install();

    // Return the builtin symbol map for a given soname.
    // Pass to Linker::add_builtin(soname, symbols).
    // Returns empty map if soname is not known.
    BuiltinSymbols builtin_symbols(const std::string& soname) const;

    // All known sonames that have stubs.
    static constexpr const char* KNOWN_SONAMES[] = {
        "libc.so", "libm.so", "libdl.so", "liblog.so",
        "libandroid.so", "libstdc++.so", nullptr
    };

    // HVC dispatch — call from the HVC exit handler when X8 is in
    // [0x2000, 0x23FF].  Fills *x0_out and returns true if consumed.
    bool try_dispatch(uint32_t hvc_nr, const uint64_t regs[8], uint64_t* x0_out);

private:
    void register_libc_stubs();
    void register_liblog_stubs();
    void register_libandroid_stubs();
    void register_libdl_stubs();

    // Write one HVC shim stub at the next free slot in the arena.
    // Returns the GPA of the stub.
    uint64_t write_stub(uint32_t hvc_nr);

    // Register a stub and return its GPA.
    uint64_t add(const std::string& soname,
                 const std::string& symbol,
                 uint32_t           hvc_nr,
                 StubHandler        handler);

    guest_t*  guest_;
    uint64_t  arena_gpa_;
    uint64_t  next_stub_gpa_ = 0;  // set in install()
    bool      installed_     = false;

    // hvc_nr → handler
    std::unordered_map<uint32_t, StubHandler> handlers_;

    // soname → { symbol → stub_gpa }
    std::unordered_map<std::string, BuiltinSymbols> sym_tables_;

    // ── In-guest heap (bump allocator for malloc stubs) ───────────────────
    uint64_t heap_base_  = 0;
    uint64_t heap_bump_  = 0;
    static constexpr uint64_t HEAP_SIZE = 512 * 1024; // lives inside shim data

    // ── pthread handle table ──────────────────────────────────────────────
    struct PthreadEntry { uint64_t stack_gpa; uint64_t stack_size; };
    std::unordered_map<uint64_t /*handle*/, PthreadEntry> threads_;
    uint64_t next_thread_handle_ = 0x8000'0001ULL;

    // ── dlopen handle table ───────────────────────────────────────────────
    struct DlopenEntry { std::string path; uint64_t load_base; };
    std::unordered_map<uint64_t, DlopenEntry> dl_handles_;
    uint64_t next_dl_handle_ = 0x9000'0001ULL;
};

} // namespace muplar::runtime::android
