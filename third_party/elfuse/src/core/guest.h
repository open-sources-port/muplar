/* Guest memory management
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Provides identity-mapped guest physical memory (GVA == GPA == offset into
 * host buffer). Buffer size is determined by the VM's configured IPA width:
 *   - Native aarch64 on M2 (36-bit IPA): 64GiB
 *   - Native aarch64 on M3+ (40-bit IPA): 1TiB
 *
 * Reserved via mmap(MAP_ANON); macOS demand-pages physical memory on first
 * touch, so unused pages cost nothing. The slab is mapped RWX to
 * Hypervisor.framework; fine-grained permissions are enforced by the guest's
 * own page tables built from mem_region descriptors. Page tables can be
 * extended at runtime (e.g. when mmap/brk grows beyond initial mappings).
 */

#pragma once

#include <Hypervisor/Hypervisor.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Memory layout constants.
 *
 * Guest memory size is determined dynamically from the VM's IPA width (36-bit
 * = 64GiB on M2, 40-bit = 1TiB on M3+). See guest.c for the runtime probe that
 * selects the correct size.
 *
 * Infrastructure layout (page-table pool, shim code, shim data): a 4MiB reserve
 * placed just below g->interp_base, in the dead zone between g->mmap_limit and
 * g->interp_base. The exact base is computed at guest_init time and stored in
 * guest_t.pt_pool_base / pt_pool_end / shim_base / shim_data_base. EL0 user
 * binaries are therefore free to load at low addresses (down to 64KiB) without
 * colliding with the runtime.
 *
 * Internal layout within the 4MiB reserve:
 *   +0x000000 .. +0x010000  unused (64KiB null guard)
 *   +0x010000 .. +0x100000  page-table pool (960KiB, RW)
 *   +0x100000 .. +0x200000  shim code slot (1MiB, RX). Sits in the same
 *                           2MiB L2 block as the PT pool, so that block
 *                           is split into 4KiB L3 pages (mixed RX/RW).
 *   +0x200000 .. +0x400000  shim data + EL1 stack (full 2MiB L2 block, RW)
 */

/* Total size of the runtime infrastructure reserve. Shifted to
 * [g->interp_base - INFRA_RESERVE, g->interp_base) at guest_init.
 */
#define INFRA_RESERVE 0x00400000ULL         /* 4MiB */
#define INFRA_PT_POOL_OFF 0x00010000ULL     /* offset of PT pool */
#define INFRA_PT_POOL_END_OFF 0x00100000ULL /* PT pool end (960KiB) */
#define INFRA_SHIM_OFF 0x00100000ULL        /* offset of shim code slot */
#define INFRA_SHIM_DATA_OFF 0x00200000ULL   /* offset of shim data slot */
#define ELF_DEFAULT_BASE 0x00400000ULL      /* Typical ELF load base */
#define PIE_LOAD_BASE 0x00400000ULL    /* PIE (ET_DYN) executable base (4MiB) */
#define BRK_BASE_DEFAULT 0x01000000ULL /* Default brk start (16MiB) */

/* 8MiB stack (four 2MiB blocks); unused HVF backing pages consume no RAM. */
#define STACK_SIZE 0x00800000ULL

/* Used when brk_start is below 128MiB; otherwise placed above brk. */
#define STACK_TOP_DEFAULT 0x08000000ULL
#define STACK_GUARD_SIZE 0x00001000ULL /* 4KiB guard at stack bottom */

/* mmap RX region for PROT_EXEC; placed below 8GiB to leave the high mmap region
 * clear for runtimes that demand a specific minimum heap address.
 */
#define MMAP_RX_BASE 0x10000000ULL

/* Initial pre-mapped mmap RX end. Only covers the first 2MiB block; additional
 * pages are mapped lazily by guest_extend_page_tables() when sys_mmap needs
 * more PROT_EXEC space. Reduces startup time and memory pressure for small
 * binaries that never call mmap.
 */
#define MMAP_RX_INITIAL_END (MMAP_RX_BASE + 0x200000ULL) /* +2MiB */

/* mmap RW region starts at 8GiB to match real Linux address layouts. */
#define MMAP_BASE 0x200000000ULL

/* Initial pre-mapped mmap RW end. Only covers the first 2MiB block; additional
 * pages are mapped lazily by guest_extend_page_tables().
 */
