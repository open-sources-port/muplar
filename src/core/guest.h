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
 * Guest memory size is determined dynamically from the VM's IPA width
 * (36-bit = 64GiB on M2, 40-bit = 1TiB on M3+). See guest.c for the
 * runtime probe that selects the correct size.
 */

#define PT_POOL_BASE 0x00010000ULL     /* Page table pool start */
#define PT_POOL_END 0x00100000ULL      /* Page table pool end (960KiB) */
#define SHIM_BASE 0x00100000ULL        /* Shim code (2MiB block, RX) */
#define SHIM_DATA_BASE 0x00200000ULL   /* Shim stack/data (2MiB block, RW) */
#define ELF_DEFAULT_BASE 0x00400000ULL /* Typical ELF load base */
#define PIE_LOAD_BASE 0x00400000ULL    /* PIE (ET_DYN) executable base (4MiB) */
#define BRK_BASE_DEFAULT 0x01000000ULL /* Default brk start (16MiB) */

/* 8MiB stack (four 2MiB blocks); unused HVF backing pages consume no RAM. */
#define STACK_SIZE 0x00800000ULL

/* Used when brk_start is below 128MiB; otherwise placed above brk. */
#define STACK_TOP_DEFAULT 0x08000000ULL
#define STACK_GUARD_SIZE 0x00001000ULL /* 4KiB guard at stack bottom */

/* mmap RX region for PROT_EXEC; placed below 8GiB to leave the high mmap
 * region clear for runtimes that demand a specific minimum heap address.
 */
#define MMAP_RX_BASE 0x10000000ULL

/* Initial pre-mapped mmap RX end. Only covers the first 2MiB block;
 * additional pages are mapped lazily by guest_extend_page_tables()
 * when sys_mmap needs more PROT_EXEC space. Reduces startup time
 * and memory pressure for small binaries that never call mmap.
 */
#define MMAP_RX_INITIAL_END (MMAP_RX_BASE + 0x200000ULL) /* +2MiB */

/* mmap RW region starts at 8GiB to match real Linux address layouts. */
#define MMAP_BASE 0x200000000ULL

/* Initial pre-mapped mmap RW end. Only covers the first 2MiB block;
 * additional pages are mapped lazily by guest_extend_page_tables().
 */
#define MMAP_INITIAL_END (MMAP_BASE + 0x200000ULL) /* +2MiB */

/* mmap_limit and interp_base are computed dynamically from guest_size
 * in main.c and stored in guest_t.
 */
#define BLOCK_2MIB (2ULL * 1024 * 1024)

/* IPA base: guest memory is mapped at this IPA in the hypervisor.
 * All guest physical addresses = GUEST_IPA_BASE + offset.
 * Must be 0 so that guest virtual addresses match ELF link addresses
 * (e.g. 0x400000). A non-zero IPA base would require all ELF binaries
 * to be linked at IPA_BASE+vaddr, which is impractical.
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
 * coalesced (see regions_mergeable in core/guest.c). Threaded runtimes
 * create many thread stacks with guard pages; with coalescing, typical
 * workloads use ~50 regions. 4096 provides ample headroom for edge cases
 * (many interleaved guard pages, file-backed mappings, etc.).
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
                          * of backing_fd at host_base+start. The kernel's
                          * page cache keeps it coherent with the file and
                          * with peer overlays of the same file, so msync
                          * skips the snapshot-style pwrite-the-diff and
                          * refresh-from-file paths for these regions. */
    uint64_t overlay_start; /* Host-page-aligned overlay start. May extend
                             * outside [start, end) when only part of a host
                             * page is guest-visible. */
    uint64_t overlay_end;   /* Host-page-aligned overlay end (exclusive). */
    char name[64];          /* Label: "[heap]", "[stack]", ELF path, etc. */
} guest_region_t;

/* Guest state. */
typedef struct {
    void *host_base; /* Host pointer to allocated guest memory */
    int shm_fd; /* File fd backing host_base for CoW fork (-1 if MAP_ANON) */

    uint64_t guest_size; /* Total size (determined by IPA capacity) */
    uint64_t ipa_base;   /* IPA base for hv_vm_map (GUEST_IPA_BASE) */
    uint64_t mmap_limit; /* Max mmap address (computed from guest_size) */

    uint64_t interp_base;  /* Dynamic linker load base (from guest_size) */
    uint64_t pt_pool_next; /* Next free page table page in pool */
    uint64_t brk_base;     /* Initial brk (set after ELF load) */
    uint64_t brk_current;  /* Current brk position */
    uint64_t stack_base;   /* Bottom of stack region (dynamic, above brk) */
    uint64_t stack_top;    /* Top of stack (stack grows down from here) */

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

    uint64_t ttbr0; /* TTBR0 value (IPA of L0 page table) */
    bool need_tlbi; /* Signal shim to flush TLB after page table changes */
    hv_vcpu_t vcpu; /* vCPU handle */
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

/* Convert a guest offset (0-based) to an IPA/VA (ipa_base + offset) */
static inline uint64_t guest_ipa(const guest_t *g, uint64_t offset)
{
    return g->ipa_base + offset;
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
 * allocates new L2 tables as needed. Sets g->need_tlbi = true.
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
 * Sets g->need_tlbi = true. Returns 0 on success, -1 on failure.
 */
int guest_split_block(guest_t *g, uint64_t block_gpa);

/* Invalidate page table entries for the range [start, end).
 * Sets L2 block descriptors and L3 page descriptors to 0 (invalid),
 * causing translation faults on access. Used when mprotect sets
 * PROT_NONE; the correct behavior is for the guest to fault.
 * If a 2MiB block is only partially invalidated, the block is split
 * into L3 pages first (preserving the non-invalidated pages).
 * Sets g->need_tlbi = true. Returns 0 on success, -1 on failure.
 */
int guest_invalidate_ptes(guest_t *g, uint64_t start, uint64_t end);

/* Update page table permissions for the range [start, end).
 * If a 2MiB block needs mixed permissions (only part of it is being
 * updated), the block is automatically split into 4KiB L3 pages first.
 * If the entire 2MiB block is being updated, the block descriptor is
 * modified in place without splitting.
 * perms is a MEM_PERM_R/W/X combination. Sets g->need_tlbi = true.
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
