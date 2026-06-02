// platform/android-aarch64/jni/jni_onload.cpp
#include "jni_onload.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

// elfuse defines PF_R/W/X; undef before pulling in system elf.h to avoid
// the -Wmacro-redefined warnings from bionic's linux/elf.h.
extern "C" {
    #include "core/elf.h"
}
#ifdef PF_R
#  undef PF_R
#  undef PF_W
#  undef PF_X
#endif
#include <elf.h>    // Elf64_Ehdr, Elf64_Sym — host-side ELF parsing only

extern "C" {
    #include "core/guest.h"
}

#include <Hypervisor/Hypervisor.h>

namespace muplar::runtime::jni {

// ────────────────────────────────────────────────────────────────────────────
// GPA arena layout (256 bytes total):
//
//  +0x00  java_vm_table : 8 slots × 8 bytes = 64 bytes  (JNIInvokeInterface)
//  +0x40  java_vm_ptr   : 8 bytes   — JavaVM* seen by the guest
//  +0x48  jni_env_ptr   : 8 bytes   — JNIEnv** seen by the guest
//  +0x50  (free)
// ────────────────────────────────────────────────────────────────────────────
static constexpr uint64_t OFFSET_VM_TABLE  = 0x00;
static constexpr uint64_t OFFSET_VM_PTR    = 0x40;
static constexpr uint64_t OFFSET_ENV_PTR   = 0x48;
static constexpr uint64_t STUB_SIZE        = 12;
static constexpr uint64_t ARENA_SIZE       = 0x100;

JniOnLoad::JniOnLoad(guest_t*   guest,
                     JniBridge* bridge,
                     JniEnv*    env,
                     uint64_t   arena_gpa)
    : guest_(guest)
    , bridge_(bridge)
    , env_(env)
    , arena_gpa_(arena_gpa)
{}

// ────────────────────────────────────────────────────────────────────────────
void JniOnLoad::write_u64(uint64_t gpa, uint64_t value)
{
    if (guest_write(guest_, gpa, &value, sizeof(value)) != 0) {
        std::fprintf(stderr, "[JNI_OnLoad] write_u64 failed at GPA 0x%llx\n",
                     (unsigned long long)gpa);
    }
}

// ────────────────────────────────────────────────────────────────────────────
void JniOnLoad::install()
{
    java_vm_table_gpa_ = arena_gpa_ + OFFSET_VM_TABLE;
    java_vm_ptr_gpa_   = arena_gpa_ + OFFSET_VM_PTR;
    jni_env_ptr_gpa_   = arena_gpa_ + OFFSET_ENV_PTR;

    // ── Zero the arena ────────────────────────────────────────────────────
    uint8_t zeroes[ARENA_SIZE] = {};
    guest_write(guest_, arena_gpa_, zeroes, ARENA_SIZE);

    // ── Helper: encode a callable HVC #6 stub into guest memory ─────────────
    auto write_vm_stub = [&](uint64_t gpa, uint32_t hvc_nr) {
        uint8_t stub[STUB_SIZE];
        uint32_t movz = 0xD2800008u | ((hvc_nr & 0xFFFFu) << 5);
        uint32_t hvc  = 0xD4000002u | (6u << 5);   // hvc #6
        uint32_t ret  = 0xD65F03C0u;
        memcpy(stub + 0, &movz, 4);
        memcpy(stub + 4, &hvc,  4);
        memcpy(stub + 8, &ret,  4);
        guest_write(guest_, gpa, stub, sizeof(stub));
        return gpa;
    };

    // JavaVM HVC call numbers — handled by try_intercept()
    static constexpr uint32_t HVC_VM_GETENV        = 0x1FF0;
    static constexpr uint32_t HVC_VM_ATTACH        = 0x1FF1;
    static constexpr uint32_t HVC_VM_DETACH        = 0x1FF2;
    static constexpr uint32_t HVC_VM_DESTROY       = 0x1FF3;
    static constexpr uint32_t HVC_VM_ATTACH_DAEMON = 0x1FF4;

    // Place VM stubs after the table/pointer cells.
    uint64_t vm_stub_base = arena_gpa_ + 0x50;
    uint64_t stub_destroy = write_vm_stub(vm_stub_base + 0 * STUB_SIZE, HVC_VM_DESTROY);
    uint64_t stub_attach  = write_vm_stub(vm_stub_base + 1 * STUB_SIZE, HVC_VM_ATTACH);
    uint64_t stub_detach  = write_vm_stub(vm_stub_base + 2 * STUB_SIZE, HVC_VM_DETACH);
    uint64_t stub_getenv  = write_vm_stub(vm_stub_base + 3 * STUB_SIZE, HVC_VM_GETENV);
    uint64_t stub_daemon  = write_vm_stub(vm_stub_base + 4 * STUB_SIZE, HVC_VM_ATTACH_DAEMON);

    // Sentinel stub after the VM method stubs — JNI_OnLoad branches here via LR on ret.
    // X0 = JNI_OnLoad return value at this point.
    // Sequence:
    //   movz x8, #HVC_JNI_ONLOAD_RETURN   ; tag the call for hvc6_handler
    //   hvc  #6                             ; try_intercept saves X0 as retval
    //   hvc  #0                             ; clean guest exit → vcpu_run_loop stops
    {
        uint64_t gpa = vm_stub_base + 5 * STUB_SIZE;
        uint8_t stub[12];
        uint32_t movz = 0xD2800008u | ((HVC_JNI_ONLOAD_RETURN & 0xFFFFu) << 5);
        uint32_t hvc6 = 0xD4000002u | (6u << 5);   // hvc #6 — notify host
        uint32_t hvc0 = 0xD4000002u | (0u << 5);   // hvc #0 — clean exit
        memcpy(stub + 0, &movz, 4);
        memcpy(stub + 4, &hvc6, 4);
        memcpy(stub + 8, &hvc0, 4);
        guest_write(guest_, gpa, stub, 12);
        sentinel_stub_gpa_ = gpa;
    }

    write_u64(java_vm_table_gpa_ + 3 * 8, stub_destroy);
    write_u64(java_vm_table_gpa_ + 4 * 8, stub_attach);
    write_u64(java_vm_table_gpa_ + 5 * 8, stub_detach);
    write_u64(java_vm_table_gpa_ + 6 * 8, stub_getenv);
    write_u64(java_vm_table_gpa_ + 7 * 8, stub_daemon);

    // ── java_vm_ptr points to the table ───────────────────────────────────
    write_u64(java_vm_ptr_gpa_, java_vm_table_gpa_);

    // ── jni_env_ptr points to the JNIEnv table installed by JniBridge ─────
    write_u64(jni_env_ptr_gpa_, env_->jni_interface_gpa());

    std::fprintf(stderr,
        "[JNI_OnLoad] install: vm_table=0x%llx vm_ptr=0x%llx env_ptr=0x%llx\n",
        (unsigned long long)java_vm_table_gpa_,
        (unsigned long long)java_vm_ptr_gpa_,
        (unsigned long long)jni_env_ptr_gpa_);
}

// ────────────────────────────────────────────────────────────────────────────
// find_jni_onload — scan the .so's dynsym on the host for "JNI_OnLoad".
//
// so_load_base: GPA where the dynamic linker placed the .so.
// so_path:      host filesystem path to the same .so (for symbol table read).
//
// We open the file on the host, walk Elf64_Shdr → SHT_DYNSYM, and find
// the symbol "JNI_OnLoad".  Its st_value (relative to 0) is added to
// so_load_base to get the live GPA.
// ────────────────────────────────────────────────────────────────────────────
uint64_t JniOnLoad::find_symbol(uint64_t           so_load_base,
                                 const std::string& so_path,
                                 const std::string& symbol_name,
                                 bool               quiet)
{
    FILE* f = std::fopen(so_path.c_str(), "rb");
    if (!f) {
        if (!quiet)
            std::fprintf(stderr, "[JNI] cannot open %s\n", so_path.c_str());
        return 0;
    }

    // Read ELF header
    Elf64_Ehdr ehdr;
    if (std::fread(&ehdr, sizeof(ehdr), 1, f) != 1) {
        std::fclose(f);
        return 0;
    }
    if (std::memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr.e_machine != EM_AARCH64) {
        if (!quiet)
            std::fprintf(stderr, "[JNI] %s is not AArch64 ELF64\n",
                         so_path.c_str());
        std::fclose(f);
        return 0;
    }

    // Walk section headers looking for SHT_DYNSYM
    uint64_t dynsym_off  = 0;
    uint64_t dynsym_size = 0;
    uint64_t dynstr_off  = 0;

    for (int i = 0; i < ehdr.e_shnum; ++i) {
        Elf64_Shdr shdr;
        std::fseek(f, static_cast<long>(ehdr.e_shoff + i * ehdr.e_shentsize), SEEK_SET);
        if (std::fread(&shdr, sizeof(shdr), 1, f) != 1) break;

        if (shdr.sh_type == SHT_DYNSYM) {
            dynsym_off  = shdr.sh_offset;
            dynsym_size = shdr.sh_size;
            // Associated string table is shdr.sh_link
            Elf64_Shdr strshdr;
            std::fseek(f, static_cast<long>(ehdr.e_shoff + shdr.sh_link * ehdr.e_shentsize), SEEK_SET);
            if (std::fread(&strshdr, sizeof(strshdr), 1, f) == 1)
                dynstr_off = strshdr.sh_offset;
            break;
        }
    }

    if (!dynsym_off || !dynstr_off) {
        if (!quiet)
            std::fprintf(stderr, "[JNI] no DYNSYM in %s\n", so_path.c_str());
        std::fclose(f);
        return 0;
    }

    // Walk symbols
    size_t sym_count = dynsym_size / sizeof(Elf64_Sym);
    std::fseek(f, static_cast<long>(dynsym_off), SEEK_SET);

    for (size_t i = 0; i < sym_count; ++i) {
        Elf64_Sym sym;
        if (std::fread(&sym, sizeof(sym), 1, f) != 1) break;

        if (sym.st_name == 0) continue;

        char name[256] = {};
        long cur = std::ftell(f);
        std::fseek(f, static_cast<long>(dynstr_off + sym.st_name), SEEK_SET);
        std::fread(name, 1, sizeof(name) - 1, f);
        std::fseek(f, cur, SEEK_SET);

        if (symbol_name == name) {
            std::fclose(f);
            uint64_t gpa = so_load_base + sym.st_value;
            if (!quiet)
                std::fprintf(stderr,
                    "[JNI] found export %s: st_value=0x%llx → GPA=0x%llx\n",
                    symbol_name.c_str(),
                    (unsigned long long)sym.st_value,
                    (unsigned long long)gpa);
            return gpa;
        }
    }

    if (!quiet)
        std::fprintf(stderr, "[JNI] export %s not found in %s\n",
                     symbol_name.c_str(), so_path.c_str());
    std::fclose(f);
    return 0;
}

uint64_t JniOnLoad::find_jni_onload(uint64_t           so_load_base,
                                     const std::string& so_path)
{
    uint64_t gpa = find_symbol(so_load_base, so_path, "JNI_OnLoad", true);
    if (gpa) {
        std::fprintf(stderr, "[JNI_OnLoad] found JNI_OnLoad: GPA=0x%llx\n",
                     (unsigned long long)gpa);
    } else {
        std::fprintf(stderr, "[JNI_OnLoad] JNI_OnLoad not found in %s\n",
                     so_path.c_str());
    }
    return gpa;
}

// ────────────────────────────────────────────────────────────────────────────
// call_jni_onload
//
// Sets up the vCPU registers and re-enters the run loop until the sentinel
// HVC fires (LR = shim stub for HVC 0x1FFF).
// ────────────────────────────────────────────────────────────────────────────
int JniOnLoad::call_jni_onload(
    uint64_t        jni_onload_gpa,
    hv_vcpu_t       vcpu,
    hv_vcpu_exit_t* vexit,
    std::function<int(hv_vcpu_t, hv_vcpu_exit_t*, guest_t*)> run_loop_cb)
{
    // ── Save current PC/LR so we can restore after JNI_OnLoad ─────────────
    uint64_t saved_pc, saved_lr, saved_x0, saved_x1;
    uint64_t saved_sp_el1 = 0;
    uint64_t saved_sctlr = 0;
    uint64_t call_sp = 0;
    hv_vcpu_get_reg(vcpu, HV_REG_PC, &saved_pc);
    hv_vcpu_get_reg(vcpu, HV_REG_LR, &saved_lr);
    hv_vcpu_get_reg(vcpu, HV_REG_X0, &saved_x0);
    hv_vcpu_get_reg(vcpu, HV_REG_X1, &saved_x1);
    hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_SP_EL1, &saved_sp_el1);
    hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_SP_EL0, &call_sp);
    hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_SCTLR_EL1, &saved_sctlr);
    // Host-entered JNI calls run on the app stack, not elfuse's high EL1 stack.
    if (call_sp)
        hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, call_sp);
    hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SCTLR_EL1, saved_sctlr | (1ULL << 6));

    // ── Set X0 = JavaVM* (points to our JavaVM pointer) ───────────────────
    hv_vcpu_set_reg(vcpu, HV_REG_X0, java_vm_ptr_gpa_);

    // ── Set X1 = 0 (reserved, ignored by JNI_OnLoad) ──────────────────────
    hv_vcpu_set_reg(vcpu, HV_REG_X1, 0);

    // ── Set LR = sentinel HVC stub GPA so JNI_OnLoad's ret fires HVC 0x1FFF ─
    hv_vcpu_set_reg(vcpu, HV_REG_LR, sentinel_stub_gpa_);

    // ── Set PC = JNI_OnLoad entry ──────────────────────────────────────────
    hv_vcpu_set_reg(vcpu, HV_REG_PC, jni_onload_gpa);

    std::fprintf(stderr,
        "[JNI_OnLoad] calling JNI_OnLoad at GPA 0x%llx (JavaVM*=0x%llx)\n",
        (unsigned long long)jni_onload_gpa,
        (unsigned long long)java_vm_ptr_gpa_);

    onload_returned_ = false;
    onload_retval_   = 0;

    // ── Re-enter the run loop; try_intercept() sets onload_returned_ ──────
    run_loop_cb(vcpu, vexit, guest_);

    // ── Restore registers ─────────────────────────────────────────────────
    hv_vcpu_set_reg(vcpu, HV_REG_PC, saved_pc);
    hv_vcpu_set_reg(vcpu, HV_REG_LR, saved_lr);
    hv_vcpu_set_reg(vcpu, HV_REG_X0, saved_x0);
    hv_vcpu_set_reg(vcpu, HV_REG_X1, saved_x1);
    hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, saved_sp_el1);
    hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SCTLR_EL1, saved_sctlr);

    int ret = static_cast<int>(onload_retval_);
    std::fprintf(stderr, "[JNI_OnLoad] JNI_OnLoad returned %d\n", ret);
    return ret;
}