#define MMAP_INITIAL_END (MMAP_BASE + 0x200000ULL) /* +2MiB */

/* mmap_limit and interp_base are computed dynamically from guest_size in main.c
 * and stored in guest_t.
 */
#define BLOCK_2MIB (2ULL * 1024 * 1024)

/* IPA base: guest memory is mapped at this IPA in the hypervisor.
 * All guest physical addresses = GUEST_IPA_BASE + offset.
 * Must be 0 so that guest virtual addresses match ELF link addresses (e.g.
 * 0x400000). A non-zero IPA base would require all ELF binaries to be linked at
 * IPA_BASE+vaddr, which is impractical.
 */
#define GUEST_IPA_BASE 0x0ULL

/* Page table attributes. */
/* Memory region permission flags */
#define MEM_PERM_R (1 << 0)
#define MEM_PERM_W (1 << 1)
#define MEM_PERM_X (1 << 2)
#define MEM_PERM_RX (MEM_PERM_R | MEM_PERM_X)
#define MEM_PERM_RW (MEM_PERM_R | MEM_PERM_W)

/* A contiguous region of guest memory to be mapped in page tables.
 * Identity-mapped: VA == GPA.
 */
typedef struct {
    uint64_t gpa_start; /* Output IPA/GPA (2MiB aligned) */
    uint64_t gpa_end;   /* Output IPA/GPA end (exclusive, 2MiB aligned) */
    int perms;          /* MEM_PERM_* flags */
} mem_region_t;

/* Semantic region tracking. */

/* Maximum number of tracked memory regions (heap/stack/mmap/ELF/etc.).
 * Adjacent anonymous regions with matching permissions are automatically
 * coalesced (see regions_mergeable in core/guest.c). Threaded runtimes create
 * many thread stacks with guard pages; with coalescing, typical workloads use
 * ~50 regions. 4096 provides ample headroom for edge cases (many interleaved
 * guard pages, file-backed mappings, etc.).
 */
#define GUEST_MAX_REGIONS 4096

/* HVF stage-2 mapping segment. The slab is mapped to HVF in pieces so that
 * file-backed MAP_SHARED regions can have real host-VA overlays applied via
 * mmap MAP_FIXED|MAP_SHARED of a file fd. HVF requires hv_vm_unmap to target
 * an exactly-previously-mapped range; sub-range unmap of a larger map fails
 * with HV_BAD_ARGUMENT. To allow a 2 MiB-aligned middle range to be unmapped +
 * remapped (refreshing HVF stage-2 caching after a host mmap MAP_FIXED), the
 * slab is split into 2 MiB-aligned segments around each affected block. All
 * segments are 2 MiB-aligned and 2 MiB-sized at minimum.
 *
 * 256 segments is generous: each MAP_SHARED file mmap costs at most 2 new
 * segments (left/right of the carved block), and most workloads keep that count
 * well under 50.
 */
typedef struct {
    uint64_t ipa; /* 2 MiB-aligned IPA start */
    uint64_t len; /* 2 MiB-aligned length */
} hvf_segment_t;

#define GUEST_MAX_HVF_SEGMENTS 256

/* A semantic memory region tracked for munmap/mprotect and /proc/self/maps.
 * Distinct from mem_region_t which is used purely for page table construction.
 * Regions are kept sorted by start address in guest_t.regions[].
 */
typedef struct {
    uint64_t start;  /* GPA start for gap-finder (page-aligned) */
    uint64_t end;    /* GPA end (exclusive, page-aligned) */
    int prot;        /* LINUX_PROT_* flags */
    int flags;       /* LINUX_MAP_* flags (for /proc/self/maps display) */
    uint64_t offset; /* File offset (for /proc/self/maps display) */
    int backing_fd;  /* Duplicated host fd for file-backed mappings, or -1 */
    bool shared;     /* MAP_SHARED (writes should propagate) */
    bool noreserve;  /* MAP_NORESERVE: PTEs deferred until fault */
    bool overlay_active; /* Region has a live host MAP_FIXED|MAP_SHARED overlay
                          * of backing_fd at host_base+start. The kernel's page
                          * cache keeps it coherent with the file and with peer
                          * overlays of the same file, so msync skips the
                          * snapshot-style pwrite-the-diff and refresh-from-file
                          * paths for these regions.
                          */
    uint64_t overlay_start; /* Host-page-aligned overlay start. May extend
                             * outside [start, end) when only part of a host
                             * page is guest-visible.
                             */
    uint64_t overlay_end;   /* Host-page-aligned overlay end (exclusive). */
    char name[64];          /* Label: "[heap]", "[stack]", ELF path, etc. */
} guest_region_t;

