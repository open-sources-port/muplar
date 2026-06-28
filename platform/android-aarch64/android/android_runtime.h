#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "gpu_bridge.h"

extern "C" {
    #include "core/guest.h"
}

namespace muplar::runtime::android {

using BuiltinSymbols = std::unordered_map<std::string, uint64_t>;

using StubHandler = std::function<uint64_t(guest_t*, const uint64_t[8])>;
using GuestFunctionInvoker =
    std::function<int64_t(uint64_t, const std::vector<uint64_t>&)>;

struct StubEntry {
    std::string  soname;
    std::string  symbol;
    uint32_t     hvc_nr;
    StubHandler  handler;
};

class AndroidRuntime : public GpuBridge {
public:
    struct PendingPthreadCall {
        uint64_t handle = 0;
        uint64_t start_routine = 0;
        uint64_t arg = 0;
        uint64_t stack_top = 0;
    };

    struct PendingLooperCallback {
        uint64_t callback = 0;
        int32_t fd = -1;
        int32_t events = 0;
        uint64_t data = 0;
        int32_t ident = 0;
    };

    struct PendingFrameCallback {
        uint64_t callback = 0;
        uint64_t frame_time_nanos = 0;
        uint64_t data = 0;
    };

    AndroidRuntime(guest_t* guest,
                   uint64_t stub_arena_gpa,
                   bool host_window_enabled = false);
    ~AndroidRuntime();

    void set_asset_root(std::string asset_root);
    void set_guest_function_invoker(GuestFunctionInvoker invoker);

    // Write HVC shim stubs into guest memory and build the symbol tables.
    void install() override;


    // Return the builtin symbol map for a given soname.
    BuiltinSymbols builtin_symbols(const std::string& soname) const override;


    static constexpr const char* KNOWN_SONAMES[] = {
        "libc.so", "libm.so", "libdl.so", "libdl_android.so", "liblog.so",
        "libandroid.so", "libbinder_ndk.so", "libstdc++.so",
        "libEGL.so", "libGLESv2.so", "libGLESv3.so",
        "libc++_shared.so", "libc++abi.so", "libunwind.so",
        "libandroid_support.so", "libjnigraphics.so",
        nullptr
    };

    // HVC dispatch — call from the HVC exit handler when X8 is in [0x2000, 0x2FFF].
    bool try_dispatch(uint32_t hvc_nr, const uint64_t regs[8], uint64_t* x0_out) override;

    // EGL state accessors (used by NativeWindow bridge)
    uint64_t input_queue_handle() const { return GUEST_INPUT_QUEUE; }
    uint64_t asset_manager_handle() const { return GUEST_ASSET_MANAGER; }
    bool pump_host_app_events() override;
    void set_thread_yield_enabled(bool enabled);
    bool consume_thread_yield();
    std::vector<PendingPthreadCall> take_pending_pthread_calls();
    void complete_pthread_call(uint64_t handle, uint64_t retval);
    std::vector<PendingLooperCallback> take_pending_looper_callbacks();
    std::vector<PendingFrameCallback> take_pending_frame_callbacks();

protected:
    bool collect_host_input_events() override;

private:
    bool queue_ready_looper_callbacks();

private:
    void register_libc_stubs();
    void register_liblog_stubs();
    void register_libandroid_stubs();
    void register_libjnigraphics_stubs();
    void register_libdl_stubs();
    void register_libcxx_stubs();
    void register_libbinder_stubs();

    // Allocate a passthrough stub that saves/restores and calls a host fn ptr.
    // Used by eglGetProcAddress so the guest can resolve arbitrary GLES symbols.
    uint64_t alloc_passthrough_stub(void* host_fn, uint32_t hvc_nr);

    // ── Bump allocator for malloc stubs ──────────────────────────────────────
    uint64_t heap_base_  = 0;
    uint64_t heap_bump_  = 0;
    static constexpr uint64_t HEAP_SIZE = 8 * 1024 * 1024; // 8MB for libc++ objects

    // ── pthread handle table ──────────────────────────────────────────────────
    struct PthreadEntry { uint64_t stack_gpa; uint64_t stack_size; };
    std::unordered_map<uint64_t, PthreadEntry> threads_;
    std::vector<PendingPthreadCall> pending_pthreads_;
    std::unordered_map<uint64_t, uint64_t> pthread_returns_;
    std::unordered_map<uint64_t, uint64_t> pending_pthread_join_retvals_;
    uint64_t next_thread_handle_ = 0x8000'0001ULL;

    // ── ALooper registrations ────────────────────────────────────────────────
    struct LooperRegistration {
        int32_t fd = -1;
        int32_t ident = 0;
        int32_t events = 0;
        uint64_t callback = 0;
        uint64_t data = 0;
        bool delivered = false;
    };
    std::vector<LooperRegistration> looper_regs_;
    std::vector<PendingLooperCallback> pending_looper_callbacks_;

    // ── Guest pipe state used by native_app_glue-style command queues ────────
    struct HostPipe {
        int32_t read_fd = -1;
        int32_t write_fd = -1;
        bool read_open = true;
        bool write_open = true;
        std::vector<uint8_t> buffer;
    };
    std::vector<HostPipe> pipes_;
    int32_t next_pipe_fd_ = 200;