// ────────────────────────────────────────────────────────────────────────────
int64_t JniOnLoad::call_guest_function(
    uint64_t        entry_gpa,
    const std::vector<uint64_t>& args,
    hv_vcpu_t       vcpu,
    hv_vcpu_exit_t* vexit,
    std::function<int(hv_vcpu_t, hv_vcpu_exit_t*, guest_t*)> run_loop_cb)
{
    uint64_t saved_pc = 0;
    uint64_t saved_x[31] = {};
    uint64_t saved_fpcr = 0;
    uint64_t saved_fpsr = 0;
    uint64_t saved_cpsr = 0;
    uint64_t saved_sp_el0 = 0;
    uint64_t saved_sp_el1 = 0;
    uint64_t saved_spsr_el1 = 0;
    uint64_t saved_sctlr = 0;
    uint64_t call_sp = 0;

    hv_vcpu_get_reg(vcpu, HV_REG_PC, &saved_pc);
    for (int i = 0; i < 29; ++i)
        hv_vcpu_get_reg(vcpu,
                        static_cast<hv_reg_t>(HV_REG_X0 + i),
                        &saved_x[i]);
    hv_vcpu_get_reg(vcpu, HV_REG_X29, &saved_x[29]);
    hv_vcpu_get_reg(vcpu, HV_REG_LR, &saved_x[30]);
    hv_vcpu_get_reg(vcpu, HV_REG_FPCR, &saved_fpcr);
    hv_vcpu_get_reg(vcpu, HV_REG_FPSR, &saved_fpsr);
    hv_vcpu_get_reg(vcpu, HV_REG_CPSR, &saved_cpsr);
    hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_SP_EL0, &saved_sp_el0);
    hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_SP_EL1, &saved_sp_el1);
    hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_SPSR_EL1, &saved_spsr_el1);
    hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_SCTLR_EL1, &saved_sctlr);

    // Nested guest callbacks must reuse the active guest stack. SP_EL0 still
    // points near the outer entry stack top and can overlap the caller frame.
    call_sp = saved_sp_el0;
    if (saved_sp_el1 && saved_sp_el1 < 0x100000000ULL)
        call_sp = saved_sp_el1;
    if (call_sp)
        hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, call_sp);
    hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SCTLR_EL1, saved_sctlr | (1ULL << 6));

    for (int i = 0; i < 8; ++i)
        hv_vcpu_set_reg(vcpu, static_cast<hv_reg_t>(HV_REG_X0 + i), 0);

    size_t n = args.size();
    if (n > 8) {
        std::fprintf(stderr,
            "[JNI] raw guest call has %zu args; only first 8 fit X0..X7\n", n);
        n = 8;
    }
    for (size_t i = 0; i < n; ++i) {
        hv_vcpu_set_reg(vcpu,
                        static_cast<hv_reg_t>(HV_REG_X0 + i),
                        args[i]);
    }

    hv_vcpu_set_reg(vcpu, HV_REG_LR, sentinel_stub_gpa_);
    hv_vcpu_set_reg(vcpu, HV_REG_PC, entry_gpa);

    std::fprintf(stderr,
        "[JNI] calling guest function at GPA 0x%llx args=%zu\n",
        (unsigned long long)entry_gpa, args.size());

    onload_returned_ = false;
    onload_retval_   = 0;
    run_loop_cb(vcpu, vexit, guest_);

    hv_vcpu_set_reg(vcpu, HV_REG_PC, saved_pc);
    for (int i = 0; i < 29; ++i)
        hv_vcpu_set_reg(vcpu,
                        static_cast<hv_reg_t>(HV_REG_X0 + i),
                        saved_x[i]);
    hv_vcpu_set_reg(vcpu, HV_REG_X29, saved_x[29]);
    hv_vcpu_set_reg(vcpu, HV_REG_LR, saved_x[30]);
    hv_vcpu_set_reg(vcpu, HV_REG_FPCR, saved_fpcr);
    hv_vcpu_set_reg(vcpu, HV_REG_FPSR, saved_fpsr);
    hv_vcpu_set_reg(vcpu, HV_REG_CPSR, saved_cpsr);
    hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL0, saved_sp_el0);
    hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, saved_sp_el1);
    hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SPSR_EL1, saved_spsr_el1);
    hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SCTLR_EL1, saved_sctlr);

    int64_t ret = static_cast<int64_t>(onload_retval_);
    std::fprintf(stderr, "[JNI] guest function returned %lld\n", (long long)ret);
    return ret;
}