/* TLB invalidation request kinds. After every page-table modification, the
 * shim flushes the TLB on syscall return. The host accumulates the smallest
 * sufficient request across the syscall and emits it via the X8/X9/X10
 * register channel. Kind values are an internal enum independent of the
 * X8 wire codes; the syscall epilogue does the mapping (src/syscall/syscall.c
 * holds the canonical table):
 *   TLBI_NONE      -> X8 = 0  (no TLB flush)
 *   TLBI_BROADCAST -> X8 = 1  (TLBI VMALLE1IS, broadest)
 *   TLBI_RANGE     -> X8 = 3, X9 = start VA, X10 = page count
 *                     (TLBI VAE1IS loop preserves unrelated TLB entries)
 * X8 = 2 is reserved for the execve drop-frame marker the shim handles
 * separately; it is never produced by the accumulator.
 */
typedef enum {
    TLBI_NONE = 0,
    TLBI_BROADCAST = 1,
    TLBI_RANGE = 2,
} tlbi_kind_t;

/* Cap selective TLBI at this many 4 KiB pages. Beyond this, fall back to
 * TLBI_BROADCAST: each TLBI VAE1IS broadcasts to all cores, so for large
 * ranges the per-instruction issue cost outweighs the benefit of preserving
 * unrelated TLB entries. 16 pages == 64 KiB covers RELRO and other typical
 * mprotect / munmap targets.
 */
#define TLBI_SELECTIVE_MAX_PAGES 16

typedef struct {
    uint8_t kind;   /* tlbi_kind_t */
    uint16_t pages; /* Page count when kind == TLBI_RANGE (1..MAX) */
    uint64_t start; /* Page-aligned VA when kind == TLBI_RANGE */
} tlbi_request_t;

/* Guest state. */
typedef struct {
    void *host_base; /* Host pointer to allocated guest memory */
    int shm_fd; /* File fd backing host_base for CoW fork (-1 if MAP_ANON) */

    uint64_t guest_size; /* Total size (determined by IPA capacity) */
    uint64_t ipa_base;   /* IPA base for hv_vm_map (GUEST_IPA_BASE) */
    uint64_t mmap_limit; /* Max mmap address (computed from guest_size) */

    uint64_t interp_base; /* Dynamic linker load base (from guest_size) */

    /* Runtime-infrastructure reserve. Computed at guest_init time and placed at
     * [interp_base - INFRA_RESERVE, interp_base). All four values are derived
     * from the same base, so the inequalities
     *   pt_pool_base < pt_pool_end <= shim_base < shim_data_base
     * always hold, and shim_data_base + BLOCK_2MIB == interp_base.
     */
    uint64_t pt_pool_base;   /* Page-table pool start (high IPA) */
    uint64_t pt_pool_end;    /* Page-table pool end (exclusive) */
    uint64_t shim_base;      /* Shim code (2MiB block, RX) */
    uint64_t shim_data_base; /* Shim stack/data (2MiB block, RW) */

    uint64_t pt_pool_next; /* Next free page table page in pool */
    /* Lowest virtual address of the loaded ELF (executable image, not the
     * dynamic linker). Set by bootstrap and re-set by execve. Used by the
     * legacy fork IPC path to bound the ELF + brk copy chunk; it must cover
     * ET_EXECs linked below ELF_DEFAULT_BASE (e.g. 0x200000).
     */
    uint64_t elf_load_min;
    uint64_t brk_base;    /* Initial brk (set after ELF load) */
    uint64_t brk_current; /* Current brk position */
    uint64_t stack_base;  /* Bottom of stack region (dynamic, above brk) */
    uint64_t stack_top;   /* Top of stack (stack grows down from here) */

    uint64_t mmap_next; /* RW mmap high-water mark for fork IPC snapshots */
    uint64_t mmap_end;  /* Current page-table-covered RW mmap limit */
    /* RX mmap high-water mark serialized through fork IPC. */
    uint64_t mmap_rx_next;
    uint64_t mmap_rx_end; /* Current page-table-covered RX mmap limit */
    /* Gap-finder allocator hints. First free GPA past the last successful mmap
     * in each region; munmap and mremap rewind the hint when a lower address is
     * freed. mprotect does not, since permission changes do not free address
     * space. Per-guest so multi-guest test harnesses (or any future second VM
     * in the same process) cannot cross-pollute each other's allocator state.
     */
    uint64_t mmap_rw_gap_hint, mmap_rx_gap_hint;

    uint64_t ttbr0;       /* TTBR0 value (IPA of L0 page table) */
    hv_vcpu_t vcpu;       /* vCPU handle */
    hv_vcpu_exit_t *exit; /* vCPU exit info */
    uint32_t ipa_bits;    /* IPA bits requested from HVF */
    /* Semantic region tracking for munmap/mprotect/proc-self-maps */
    guest_region_t regions[GUEST_MAX_REGIONS];
    int nregions; /* Number of active regions */

    /* HVF stage-2 segment list: the union of segments[0..n_segments) covers the
     * live IPA range that is currently hv_vm_map'd to HVF. Sorted by ipa.
     * Initially one segment spans the whole slab. See guest.h header comment on
     * hvf_segment_t for the rationale.
     */
    hvf_segment_t segments[GUEST_MAX_HVF_SEGMENTS];
    int n_segments;

    /* Page table generation counter: incremented on every PT modification.
     * Used by the per-thread GVA TLB cache to detect stale entries.
     * 64-bit to avoid wrap-around stale hits over long-running sessions.
     */
    _Atomic uint64_t pt_gen;
} guest_t;