    // ── Input queue state ────────────────────────────────────────────────────
    struct InputQueueState {
        bool attached = false;
        uint64_t looper = 0;
        int32_t ident = 0;
        uint64_t callback = 0;
        uint64_t data = 0;
    };
    struct InputEventState {
        uint64_t handle = 0;
        int32_t type = 0;
        int32_t action = 0;
        int32_t source = 0;
        int32_t device_id = 0;
        int32_t key_code = 0;
        float x = 0.0f;
        float y = 0.0f;
        bool offered = false;
        bool finished = false;
    };
    InputQueueState input_queue_;
    std::vector<InputEventState> input_events_;
    size_t next_input_event_ = 0;
    uint64_t current_input_event_ = 0;
    static constexpr uint64_t GUEST_INPUT_QUEUE = 0xA11E0001ULL;
    static constexpr uint64_t GUEST_INPUT_EVENT_BASE = 0xA11E1000ULL;
    static constexpr int32_t INPUT_QUEUE_FD = 91;

    // ── AssetManager state ───────────────────────────────────────────────────
    struct AssetState {
        std::string name;
        std::vector<uint8_t> bytes;
        size_t offset = 0;
    };
    std::string asset_root_;
    std::unordered_map<uint64_t, AssetState> assets_;
    uint64_t next_asset_handle_ = 0xA5511000ULL;
    static constexpr uint64_t GUEST_ASSET_MANAGER = 0xA5510001ULL;

    // ── Choreographer state ──────────────────────────────────────────────────
    std::vector<PendingFrameCallback> pending_frame_callbacks_;
    uint64_t next_frame_time_nanos_ = 16'666'666ULL;
    bool thread_yield_enabled_ = false;
    bool thread_yielded_ = false;

    // ── dlopen handle table ───────────────────────────────────────────────────
    struct DlopenEntry { std::string path; uint64_t load_base; };
    std::unordered_map<uint64_t, DlopenEntry> dl_handles_;
    uint64_t next_dl_handle_ = 0x9000'0001ULL;

    // ── Binder/service-manager state ─────────────────────────────────────────
    struct BinderService {
        std::string name;
        uint32_t ref_count = 1;
        bool alive = true;
        bool remote = true;
        uint64_t class_handle = 0;
        uint64_t user_data = 0;
        uint64_t extension_handle = 0;
        struct DeathLink {
            uint64_t recipient_handle = 0;
            uint64_t cookie = 0;
            uint64_t on_binder_died = 0;
            uint64_t on_unlinked = 0;
        };
        std::vector<DeathLink> death_links;
    };
    struct BinderClass {
        std::string descriptor;
        uint64_t descriptor_gpa = 0;
        uint64_t on_create = 0;
        uint64_t on_destroy = 0;
        uint64_t on_transact = 0;
        bool interface_token_header = true;
        std::vector<std::string> transaction_names;
        std::vector<uint64_t> transaction_name_gpas;
    };
    enum class BinderParcelKind {
        InterfaceToken,
        Int32,
        Uint32,
        Int64,
        Uint64,
        Float,
        Double,
        Bool,
        Byte,
        Char,
        StrongBinder,
        Status,
        String,
        Int32Array,
        StringArray,
        ParcelableArray,
        ParcelFileDescriptor,
    };
    struct BinderParcelValue {
        BinderParcelKind kind = BinderParcelKind::Int32;
        uint64_t value = 0;
        std::string text;
        std::vector<uint64_t> elements;
        std::vector<std::string> strings;
    };
    struct BinderParcel {
        uint64_t target_binder = 0;
        uint32_t transaction_code = 0;
        bool reply = false;
        size_t cursor = 0;
        std::vector<BinderParcelValue> values;
    };
    struct BinderStatus {
        int32_t exception = 0;
        int32_t service_error = 0;
        int32_t status = 0;
        std::string message;
    };
    struct BinderDeathRecipient {
        uint64_t on_binder_died = 0;
        uint64_t on_unlinked = 0;
    };
    struct BinderWeak {
        uint64_t binder_handle = 0;
    };
    std::unordered_map<uint64_t, BinderService> binder_services_;
    std::unordered_map<std::string, uint64_t> binder_service_by_name_;
    std::unordered_set<std::string> binder_removed_service_names_;
    uint64_t next_binder_handle_ = 0xB1D0'0001ULL;
    std::unordered_map<uint64_t, BinderClass> binder_classes_;
    uint64_t next_binder_class_handle_ = 0xB1C0'0001ULL;
    std::unordered_map<uint64_t, BinderParcel> binder_parcels_;
    uint64_t next_binder_parcel_handle_ = 0xB1D1'0001ULL;
    std::unordered_map<uint64_t, BinderStatus> binder_statuses_;
    uint64_t next_binder_status_handle_ = 0xB1D2'0001ULL;
    std::unordered_map<uint64_t, BinderDeathRecipient> binder_death_recipients_;
    uint64_t next_binder_death_handle_ = 0xB1D3'0001ULL;
    std::unordered_map<uint64_t, BinderWeak> binder_weaks_;
    uint64_t next_binder_weak_handle_ = 0xB1D4'0001ULL;
    GuestFunctionInvoker guest_function_invoker_;

    void rearm_looper_fd(int32_t fd);
    HostPipe* pipe_for_fd(int32_t fd);
    const HostPipe* pipe_for_fd(int32_t fd) const;
    bool looper_fd_ready(int32_t fd) const;
    void release_binder_strong(uint64_t handle);
};

} // namespace muplar::runtime::android