// ────────────────────────────────────────────────────────────────────────────
int JniOnLoad::call_native_int2(
    uint64_t        native_gpa,
    uint64_t        thiz,
    int             a,
    int             b,
    hv_vcpu_t       vcpu,
    hv_vcpu_exit_t* vexit,
    std::function<int(hv_vcpu_t, hv_vcpu_exit_t*, guest_t*)> run_loop_cb)
{
    return static_cast<int>(
        call_native(native_gpa, thiz, { a, b }, vcpu, vexit,
                    std::move(run_loop_cb)));
}

// ────────────────────────────────────────────────────────────────────────────
int64_t JniOnLoad::call_native(
    uint64_t        native_gpa,
    uint64_t        thiz,
    const std::vector<int64_t>& args,
    hv_vcpu_t       vcpu,
    hv_vcpu_exit_t* vexit,
    std::function<int(hv_vcpu_t, hv_vcpu_exit_t*, guest_t*)> run_loop_cb)
{
    std::vector<uint64_t> raw_args;
    raw_args.reserve(args.size() + 2);
    raw_args.push_back(jni_env_ptr_gpa_);
    raw_args.push_back(thiz);

    if (args.size() > 6)
        std::fprintf(stderr,
            "[JNI] native call has %zu args; only first 6 fit X2..X7\n",
            args.size());
    for (size_t i = 0; i < args.size() && i < 6; ++i)
        raw_args.push_back(static_cast<uint64_t>(args[i]));

    std::fprintf(stderr,
        "[JNI] calling native at GPA 0x%llx args=%zu\n",
        (unsigned long long)native_gpa, args.size());
    int64_t ret = call_guest_function(native_gpa, raw_args, vcpu, vexit,
                                      std::move(run_loop_cb));
    std::fprintf(stderr, "[JNI] native returned %lld\n", (long long)ret);
    return ret;
}