/* Bump the page table generation counter (call after any PT modification).
 * This invalidates all per-thread GVA TLB caches.
 */
static inline void guest_pt_gen_bump(guest_t *g)
{
    atomic_fetch_add_explicit(&g->pt_gen, 1, memory_order_release);
}

/* TLB invalidation request helpers.
 *
 * Per-vCPU TLS slot. Each guest thread (= one host pthread + one HVF vCPU)
 * accumulates its own pending TLBI request as its syscall handlers mutate
 * the page tables. The syscall epilogue (src/syscall/syscall.c) reads its
 * own thread's slot, emits the X8/X9/X10 protocol, and clears it.
 *
 * Why per-vCPU and not a guest_t-global accumulator: a global slot lets one
 * vCPU's syscall epilogue drain (and clear) another vCPU's pending request
 * before that vCPU has eret'd back to EL0, allowing the second vCPU to use
 * a stale TLB until the broadcast TLBI from the first vCPU's shim catches
 * up. A per-vCPU slot makes each thread strictly responsible for issuing
 * the TLBI for its own changes before its own eret. Page-table changes are
 * still global (guest memory and page tables are shared), but TLBI VAE1IS
 * and TLBI VMALLE1IS in the inner-shareable domain broadcast to all PEs,
 * so one vCPU's own TLBI is sufficient to invalidate stale entries on its
 * own PE before resuming guest code.
 *
 * No locking is needed for the slot itself; only the owning thread reads
 * or writes it. Page-table updates remain serialized by mmap_lock.
 *
 * Cross-vCPU shootdown window: between vCPU A releasing mmap_lock at the
 * end of an mprotect/munmap and the shim on A issuing the TLBI, sibling
 * vCPU B may continue executing EL0 code that hits A's now-stale TLB
 * entries. Real Linux closes this with cross-CPU IPI synchronization in
 * the kernel; user-space emulation on Hypervisor.framework cannot inject
 * a synchronous IPI into a sibling vCPU thread, so the window remains.
 * The guest is responsible for serializing concurrent PT mutations
 * against concurrent accesses (futex / pthread_mutex), which is the same
 * contract real Linux requires of well-behaved multi-threaded code. See
 * TODO.md "Bounded retry on stale TLB data abort" (P3 hardening) for the
 * tracked follow-up if a workload ever surfaces an actual reproducer.
 */
extern _Thread_local tlbi_request_t cpu_tlbi_req;

static inline void tlbi_request_clear(void)
{
    cpu_tlbi_req.kind = TLBI_NONE;
    cpu_tlbi_req.pages = 0;
    cpu_tlbi_req.start = 0;
}

static inline void tlbi_request_broadcast(void)
{
    cpu_tlbi_req.kind = TLBI_BROADCAST;
}

