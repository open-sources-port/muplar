// runtime/android/android_runtime.cpp
#include "android_runtime.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>

extern "C" {
    #include "core/guest.h"
}

namespace muplar::runtime::android {

// ── HVC call number ranges ────────────────────────────────────────────────────
// libc  : 0x2000–0x20FF
// liblog: 0x2100–0x21FF
// libandroid: 0x2200–0x22FF
// libdl : 0x2300–0x23FF

// libc
static constexpr uint32_t HVC_MALLOC              = 0x2000;
static constexpr uint32_t HVC_FREE                = 0x2001;
static constexpr uint32_t HVC_CALLOC              = 0x2002;
static constexpr uint32_t HVC_REALLOC             = 0x2003;
static constexpr uint32_t HVC_MEMCPY              = 0x2004;
static constexpr uint32_t HVC_MEMMOVE             = 0x2005;
static constexpr uint32_t HVC_MEMSET              = 0x2006;
static constexpr uint32_t HVC_MEMCMP              = 0x2007;
static constexpr uint32_t HVC_STRLEN              = 0x2008;
static constexpr uint32_t HVC_STRCMP              = 0x2009;
static constexpr uint32_t HVC_STRNCMP             = 0x200A;
static constexpr uint32_t HVC_STRCPY              = 0x200B;
static constexpr uint32_t HVC_STRNCPY             = 0x200C;
static constexpr uint32_t HVC_STRCAT              = 0x200D;
static constexpr uint32_t HVC_STRDUP              = 0x200E;
[[maybe_unused]] static constexpr uint32_t HVC_SPRINTF             = 0x200F;
[[maybe_unused]] static constexpr uint32_t HVC_SNPRINTF            = 0x2010;
static constexpr uint32_t HVC_PRINTF              = 0x2011;
static constexpr uint32_t HVC_ABORT               = 0x2012;
static constexpr uint32_t HVC_EXIT                = 0x2013;
static constexpr uint32_t HVC_PTHREAD_CREATE      = 0x2020;
static constexpr uint32_t HVC_PTHREAD_JOIN        = 0x2021;
static constexpr uint32_t HVC_PTHREAD_MUTEX_INIT  = 0x2022;
static constexpr uint32_t HVC_PTHREAD_MUTEX_LOCK  = 0x2023;
static constexpr uint32_t HVC_PTHREAD_MUTEX_UNLOCK= 0x2024;
static constexpr uint32_t HVC_PTHREAD_MUTEX_DESTROY=0x2025;
static constexpr uint32_t HVC_PTHREAD_KEY_CREATE  = 0x2026;
static constexpr uint32_t HVC_PTHREAD_GETSPECIFIC = 0x2027;
static constexpr uint32_t HVC_PTHREAD_SETSPECIFIC = 0x2028;
static constexpr uint32_t HVC_PTHREAD_ONCE        = 0x2029;
static constexpr uint32_t HVC_PTHREAD_SELF        = 0x202A;
static constexpr uint32_t HVC_GETPID              = 0x2030;
static constexpr uint32_t HVC_GETENV              = 0x2031;
static constexpr uint32_t HVC_CLOCK_GETTIME       = 0x2032;
static constexpr uint32_t HVC_GETTIMEOFDAY        = 0x2033;
static constexpr uint32_t HVC_USLEEP              = 0x2034;
static constexpr uint32_t HVC_NANOSLEEP           = 0x2035;
[[maybe_unused]] static constexpr uint32_t HVC_STRTOL              = 0x2036;
[[maybe_unused]] static constexpr uint32_t HVC_STRTOD              = 0x2037;
static constexpr uint32_t HVC_ATOI                = 0x2038;
[[maybe_unused]] static constexpr uint32_t HVC_ATOF                = 0x2039;
static constexpr uint32_t HVC_RAND                = 0x203A;
static constexpr uint32_t HVC_SRAND               = 0x203B;

// liblog
static constexpr uint32_t HVC_LOG_PRINT           = 0x2100;
static constexpr uint32_t HVC_LOG_WRITE           = 0x2101;
static constexpr uint32_t HVC_LOG_BUF_WRITE       = 0x2102;

// libandroid
static constexpr uint32_t HVC_ALOOPER_PREPARE     = 0x2200;
static constexpr uint32_t HVC_ALOOPER_ACQUIRE     = 0x2201;
static constexpr uint32_t HVC_ALOOPER_RELEASE     = 0x2202;
static constexpr uint32_t HVC_ALOOPER_POLL_ONCE   = 0x2203;
static constexpr uint32_t HVC_ALOOPER_POLL_ALL    = 0x2204;
static constexpr uint32_t HVC_ALOOPER_ADD_FD      = 0x2205;
static constexpr uint32_t HVC_ALOOPER_REMOVE_FD   = 0x2206;
static constexpr uint32_t HVC_ALOOPER_WAKE        = 0x2207;
[[maybe_unused]] static constexpr uint32_t HVC_ASSET_MGR_OPEN      = 0x2210;
[[maybe_unused]] static constexpr uint32_t HVC_ASSET_OPEN          = 0x2211;
[[maybe_unused]] static constexpr uint32_t HVC_ASSET_READ          = 0x2212;
[[maybe_unused]] static constexpr uint32_t HVC_ASSET_CLOSE         = 0x2213;
[[maybe_unused]] static constexpr uint32_t HVC_ASSET_LENGTH        = 0x2214;
static constexpr uint32_t HVC_CHOREOGRAPHER_GET   = 0x2220;
static constexpr uint32_t HVC_CHOREOGRAPHER_CB    = 0x2221;
static constexpr uint32_t HVC_NATIVE_WINDOW_SET_BUF= 0x2230;
static constexpr uint32_t HVC_NATIVE_WINDOW_LOCK  = 0x2231;
static constexpr uint32_t HVC_NATIVE_WINDOW_UNLOCK= 0x2232;
static constexpr uint32_t HVC_PROP_GET            = 0x2240;

// libdl
static constexpr uint32_t HVC_DLOPEN              = 0x2300;
static constexpr uint32_t HVC_DLSYM               = 0x2301;
static constexpr uint32_t HVC_DLCLOSE             = 0x2302;
static constexpr uint32_t HVC_DLERROR             = 0x2303;

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string guest_read_string(guest_t* g, uint64_t gpa)
{
    if (!gpa) return {};
    char buf[512] = {};
    guest_read_str(g, gpa, buf, sizeof(buf));
    return { buf };
}

static void guest_write_u64(guest_t* g, uint64_t gpa, uint64_t v)
{
    guest_write(g, gpa, &v, 8);
}

static uint64_t guest_read_u64(guest_t* g, uint64_t gpa)
{
    uint64_t v = 0;
    guest_read(g, gpa, &v, 8);
    return v;
}

// ── AArch64 HVC shim stub layout (8 bytes) ────────────────────────────────────
//   movz x8, #<hvc_nr>     ; 4 bytes
//   hvc  #5                ; 4 bytes
// The elfuse shim catches HVC #5, reads X8 as the call number, dispatches.
static void encode_stub(uint8_t* out, uint32_t hvc_nr)
{
    // MOVZ X8, #imm16  — encoding: 1_10_100101_00_<imm16>_01000
    uint32_t movz = 0xD2800008u | ((hvc_nr & 0xFFFF) << 5);
    // HVC #5           — encoding: 1101 0100 000 <imm16> 00010  imm16=5
    uint32_t hvc  = 0xD4000002u | (5u << 5);
    memcpy(out + 0, &movz, 4);
    memcpy(out + 4, &hvc,  4);
}

// ── AndroidRuntime ────────────────────────────────────────────────────────────

AndroidRuntime::AndroidRuntime(guest_t* guest, uint64_t stub_arena_gpa)
    : guest_(guest), arena_gpa_(stub_arena_gpa)
{}

uint64_t AndroidRuntime::write_stub(uint32_t hvc_nr)
{
    uint8_t stub[8];
    encode_stub(stub, hvc_nr);
    uint64_t gpa = next_stub_gpa_;
    guest_write(guest_, gpa, stub, 8);
    next_stub_gpa_ += 8;
    return gpa;
}

uint64_t AndroidRuntime::add(const std::string& soname,
                              const std::string& symbol,
                              uint32_t           hvc_nr,
                              StubHandler        handler)
{
    uint64_t gpa = write_stub(hvc_nr);
    handlers_[hvc_nr] = std::move(handler);
    sym_tables_[soname][symbol] = gpa;
    return gpa;
}

void AndroidRuntime::install()
{
    if (installed_) return;
    installed_     = true;
    next_stub_gpa_ = arena_gpa_;

    // Carve out a heap region just after the stub area.
    // Reserve 2 KB for stubs (250 stubs × 8 bytes), then heap.
    heap_base_ = arena_gpa_ + 2048;
    heap_bump_ = heap_base_;

    register_libc_stubs();
    register_liblog_stubs();
    register_libandroid_stubs();
    register_libdl_stubs();

    std::fprintf(stderr,
        "[ART] installed %zu stub sonames, %zu handlers, arena=0x%llx\n",
        sym_tables_.size(), handlers_.size(),
        (unsigned long long)arena_gpa_);
}

BuiltinSymbols AndroidRuntime::builtin_symbols(const std::string& soname) const
{
    auto it = sym_tables_.find(soname);
    return (it != sym_tables_.end()) ? it->second : BuiltinSymbols{};
}

bool AndroidRuntime::try_dispatch(uint32_t        hvc_nr,
                                   const uint64_t  regs[8],
                                   uint64_t*       x0_out)
{
    if (hvc_nr < 0x2000 || hvc_nr > 0x23FF) return false;
    auto it = handlers_.find(hvc_nr);
    if (it == handlers_.end()) {
        std::fprintf(stderr, "[ART] unhandled HVC 0x%X\n", hvc_nr);
        *x0_out = 0;
        return true;
    }
    *x0_out = it->second(guest_, regs);
    return true;
}

// ── libc stubs ────────────────────────────────────────────────────────────────

void AndroidRuntime::register_libc_stubs()
{
    // malloc — bump allocator in guest memory
    add("libc.so", "malloc", HVC_MALLOC,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            uint64_t size = a[0];
            if (!size) return 0;
            size = (size + 15) & ~15ULL; // 16-byte align
            if (heap_bump_ + size > heap_base_ + HEAP_SIZE) {
                std::fprintf(stderr, "[ART] malloc: heap exhausted\n");
                return 0;
            }
            uint64_t ptr = heap_bump_;
            heap_bump_ += size;
            return ptr;
        });