// ────────────────────────────────────────────────────────────────────────────
// try_intercept — called from the HVC exit handler in guest_runner.cpp
//
// hvc_nr   : the immediate in `hvc #N` — elfuse uses HVC #5 for Linux
//            syscalls; we reserve a separate range so there is no collision.
//            Call this only when the ESR syndrome indicates an HVC and X8
//            is in range [0x1000, 0x1FFF].
// regs[8]  : X0..X7 from the vCPU at the HVC site.
// x0_out   : write the return value here; caller writes it back to X0.
//
// Returns true if we consumed the call.
// ────────────────────────────────────────────────────────────────────────────
bool JniOnLoad::try_intercept(uint32_t       hvc_nr,
                               const uint64_t regs[8],
                               uint64_t*      x0_out)
{
    // We only handle our JNI range
    if (hvc_nr < 0x1000 || hvc_nr > 0x1FFF) return false;

    if (hvc_nr == HVC_JNI_ONLOAD_RETURN) {
        // JNI_OnLoad returned; X0 has the return value
        onload_retval_   = regs[0];
        onload_returned_ = true;
        *x0_out = 0;
        return true;
    }

    // JavaVM method stubs — return JNIEnv* for GetEnv / Attach calls.
    // JNIEnv* is a JNINativeInterface** — a pointer to a pointer to the table.
    // jni_env_ptr_gpa_ is a GPA that already holds the table GPA, so it IS
    // the correct JNIEnv* value: guest dereferences it to get the table pointer.
    // regs[1] = JNIEnv** out-parameter — we write jni_env_ptr_gpa_ into *regs[1].
    if (hvc_nr == 0x1FF0 /* GetEnv */) {
        // version check: regs[2] must be JNI_VERSION_1_6 (0x10006)
        if (regs[2] != 0x10006) {
            *x0_out = static_cast<uint64_t>(-2); // JNI_EVERSION
            return true;
        }
        if (regs[1]) {
            guest_write(guest_, regs[1], &jni_env_ptr_gpa_, 8);
        }
        *x0_out = 0; // JNI_OK
        return true;
    }
    if (hvc_nr == 0x1FF1 /* AttachCurrentThread */ ||
        hvc_nr == 0x1FF4 /* AttachCurrentThreadAsDaemon */) {
        if (regs[1]) {
            guest_write(guest_, regs[1], &jni_env_ptr_gpa_, 8);
        }
        *x0_out = 0; // JNI_OK
        return true;
    }
    if (hvc_nr == 0x1FF2 /* DetachCurrentThread */ ||
        hvc_nr == 0x1FF3 /* DestroyJavaVM */) {
        *x0_out = 0; // JNI_OK
        return true;
    }

    // Route to JniBridge for all other JNI calls (0x1000–0x10FF)
    *x0_out = bridge_->handle_hvc(hvc_nr, const_cast<uint64_t*>(regs));
    return true;
}

} // namespace muplar::runtime::jni