static inline void tlbi_request_range(uint64_t start, uint64_t end)
{
    if (cpu_tlbi_req.kind == TLBI_BROADCAST)
        return;
    if (end <= start)
        return;
    /* Page-align: TLBI VAE1IS operates on 4 KiB granules. ALIGN_UP can
     * overflow if end is within PAGE_SIZE-1 of UINT64_MAX; saturate to
     * broadcast in that pathological case rather than wrap to 0.
     */
    const uint64_t mask = 0xFFFULL;
    if (end > UINT64_MAX - mask) {
        tlbi_request_broadcast();
        return;
    }
    uint64_t s = start & ~mask;
    uint64_t e = (end + mask) & ~mask;
    uint64_t n = (e - s) >> 12;
    if (n > TLBI_SELECTIVE_MAX_PAGES) {
        tlbi_request_broadcast();
        return;
    }
    if (cpu_tlbi_req.kind == TLBI_NONE) {
        cpu_tlbi_req.kind = TLBI_RANGE;
        cpu_tlbi_req.start = s;
        cpu_tlbi_req.pages = (uint16_t) n;
        return;
    }
    /* TLBI_RANGE: coalesce by union. Disjoint ranges still produce a single
     * bounding interval; if it stays within the cap, the per-page TLBI loop
     * still wins over a full flush by preserving the rest of the TLB.
     */
    uint64_t es = cpu_tlbi_req.start;
    uint64_t pe = (uint64_t) cpu_tlbi_req.pages * 4096ULL;
    /* The accumulator only ever holds page counts <= TLBI_SELECTIVE_MAX_PAGES
     * (see the cap check above), so es + pe never overflows on real callers,
     * but be explicit.
     */
    if (es > UINT64_MAX - pe) {
        tlbi_request_broadcast();
        return;
    }
    uint64_t ee = es + pe;
    uint64_t us = s < es ? s : es;
    uint64_t ue = e > ee ? e : ee;
    uint64_t un = (ue - us) >> 12;
    if (un > TLBI_SELECTIVE_MAX_PAGES) {
        tlbi_request_broadcast();
        return;
    }
    cpu_tlbi_req.start = us;
    cpu_tlbi_req.pages = (uint16_t) un;
}

/* Convert a guest offset (0-based) to an IPA/VA (ipa_base + offset) */
static inline uint64_t guest_ipa(const guest_t *g, uint64_t offset)
{
    return g->ipa_base + offset;
}

/* True iff [start, end) overlaps the runtime infra reserve
 * [interp_base - INFRA_RESERVE, interp_base). Covers the full 4 MiB
 * reserve including the 64 KiB null-guard slot at the bottom (which
 * has no PT entries but must not become semantically reachable from
 * guest mmap state). Used by sys_mmap (MAP_FIXED), sys_munmap, and
 * sys_mprotect to reject guest attempts to touch page tables, shim
 * code, or shim data through the syscall surface.
 */
static inline bool guest_range_hits_infra(const guest_t *g,
                                          uint64_t start,
                                          uint64_t end)
{
    uint64_t infra_lo = g->interp_base - INFRA_RESERVE;
    uint64_t infra_hi = g->interp_base;
    return start < infra_hi && end > infra_lo;
}

/* True iff a single address (PC, hint, etc.) falls inside the infra reserve.
 * Used by rt_sigreturn to reject forged frames that would redirect EL0 PC into
 * EL1 shim or page-table memory. Covers the full 4 MiB reserve, matching
 * guest_range_hits_infra.
 */
static inline bool guest_addr_in_infra(const guest_t *g, uint64_t addr)
{
    uint64_t infra_lo = g->interp_base - INFRA_RESERVE;
    uint64_t infra_hi = g->interp_base;
    return addr >= infra_lo && addr < infra_hi;
}

/* API */

/* Allocate guest memory, create VM, map to hypervisor.
 * size: primary buffer size (0 = auto-detect from IPA capacity).
 * ipa_bits: IPA width for HVF VM (0 = auto-detect).
 * Returns 0 on success, -1 on failure.
 */
int guest_init(guest_t *g, uint64_t size, uint32_t ipa_bits);

/* Initialize guest from a POSIX shared memory fd (CoW fork path).
 * Maps shm_fd MAP_PRIVATE (copy-on-write), creates HVF VM, maps to
 * hypervisor. The child gets an instant CoW snapshot of parent's guest
 * memory without copying. shm_fd is closed after mapping.
 * Returns 0 on success, -1 on failure.
 */