    add("libc.so", "calloc", HVC_CALLOC,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            uint64_t nmemb = a[0], sz = a[1];
            uint64_t total = nmemb * sz;
            if (!total) return 0;
            total = (total + 15) & ~15ULL;
            if (heap_bump_ + total > heap_base_ + HEAP_SIZE) return 0;
            uint64_t ptr = heap_bump_;
            heap_bump_ += total;
            // Zero the region in guest memory
            std::vector<uint8_t> zeroes(static_cast<size_t>(total), 0);
            guest_write(g, ptr, zeroes.data(), total);
            return ptr;
        });

    add("libc.so", "realloc", HVC_REALLOC,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            // Simple: allocate new, don't copy (caller must not rely on old data)
            uint64_t size = a[1];
            if (!size) return 0;
            size = (size + 15) & ~15ULL;
            if (heap_bump_ + size > heap_base_ + HEAP_SIZE) return 0;
            uint64_t ptr = heap_bump_;
            heap_bump_ += size;
            return ptr;
        });

    add("libc.so", "free", HVC_FREE,
        [](guest_t*, const uint64_t[8]) -> uint64_t {
            return 0; // arena allocator — no-op free
        });

    // Memory operations — operate directly on guest memory
    add("libc.so", "memcpy", HVC_MEMCPY,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            uint64_t dst = a[0], src = a[1], n = a[2];
            if (!dst || !src || !n) return dst;
            std::vector<uint8_t> buf(static_cast<size_t>(n));
            guest_read(g, src, buf.data(), n);
            guest_write(g, dst, buf.data(), n);
            return dst;
        });

    add("libc.so", "memmove", HVC_MEMMOVE,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            uint64_t dst = a[0], src = a[1], n = a[2];
            if (!dst || !src || !n) return dst;
            std::vector<uint8_t> buf(static_cast<size_t>(n));
            guest_read(g, src, buf.data(), n);
            guest_write(g, dst, buf.data(), n);
            return dst;
        });

    add("libc.so", "memset", HVC_MEMSET,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            uint64_t dst = a[0];
            uint8_t  val = static_cast<uint8_t>(a[1]);
            uint64_t n   = a[2];
            if (!dst || !n) return dst;
            std::vector<uint8_t> buf(static_cast<size_t>(n), val);
            guest_write(g, dst, buf.data(), n);
            return dst;
        });

    add("libc.so", "memcmp", HVC_MEMCMP,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            uint64_t s1 = a[0], s2 = a[1], n = a[2];
            if (!n) return 0;
            std::vector<uint8_t> b1(n), b2(n);
            guest_read(g, s1, b1.data(), n);
            guest_read(g, s2, b2.data(), n);
            return static_cast<uint64_t>(memcmp(b1.data(), b2.data(), n));
        });

    // String operations
    add("libc.so", "strlen", HVC_STRLEN,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            return guest_read_string(g, a[0]).size();
        });

    add("libc.so", "strcmp", HVC_STRCMP,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto s1 = guest_read_string(g, a[0]);
            auto s2 = guest_read_string(g, a[1]);
            return static_cast<uint64_t>(s1.compare(s2));
        });

    add("libc.so", "strncmp", HVC_STRNCMP,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto s1 = guest_read_string(g, a[0]);
            auto s2 = guest_read_string(g, a[1]);
            size_t n = static_cast<size_t>(a[2]);
            return static_cast<uint64_t>(s1.compare(0, n, s2, 0, n));
        });

    add("libc.so", "strcpy", HVC_STRCPY,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto src = guest_read_string(g, a[1]);
            guest_write(g, a[0], src.c_str(), src.size() + 1);
            return a[0];
        });

    add("libc.so", "strncpy", HVC_STRNCPY,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto src = guest_read_string(g, a[1]);
            size_t n = static_cast<size_t>(a[2]);
            src.resize(n, '\0');
            guest_write(g, a[0], src.c_str(), n);
            return a[0];
        });

    add("libc.so", "strcat", HVC_STRCAT,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto dst = guest_read_string(g, a[0]);
            auto src = guest_read_string(g, a[1]);
            dst += src;
            guest_write(g, a[0], dst.c_str(), dst.size() + 1);
            return a[0];
        });

    add("libc.so", "strdup", HVC_STRDUP,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto s = guest_read_string(g, a[0]);
            uint64_t size = (s.size() + 1 + 15) & ~15ULL;
            if (heap_bump_ + size > heap_base_ + HEAP_SIZE) return 0;
            uint64_t ptr = heap_bump_;
            heap_bump_ += size;
            guest_write(g, ptr, s.c_str(), s.size() + 1);
            return ptr;
        });

    add("libc.so", "printf", HVC_PRINTF,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto fmt = guest_read_string(g, a[0]);
            std::fprintf(stderr, "[guest printf] %s\n", fmt.c_str());
            return static_cast<uint64_t>(fmt.size());
        });

    add("libc.so", "abort", HVC_ABORT,
        [](guest_t*, const uint64_t[8]) -> uint64_t {
            std::fprintf(stderr, "[ART] guest called abort()\n");
            ::abort();
            return 0;
        });

    add("libc.so", "exit", HVC_EXIT,
        [](guest_t*, const uint64_t a[8]) -> uint64_t {
            std::fprintf(stderr, "[ART] guest called exit(%lld)\n",
                         (long long)a[0]);
            ::exit(static_cast<int>(a[0]));
            return 0;
        });

    // getpid
    add("libc.so", "getpid", HVC_GETPID,
        [](guest_t*, const uint64_t[8]) -> uint64_t {
            return static_cast<uint64_t>(::getpid());
        });

    // getenv — always returns NULL (safe for most init code)
    add("libc.so", "getenv", HVC_GETENV,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    // pthread stubs — enough for init; actual thread execution needs Phase 5
    add("libc.so", "pthread_create", HVC_PTHREAD_CREATE,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            // a[0] = pthread_t*, a[1] = attr*, a[2] = fn GPA, a[3] = arg
            uint64_t handle = next_thread_handle_++;
            guest_write_u64(g, a[0], handle);
            threads_[handle] = { 0, 0 };
            std::fprintf(stderr,
                "[ART] pthread_create: fn=0x%llx handle=0x%llx (stub — not scheduled)\n",
                (unsigned long long)a[2], (unsigned long long)handle);
            return 0; // ESUCCESS
        });

    add("libc.so", "pthread_join", HVC_PTHREAD_JOIN,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    add("libc.so", "pthread_self", HVC_PTHREAD_SELF,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 1; });

    // Mutex stubs — all no-ops (single-threaded guest for now)
    add("libc.so", "pthread_mutex_init",    HVC_PTHREAD_MUTEX_INIT,    [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_mutex_lock",    HVC_PTHREAD_MUTEX_LOCK,    [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_mutex_unlock",  HVC_PTHREAD_MUTEX_UNLOCK,  [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_mutex_destroy", HVC_PTHREAD_MUTEX_DESTROY, [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    // pthread_once — call the init function if flag == 0
    add("libc.so", "pthread_once", HVC_PTHREAD_ONCE,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            // a[0] = once_control (uint64_t*), a[1] = fn GPA
            // We cannot call the guest fn here without vcpu context —
            // flag it as done and let the caller's first real call fail
            // gracefully.  TODO: queue fn GPA for execution on next vCPU run.
            uint64_t flag = guest_read_u64(g, a[0]);
            if (!flag) guest_write_u64(g, a[0], 1);
            return 0;
        });

    // TLS key stubs
    add("libc.so", "pthread_key_create",  HVC_PTHREAD_KEY_CREATE,  [](guest_t* g, const uint64_t a[8]) -> uint64_t { guest_write_u64(g, a[0], 1); return 0; });
    add("libc.so", "pthread_getspecific", HVC_PTHREAD_GETSPECIFIC, [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "pthread_setspecific", HVC_PTHREAD_SETSPECIFIC, [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    // clock / time stubs
    add("libc.so", "clock_gettime", HVC_CLOCK_GETTIME,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            // Write zeroed timespec into guest memory at a[1]
            uint64_t zero = 0;
            guest_write(g, a[1] + 0, &zero, 8);
            guest_write(g, a[1] + 8, &zero, 8);
            return 0;
        });

    add("libc.so", "gettimeofday", HVC_GETTIMEOFDAY,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            uint64_t zero = 0;
            if (a[0]) { guest_write(g, a[0], &zero, 8); guest_write(g, a[0]+8, &zero, 8); }
            return 0;
        });

    add("libc.so", "usleep",    HVC_USLEEP,    [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libc.so", "nanosleep", HVC_NANOSLEEP, [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    // Math / conversion stubs
    add("libc.so", "atoi",   HVC_ATOI,   [](guest_t* g, const uint64_t a[8]) -> uint64_t { auto s = guest_read_string(g, a[0]); return static_cast<uint64_t>(std::stol(s.empty() ? "0" : s)); });
    add("libc.so", "rand",   HVC_RAND,   [](guest_t*, const uint64_t[8]) -> uint64_t { return static_cast<uint64_t>(::rand()); });
    add("libc.so", "srand",  HVC_SRAND,  [](guest_t*, const uint64_t a[8]) -> uint64_t { ::srand(static_cast<unsigned>(a[0])); return 0; });

    // libm aliases — expose same stubs under libm.so
    sym_tables_["libm.so"] = sym_tables_["libc.so"]; // share the same stub GPAs
}

// ── liblog stubs ──────────────────────────────────────────────────────────────

void AndroidRuntime::register_liblog_stubs()
{
    add("liblog.so", "__android_log_print", HVC_LOG_PRINT,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            // a[0]=priority, a[1]=tag GPA, a[2]=fmt GPA, a[3..]=args
            auto tag = guest_read_string(g, a[1]);
            auto fmt = guest_read_string(g, a[2]);
            std::fprintf(stderr, "[logcat/%s] %s\n", tag.c_str(), fmt.c_str());
            return static_cast<uint64_t>(fmt.size());
        });

    add("liblog.so", "__android_log_write", HVC_LOG_WRITE,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto tag = guest_read_string(g, a[1]);
            auto msg = guest_read_string(g, a[2]);
            std::fprintf(stderr, "[logcat/%s] %s\n", tag.c_str(), msg.c_str());
            return 0;
        });

    add("liblog.so", "__android_log_buf_write", HVC_LOG_BUF_WRITE,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto tag = guest_read_string(g, a[2]);
            auto msg = guest_read_string(g, a[3]);
            std::fprintf(stderr, "[logcat/%s] %s\n", tag.c_str(), msg.c_str());
            return 0;
        });
}

// ── libandroid stubs ──────────────────────────────────────────────────────────

void AndroidRuntime::register_libandroid_stubs()
{
    // ALooper — return a fake handle GPA (the stub arena address itself)
    add("libandroid.so", "ALooper_prepare", HVC_ALOOPER_PREPARE,
        [this](guest_t*, const uint64_t[8]) -> uint64_t {
            return arena_gpa_ + 0x100; // fake ALooper*
        });

    add("libandroid.so", "ALooper_acquire", HVC_ALOOPER_ACQUIRE,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    add("libandroid.so", "ALooper_release", HVC_ALOOPER_RELEASE,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    add("libandroid.so", "ALooper_pollOnce", HVC_ALOOPER_POLL_ONCE,
        [](guest_t*, const uint64_t[8]) -> uint64_t {
            return static_cast<uint64_t>(-1); // ALOOPER_POLL_TIMEOUT
        });

    add("libandroid.so", "ALooper_pollAll", HVC_ALOOPER_POLL_ALL,
        [](guest_t*, const uint64_t[8]) -> uint64_t {
            return static_cast<uint64_t>(-1); // ALOOPER_POLL_TIMEOUT
        });

    add("libandroid.so", "ALooper_addFd",    HVC_ALOOPER_ADD_FD,    [](guest_t*, const uint64_t[8]) -> uint64_t { return 1; });
    add("libandroid.so", "ALooper_removeFd", HVC_ALOOPER_REMOVE_FD, [](guest_t*, const uint64_t[8]) -> uint64_t { return 1; });
    add("libandroid.so", "ALooper_wake",     HVC_ALOOPER_WAKE,      [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    // ANativeWindow stubs
    add("libandroid.so", "ANativeWindow_setBuffersGeometry", HVC_NATIVE_WINDOW_SET_BUF,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libandroid.so", "ANativeWindow_lock",   HVC_NATIVE_WINDOW_LOCK,   [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });
    add("libandroid.so", "ANativeWindow_unlockAndPost", HVC_NATIVE_WINDOW_UNLOCK, [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    // AChoreographer stub
    add("libandroid.so", "AChoreographer_getInstance", HVC_CHOREOGRAPHER_GET,
        [this](guest_t*, const uint64_t[8]) -> uint64_t {
            return arena_gpa_ + 0x200; // fake AChoreographer*
        });
    add("libandroid.so", "AChoreographer_postFrameCallback", HVC_CHOREOGRAPHER_CB,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; });

    // __system_property_get
    add("libandroid.so", "__system_property_get", HVC_PROP_GET,
        [](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto name = guest_read_string(g, a[0]);
            std::fprintf(stderr, "[ART] __system_property_get(%s) → ''\n", name.c_str());
            if (a[1]) {
                uint8_t nul = 0;
                guest_write(g, a[1], &nul, 1);
            }
            return 0;
        });
}

// ── libdl stubs ───────────────────────────────────────────────────────────────

void AndroidRuntime::register_libdl_stubs()
{
    add("libdl.so", "dlopen", HVC_DLOPEN,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto path = guest_read_string(g, a[0]);
            std::fprintf(stderr, "[ART] dlopen(%s)\n", path.c_str());
            uint64_t h = next_dl_handle_++;
            dl_handles_[h] = { path, 0 };
            return h;
        });

    add("libdl.so", "dlsym", HVC_DLSYM,
        [this](guest_t* g, const uint64_t a[8]) -> uint64_t {
            auto sym = guest_read_string(g, a[1]);
            std::fprintf(stderr, "[ART] dlsym(0x%llx, %s) → 0 (stub)\n",
                         (unsigned long long)a[0], sym.c_str());
            // Search our stub tables for the symbol
            for (auto& [soname, syms] : sym_tables_) {
                auto it = syms.find(sym);
                if (it != syms.end()) return it->second;
            }
            return 0;
        });

    add("libdl.so", "dlclose", HVC_DLCLOSE,
        [this](guest_t*, const uint64_t a[8]) -> uint64_t {
            dl_handles_.erase(a[0]);
            return 0;
        });

    add("libdl.so", "dlerror", HVC_DLERROR,
        [](guest_t*, const uint64_t[8]) -> uint64_t { return 0; }); // NULL = no error

    // libdl_android.so mirrors libdl.so
    sym_tables_["libdl_android.so"] = sym_tables_["libdl.so"];
}

} // namespace muplar::runtime::android