int guest_init_from_shm(guest_t *g,
                        int shm_fd,
                        uint64_t size,
                        uint32_t ipa_bits);

/* Tear down VM and free guest memory. */
void guest_destroy(guest_t *g);

/* Get a host pointer for a guest virtual address (read access).
 * Returns NULL if gva is out of bounds or not readable.
 */
void *guest_ptr(const guest_t *g, uint64_t gva);

/* Get a host pointer for a guest virtual address (write access).
 * Returns NULL if gva is out of bounds or not writable.
 */
void *guest_ptr_w(const guest_t *g, uint64_t gva);

/* Get a host pointer for a guest virtual address, with available byte count.
 * *avail receives the number of contiguous bytes from gva whose page table
 * entries are valid and satisfy required_perms (MEM_PERM_R/W/X bitmask).
 * The function walks forward across adjacent L2 blocks and L3 pages until it
 * hits an invalid, permission-mismatched, or physically non-contiguous entry.
 * Returns NULL if the starting page is unmapped or lacks required_perms.
 */
void *guest_ptr_avail(const guest_t *g,
                      uint64_t gva,
                      uint64_t *avail,
                      int required_perms);

/* Get a host pointer for a guest range, capped to a caller-provided limit.
 * *avail receives the number of contiguous bytes available from gva, up to
 * len_limit. Returns NULL if the starting address is unmapped or lacks perms.
 */
void *guest_ptr_bound(const guest_t *g,
                      uint64_t gva,
                      uint64_t *avail,
                      int required_perms,
                      uint64_t len_limit);

/* Bounds-checked copy from guest memory to host buffer.
 * Returns 0 on success, -1 if out of bounds.
 */
int guest_read(const guest_t *g, uint64_t gva, void *dst, size_t len);

/* Optimized guest-to-host copy for small fixed-size inputs.
 * Uses a direct guest pointer when the full range is contiguous and readable,
 * otherwise falls back to guest_read() for boundary-crossing safety.
 */
int guest_read_small(const guest_t *g, uint64_t gva, void *dst, size_t len);

/* Bounds-checked copy from host buffer into guest memory.
 * Returns 0 on success, -1 if out of bounds.
 */
int guest_write(guest_t *g, uint64_t gva, const void *src, size_t len);

/* Optimized host-to-guest copy for small fixed-size outputs.
 * Uses a direct guest pointer when the full range is contiguous and writable,
 * otherwise falls back to guest_write() for boundary-crossing safety.
 */
int guest_write_small(guest_t *g, uint64_t gva, const void *src, size_t len);

/* Read a null-terminated string from guest memory.
 * Copies up to max-1 bytes + NUL into dst.
 * Returns string length or -1 if out of bounds / unterminated.
 */
int guest_read_str(const guest_t *g, uint64_t gva, char *dst, size_t max);

/* Optimized guest string read for short, contiguous paths.
 * Uses a direct guest pointer when a full max-1-byte window is readable,
 * otherwise falls back to guest_read_str() for boundary-safe scanning.
 */
int guest_read_str_small(const guest_t *g, uint64_t gva, char *dst, size_t max);

/* Build L0->L1->L2 page tables from an array of memory regions.
 * Uses 2MiB block descriptors. Returns the TTBR0 value (GPA of L0 table),
 * or 0 on failure.
 */
uint64_t guest_build_page_tables(guest_t *g,
                                 const mem_region_t *regions,
                                 int n);

/* Extend page tables to cover a new address range [start, end) with 2MiB
 * block descriptors. Reuses the existing L0->L1 table structure and
 * allocates new L2 tables as needed. Records a TLBI request covering the
 * affected range (range or broadcast).
 * Returns 0 on success, -1 on failure.
 */
int guest_extend_page_tables(guest_t *g,
                             uint64_t start,
                             uint64_t end,
                             int perms);

/* Split a 2MiB block descriptor into 512 x 4KiB L3 page descriptors.
 * block_gpa must be within a currently-mapped 2MiB block. The block's
 * permissions are inherited by all 512 page entries. If the block is
 * already split (L2 entry is a table descriptor), this is a no-op.
 *
 * No TLBI request is issued: the split alone preserves every VA->PA
 * translation in the block (each L3 page descriptor inherits the block's
 * permissions). Every caller follows the split with guest_invalidate_ptes
 * or guest_update_perms on the actually-changing range; that subsequent
 * call records the TLBI, and TLBI VAE1IS for any VA in the block also
 * invalidates the cached 2 MiB block entry covering that VA (ARM ARM
 * B2.2.5.6), so a single per-page TLBI suffices to retire the stale
 * block translation as soon as any affected page is accessed.
 *
 * Returns 0 on success, -1 on failure.
 */
int guest_split_block(guest_t *g, uint64_t block_gpa);

/* Invalidate page table entries for the range [start, end).
 * Sets L2 block descriptors and L3 page descriptors to 0 (invalid),
 * causing translation faults on access. Used when mprotect sets
 * PROT_NONE; the correct behavior is for the guest to fault.
 * If a 2MiB block is only partially invalidated, the block is split
 * into L3 pages first (preserving the non-invalidated pages).
 * Records a TLBI request covering the invalidated range.
 * Returns 0 on success, -1 on failure.
 */
int guest_invalidate_ptes(guest_t *g, uint64_t start, uint64_t end);

/* Update page table permissions for the range [start, end).
 * If a 2MiB block needs mixed permissions (only part of it is being
 * updated), the block is automatically split into 4KiB L3 pages first.
 * If the entire 2MiB block is being updated, the block descriptor is
 * modified in place without splitting.
 * perms is a MEM_PERM_R/W/X combination. Records a TLBI request only for
 * pages whose descriptor actually changed.
 * Returns 0 on success, -1 on failure.
 */
int guest_update_perms(guest_t *g, uint64_t start, uint64_t end, int perms);

/* Reset guest memory for execve. Zeros ELF, brk, stack, mmap regions and
 * resets page table pool, brk, and mmap allocation state. Preserves the
 * host_base mapping and VM/vCPU handles.
 */
void guest_reset(guest_t *g);

/* A used memory region for fork state transfer */
typedef struct {
    uint64_t offset; /* Offset from host_base (0-based) */
    uint64_t size;   /* Size in bytes */
} used_region_t;

/* Enumerate used memory regions for fork state transfer.
 * Writes up to max entries into out[]. Returns the count written.
 * shim_size is the shim binary size (needed to determine shim region).
 */
int guest_get_used_regions(const guest_t *g,
                           unsigned int shim_size,
                           used_region_t *out,
                           int max);

/* Semantic region tracking API. */

/* Add a region to the sorted tracking array. Overlapping regions are NOT
 * merged; each call adds a distinct entry. Returns 0 on success, -1 if the
 * region table is full or backing fd duplication fails.
 */
int guest_region_add(guest_t *g,
                     uint64_t start,
                     uint64_t end,
                     int prot,
                     int flags,
                     uint64_t offset,
                     const char *name);
int guest_region_add_ex(guest_t *g,
                        uint64_t start,
                        uint64_t end,
                        int prot,
                        int flags,
                        uint64_t offset,
                        const char *name,
                        int backing_fd);
/* Like guest_region_add_ex, but consumes owned_backing_fd on success or
 * failure.
 */
int guest_region_add_ex_owned(guest_t *g,
                              uint64_t start,
                              uint64_t end,
                              int prot,
                              int flags,
                              uint64_t offset,
                              const char *name,
                              int owned_backing_fd);

/* Remove all region coverage in [start, end). Regions fully contained are
 * deleted; partially overlapping regions are trimmed or split.
 */
void guest_region_remove(guest_t *g, uint64_t start, uint64_t end);

/* Find the region containing addr. Returns a pointer to the region (inside
 * the guest_t regions array) or NULL if addr is not in any tracked region.
 */
const guest_region_t *guest_region_find(const guest_t *g, uint64_t addr);

/* Update protection bits for all region coverage in [start, end).
 * Splits regions at boundaries as needed.
 */
void guest_region_set_prot(guest_t *g, uint64_t start, uint64_t end, int prot);

/* Try to materialize a lazy (MAP_NORESERVE) page at the given offset.
 * Called from the data/instruction abort handler when the faulting address
 * falls within a noreserve region. Creates page table entries for one 2MiB
 * block containing the fault address, zeros the memory, and clears the
 * noreserve flag for the materialized sub-range.
 * Returns 0 on success (caller should TLBI and retry), -1 if the offset is not
 * in a noreserve region.
 */
int guest_materialize_lazy(guest_t *g, uint64_t fault_offset);
