/* Guest memory management
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Identity-mapped guest memory: GVA == GPA == offset into host_base.
 * The guest address space size is determined by the VM's configured IPA width
 * (capped at 40-bit = 1TiB): 64GiB for native aarch64 on M2 (36-bit), 1TiB for
 * M3+ (40-bit). Reserved via mmap(MAP_ANON); macOS demand-pages physical memory
 * on first touch, so only used pages consume RAM. The slab is mapped RWX to
 * Hypervisor.framework. The guest's own page tables (built here) enforce
 * per-region permissions using 2MiB block descriptors, which are mandatory for
 * transparent misaligned access. Page tables can be extended at runtime via
 * guest_extend_page_tables().
 *
 * PROT_NONE mappings in the primary address space (used by managed runtimes for
 * heap reservation) do NOT get page table entries; the translation fault is the
 * correct behavior. When mprotect changes an accessible region to PROT_NONE,
 * guest_invalidate_ptes() removes existing page table entries. Page tables are
 * created on demand when mprotect changes PROT_NONE to an accessible
 * permission.
 *
 * Page table format: AArch64 4KiB granule, up to 4-level:
 *   L0 entry covers 512GiB: multiple entries for >512GiB address spaces
 *   L1 entry covers 1GiB:   either block or table pointing to L2
 *   L2 entry covers 2MiB:   block descriptors with final permissions
 *   L3 entry covers 4KiB:   optional, created by guest_split_block() for mixed
 *                           permissions within a 2MiB block (W^X)
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

#include "core/guest.h"
#include "debug/log.h"
#include "utils.h"
#include "runtime/thread.h" /* thread_destroy_all_vcpus */

static void guest_region_clear(guest_t *g);

/* Page table descriptor bits. */
#define PT_VALID (1ULL << 0)
#define PT_TABLE (1ULL << 1)     /* Table descriptor (L0/L1/L2) */
#define PT_BLOCK (1ULL << 0)     /* Block descriptor (L1/L2): valid bit only */
#define PT_AF (1ULL << 10)       /* Access Flag */
#define PT_SH_ISH (3ULL << 8)    /* Inner Shareable */
#define PT_NS (1ULL << 5)        /* Non-Secure */
#define PT_ATTR1 (1ULL << 2)     /* MAIR index 1: Normal WB cacheable */
#define PT_UXN (1ULL << 54)      /* Unprivileged Execute Never */
#define PT_PXN (1ULL << 53)      /* Privileged Execute Never */
#define PT_AP_RW_EL0 (1ULL << 6) /* AP[2:1]=01: RW at EL1, RW at EL0 */
#define PT_AP_RO (3ULL << 6)     /* AP[2:1]=11: RO at EL1, RO at EL0 */

/* PAGE_SIZE / ALIGN_2MB_* live in utils.h; BLOCK_2MIB lives in core/guest.h. */
#define PAGE_SIZE GUEST_PAGE_SIZE
#define BLOCK_1GIB (1ULL * 1024 * 1024 * 1024)

/* Mask to extract the physical address from a 2MiB L2 block descriptor */
#define L2_BLOCK_ADDR_MASK 0xFFFFFFE00000ULL

/* Forward declaration (defined in the page table section below) */
static int desc_to_perms(uint64_t desc);

/* Page table pool allocator. */

/* Protects the page table pool bump allocator. Multiple threads may
 * trigger page table extension concurrently (via mmap/brk/mprotect).
 */
static pthread_mutex_t pt_lock = PTHREAD_MUTEX_INITIALIZER; /* Lock order: 2 */

/* Track whether the 80% warning has been emitted (avoid log spam) */
static bool pt_pool_warned = false;

static size_t guest_host_page_size_cached(void)
{
    static size_t cached;
    if (!cached) {
        long s = sysconf(_SC_PAGESIZE);
        cached = (s > 0) ? (size_t) s : GUEST_PAGE_SIZE;
    }
    return cached;
}

static void guest_region_clear_overlay(guest_region_t *r)
{
    r->overlay_active = false;
    r->overlay_start = 0;
    r->overlay_end = 0;
}

static void guest_region_clip_overlay(guest_region_t *r)
{
    if (!r->overlay_active || r->end <= r->start) {
        guest_region_clear_overlay(r);
        return;
    }

    size_t hps = guest_host_page_size_cached();
    uint64_t page_start = ALIGN_DOWN(r->start, hps);
    uint64_t page_end = ALIGN_UP(r->end, hps);
    uint64_t overlay_start =
        r->overlay_start > page_start ? r->overlay_start : page_start;
    uint64_t overlay_end =
        r->overlay_end < page_end ? r->overlay_end : page_end;

    if (overlay_end <= overlay_start) {
        guest_region_clear_overlay(r);
        return;
    }

    r->overlay_start = overlay_start;
    r->overlay_end = overlay_end;
}

/* Allocate a zeroed 4KiB page from the page table pool.
 * Returns GPA of the page, or 0 on pool exhaustion.
 * Acquires pt_lock internally. Caller typically holds mmap_lock.
 */
static uint64_t pt_alloc_page(guest_t *g)
{
    pthread_mutex_lock(&pt_lock);
    if (g->pt_pool_next + PAGE_SIZE > PT_POOL_END) {
        log_error(
            "guest: page table pool exhausted "
            "(used %llu / %llu bytes)",
            (unsigned long long) (g->pt_pool_next - PT_POOL_BASE),
            (unsigned long long) (PT_POOL_END - PT_POOL_BASE));
        pthread_mutex_unlock(&pt_lock);
        return 0;
    }
    uint64_t gpa = g->pt_pool_next;
    g->pt_pool_next += PAGE_SIZE;

    /* Warn at 80% pool usage so users can anticipate exhaustion */
    uint64_t used = gpa + PAGE_SIZE - PT_POOL_BASE;
    uint64_t total = PT_POOL_END - PT_POOL_BASE;
    if (!pt_pool_warned && used > (total * 4 / 5)) {
        log_debug(
            "guest: page table pool at %llu%% "
            "(%llu / %llu bytes)",
            (unsigned long long) (used * 100 / total),
            (unsigned long long) used, (unsigned long long) total);
        pt_pool_warned = true;
    }

    /* Zero the page while still holding the lock so no other thread can
     * observe a partially-zeroed page table page.
     */
    memset((uint8_t *) g->host_base + gpa, 0, PAGE_SIZE);
    pthread_mutex_unlock(&pt_lock);
    return gpa;
}

/* Get host pointer to a page table entry array at a given GPA */
static uint64_t *pt_at(const guest_t *g, uint64_t gpa)
{
    return (uint64_t *) ((uint8_t *) g->host_base + gpa);
}

/* Public API */

int guest_init(guest_t *g, uint64_t size, uint32_t ipa_bits)
{
    memset(g, 0, sizeof(*g));
    g->shm_fd = -1;
    g->ipa_base = GUEST_IPA_BASE;
    g->pt_pool_next = PT_POOL_BASE;
    g->brk_base = BRK_BASE_DEFAULT;
    g->brk_current = BRK_BASE_DEFAULT;
    g->mmap_next = MMAP_BASE;
    g->mmap_rx_next = MMAP_RX_BASE;

    /* Query the maximum IPA size supported by the hardware/kernel. macOS 15+
     * on Apple Silicon reports 40 bits (1TiB). Older versions or fallback
     * yields 36 bits (64GiB).
     */
    uint32_t max_ipa = 0;
    hv_vm_config_get_max_ipa_size(&max_ipa);
    if (max_ipa < 36)
        max_ipa = 36;

    /* Determine VM IPA width.
     * ipa_bits = 0 : auto-detect (40-bit on macOS 15+, else 36-bit).
     * ipa_bits > 0 : use that exact value.
     */
    uint32_t vm_ipa;
    if (ipa_bits > 0)
        vm_ipa = ipa_bits;
    else if (max_ipa >= 40)
        vm_ipa = 40;
    else
        vm_ipa = 36;

    /* Primary buffer size: use the VM's configured IPA width (capped at
     * 40-bit = 1TiB). macOS demand-pages the host reservation, so only touched
     * pages cost physical memory.
     */
    uint32_t buf_bits = (vm_ipa > 40) ? 40 : vm_ipa;
    uint64_t buf_capacity = 1ULL << buf_bits;
    if (size == 0 || size > buf_capacity)
        size = buf_capacity;
    g->guest_size = size;
    g->ipa_bits = vm_ipa;

    /* Compute dynamic layout limits from primary buffer size.
     * interp_base: last 4GiB (dynamic linker load address)
     * mmap_limit:  last 8GiB reserved (max mmap RW address)
     * For 64GiB:   interp=60GiB, mmap_limit=56GiB
     * For 1TiB:    interp=1020GiB, mmap_limit=1016GiB
     */
    g->interp_base = g->guest_size - 0x100000000ULL;
    g->mmap_limit = g->guest_size - 0x200000000ULL;

    /* Reserve primary address space via mmap(MAP_ANON). macOS demand-pages
     * this: physical pages are allocated only on first touch, so reserving up
     * to 1TiB costs nothing until pages are actually used. Do NOT memset
     * because that would touch all pages and defeat demand paging.
     */
    g->host_base =
        mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (g->host_base == MAP_FAILED) {
        perror("guest: mmap");
        g->host_base = NULL;
        return -1;
    }

    /* Upgrade to file-backed shared memory for CoW fork support.
     * mkstemp + unlink + ftruncate + MAP_SHARED|MAP_FIXED replaces the
     * anonymous mapping with file-backed memory at the same host address.
     * At fork time, the parent stays on MAP_SHARED (HVF caches VA->PA, so
     * remapping would cause stale reads) and sends the file fd to the child.
     * The child maps it MAP_PRIVATE, giving it an instant copy-on-write
     * clone of all guest memory.
     *
     * macOS rejects MAP_PRIVATE on shm_open objects (EINVAL), but regular
     * file fds support MAP_SHARED, MAP_PRIVATE, and MAP_PRIVATE|MAP_FIXED
     * correctly. The file is unlinked immediately; the fd keeps it alive.
     * macOS demand-pages file mappings, so untouched pages cost nothing.
     * If any step fails, guest memory silently keeps the MAP_ANON mapping and
     * falls back to the IPC region-copy path on fork.
     */
    {
        char tmppath[] = "/tmp/elfuse-XXXXXX";
        int sfd = mkstemp(tmppath);
        if (sfd >= 0) {
            unlink(tmppath); /* Unlink immediately; fd keeps file alive */
            if (ftruncate(sfd, (off_t) size) == 0) {
                void *p = mmap(g->host_base, size, PROT_READ | PROT_WRITE,
                               MAP_SHARED | MAP_FIXED, sfd, 0);
                if (p != MAP_FAILED) {
                    g->shm_fd = sfd;
                } else {
                    /* MAP_FIXED failed; keep the original MAP_ANON mapping */
                    close(sfd);
                }
            } else {
                close(sfd);
            }
        }
        /* If shm_fd is still -1, guest memory is on MAP_ANON; fork uses IPC
         * copy.
         */
    }

    /* Create Hypervisor VM with the determined IPA width and map the
     * primary slab at GUEST_IPA_BASE.
     *
     * macOS may not release HVF VM resources immediately after
     * hv_vm_destroy(), so rapid sequential VM creation (e.g. running
     * many test binaries) can hit transient resource exhaustion.
     * Retry with linear backoff (500ms intervals, up to 30 attempts =
     * 15 seconds max wait) to handle this gracefully.
     */
    hv_return_t ret = HV_ERROR;
    for (int attempt = 0; attempt < 30; attempt++) {
        hv_vm_config_t config = hv_vm_config_create();
        hv_vm_config_set_ipa_size(config, vm_ipa);
        ret = hv_vm_create(config);
        os_release(config);
        if (ret == HV_SUCCESS)
            break;
        usleep(500000); /* 500ms between attempts */
    }
    if (ret != HV_SUCCESS) {
        log_error("guest: hv_vm_create failed: %d (ipa_bits=%u)", (int) ret,
                  vm_ipa);
        munmap(g->host_base, size);
        g->host_base = NULL;
        if (g->shm_fd >= 0) {
            close(g->shm_fd);
            g->shm_fd = -1;
        }
        return -1;
    }

    ret = hv_vm_map(g->host_base, GUEST_IPA_BASE, size,
                    HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);
    if (ret != HV_SUCCESS && buf_bits > max_ipa) {
        /* 1TiB primary map failed; fall back to hardware-default buffer.
         * This handles undocumented HVF limits on primary buffer size.
         * Close shm_fd since the fallback uses anonymous memory (the file is no
         * longer mapped to host_base, so CoW fork cannot work).
         */
        log_info(
            "guest: hv_vm_map %llu GiB failed (%d), "
            "retrying with %u-bit (%llu GiB)",
            (unsigned long long) (size >> 30), (int) ret, max_ipa,
            1ULL << (max_ipa - 30));
        munmap(g->host_base, size);
        if (g->shm_fd >= 0) {
            close(g->shm_fd);
            g->shm_fd = -1;
        }
        buf_bits = (max_ipa > 40) ? 40 : max_ipa;
        size = 1ULL << buf_bits;
        g->guest_size = size;
        g->interp_base = size - 0x100000000ULL;
        g->mmap_limit = size - 0x200000000ULL;
        g->host_base = mmap(NULL, size, PROT_READ | PROT_WRITE,
                            MAP_ANON | MAP_PRIVATE, -1, 0);
        if (g->host_base == MAP_FAILED) {
            perror("guest: mmap (fallback)");
            g->host_base = NULL;
            hv_vm_destroy();
            return -1;
        }
        ret = hv_vm_map(g->host_base, GUEST_IPA_BASE, size,
                        HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);
    }
    if (ret != HV_SUCCESS) {
        log_error("guest: hv_vm_map failed: %d", (int) ret);
        hv_vm_destroy();
        munmap(g->host_base, size);
        g->host_base = NULL;
        if (g->shm_fd >= 0) {
            close(g->shm_fd);
            g->shm_fd = -1;
        }
        return -1;
    }

    /* Seed HVF segment list with one entry covering the whole slab.
     * sys_mmap may later split this for MAP_SHARED file overlays.
     */
    g->segments[0] = (hvf_segment_t) {.ipa = GUEST_IPA_BASE, .len = size};
    g->n_segments = 1;

    return 0;
}

int guest_init_from_shm(guest_t *g,
                        int shm_fd,
                        uint64_t size,
                        uint32_t ipa_bits)
{
    memset(g, 0, sizeof(*g));
    g->shm_fd = -1; /* Child does not own the shm */
    g->ipa_base = GUEST_IPA_BASE;
    g->pt_pool_next = PT_POOL_BASE;
    g->brk_base = BRK_BASE_DEFAULT;
    g->brk_current = BRK_BASE_DEFAULT;
    g->mmap_next = MMAP_BASE;
    g->mmap_rx_next = MMAP_RX_BASE;
    g->guest_size = size;
    g->ipa_bits = ipa_bits;

    /* Compute layout limits (same formula as guest_init) */
    g->interp_base = size - 0x100000000ULL;
    g->mmap_limit = size - 0x200000000ULL;

    /* Map the shm fd MAP_PRIVATE: copy-on-write semantics. Reads see
     * the parent's frozen snapshot; writes are private to this process.
     * macOS CoW is page-granular: only modified pages are duplicated.
     */
    g->host_base =
        mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, shm_fd, 0);
    if (g->host_base == MAP_FAILED) {
        perror("guest: mmap shm");
        g->host_base = NULL;
        close(shm_fd);
        return -1;
    }

    /* Close the shm fd; the mapping keeps the pages alive */
    close(shm_fd);

    /* Create HVF VM with the same IPA width as the parent */
    hv_return_t ret = HV_ERROR;
    for (int attempt = 0; attempt < 30; attempt++) {
        hv_vm_config_t config = hv_vm_config_create();
        hv_vm_config_set_ipa_size(config, ipa_bits);
        ret = hv_vm_create(config);
        os_release(config);
        if (ret == HV_SUCCESS)
            break;
        usleep(500000);
    }
    if (ret != HV_SUCCESS) {
        log_error("guest: hv_vm_create (shm) failed: %d", (int) ret);
        munmap(g->host_base, size);
        g->host_base = NULL;
        return -1;
    }

    ret = hv_vm_map(g->host_base, GUEST_IPA_BASE, size,
                    HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);
    if (ret != HV_SUCCESS) {
        log_error("guest: hv_vm_map (shm) failed: %d", (int) ret);
        hv_vm_destroy();
        munmap(g->host_base, size);
        g->host_base = NULL;
        return -1;
    }

    /* Seed HVF segment list. The child re-establishes any per-region file
     * overlays the parent had after this call (handled by fork-state
     * deserialization).
     */
    g->segments[0] = (hvf_segment_t) {.ipa = GUEST_IPA_BASE, .len = size};
    g->n_segments = 1;

    log_debug(
        "guest: CoW fork: mapped %llu GiB from shm "
        "(ipa=%u bits)",
        (unsigned long long) (size / (1024ULL * 1024 * 1024)), ipa_bits);

    return 0;
}

void guest_destroy(guest_t *g)
{
    /* Destroy all worker vCPUs (thread table) before tearing down the VM.
     * This prevents hv_vm_destroy from racing with active vCPUs that may still
     * be running if thread join timed out during exit_group.
     */
    thread_destroy_all_vcpus();
    if (g->vcpu) {
        hv_vcpu_destroy(g->vcpu);
        g->vcpu = 0;
    }
    /* Unmap each HVF segment. hv_vm_destroy releases all stage-2 state
     * regardless, but unmapping explicitly keeps invariants clean for
     * downstream tools (Instruments, leak detectors).
     */
    for (int i = 0; i < g->n_segments; i++)
        hv_vm_unmap(g->segments[i].ipa, g->segments[i].len);
    g->n_segments = 0;
    hv_vm_destroy();
    if (g->host_base) {
        munmap(g->host_base, g->guest_size);
        g->host_base = NULL;
    }
    for (int i = 0; i < g->nregions; i++) {
        if (g->regions[i].backing_fd >= 0) {
            close(g->regions[i].backing_fd);
            g->regions[i].backing_fd = -1;
        }
    }
    g->nregions = 0;
    /* Close the shm fd if guest memory owns one (parent with shm backing) */
    if (g->shm_fd >= 0) {
        close(g->shm_fd);
        g->shm_fd = -1;
    }
}

typedef struct {
    uint64_t gpa, chunk;
} gva_translation_t;

/* Per-thread GVA TLB cache.
 *
 * Single-entry translation cache: avoids 3-4 pointer chases through the page
 * table on repeated accesses to the same 2MiB block (or 4KiB page if L3-split).
 * Validated by an atomic generation counter in guest_t that is bumped on every
 * page table modification.
 */
static _Thread_local struct {
    const guest_t *owner; /* Which guest_t this entry belongs to */
    uint64_t base_gva;    /* Block/page-aligned GVA */
    uint64_t base_gpa;    /* Corresponding GPA offset */
    uint64_t size;        /* 2MiB or 4KiB (0 = invalid) */
    int perms;            /* Cached permissions */
    uint64_t gen;         /* guest_t.pt_gen at population time */
} gva_tlb;

static void guest_tlb_flush(void)
{
    gva_tlb.size = 0;
}

static int gva_translate_perm(const guest_t *g,
                              uint64_t gva,
                              int required_perms,
                              gva_translation_t *out)
{
    /* Fast path: check per-thread TLB cache */
    uint64_t gen = atomic_load_explicit(&g->pt_gen, memory_order_acquire);
    if (gva_tlb.size && gva_tlb.owner == g && gva_tlb.gen == gen &&
        gva >= gva_tlb.base_gva && gva - gva_tlb.base_gva < gva_tlb.size &&
        (required_perms & gva_tlb.perms) == required_perms) {
        out->gpa = gva_tlb.base_gpa + (gva - gva_tlb.base_gva);
        out->chunk = (gva_tlb.base_gva + gva_tlb.size) - gva;
        return 0;
    }

    uint64_t base = g->ipa_base;

    const uint64_t *l0 = pt_at(g, g->ttbr0 - base);
    unsigned l0_idx = (unsigned) (gva / (512ULL * BLOCK_1GIB));
    if (l0_idx >= 512 || !(l0[l0_idx] & PT_VALID))
        return -1;

    uint64_t l1_ipa = l0[l0_idx] & 0xFFFFFFFFF000ULL;
    if (l1_ipa < base || l1_ipa - base >= g->guest_size)
        return -1;
    const uint64_t *l1 = pt_at(g, l1_ipa - base);
    unsigned l1_idx = (unsigned) ((gva / BLOCK_1GIB) % 512);
    if (!(l1[l1_idx] & PT_VALID))
        return -1;

    uint64_t l2_ipa = l1[l1_idx] & 0xFFFFFFFFF000ULL;
    if (l2_ipa < base || l2_ipa - base >= g->guest_size)
        return -1;
    const uint64_t *l2 = pt_at(g, l2_ipa - base);
    unsigned l2_idx = (unsigned) ((gva / BLOCK_2MIB) % 512);
    if (!(l2[l2_idx] & PT_VALID))
        return -1;

    if (l2[l2_idx] & PT_TABLE) {
        /* L3 page descriptor: 4KiB granularity. */
        uint64_t l3_ipa = l2[l2_idx] & 0xFFFFFFFFF000ULL;
        if (l3_ipa < base || l3_ipa - base >= g->guest_size)
            return -1;
        const uint64_t *l3 = pt_at(g, l3_ipa - base);
        unsigned l3_idx = (unsigned) ((gva / PAGE_SIZE) % 512);
        if (!(l3[l3_idx] & PT_VALID))
            return -1;

        int perms = desc_to_perms(l3[l3_idx]);
        if ((perms & required_perms) != required_perms)
            return -1;

        uint64_t page_ipa = l3[l3_idx] & 0xFFFFFFFFF000ULL;
        if (page_ipa < base)
            return -1;
        uint64_t gpa = (page_ipa - base) + (gva & (PAGE_SIZE - 1));
        if (gpa >= g->guest_size)
            return -1;

        out->gpa = gpa;
        out->chunk = PAGE_SIZE - (gva & (PAGE_SIZE - 1));

        /* Populate TLB cache for this 4KiB page */
        gva_tlb.owner = g;
        gva_tlb.base_gva = gva & ~(PAGE_SIZE - 1);
        gva_tlb.base_gpa = page_ipa - base;
        gva_tlb.size = PAGE_SIZE;
        gva_tlb.perms = perms;
        gva_tlb.gen = gen;
        return 0;
    }

    /* L2 block descriptor: 2MiB granularity. */
    int perms = desc_to_perms(l2[l2_idx]);
    if ((perms & required_perms) != required_perms)
        return -1;

    uint64_t block_ipa = l2[l2_idx] & L2_BLOCK_ADDR_MASK;
    if (block_ipa < base)
        return -1;
    uint64_t gpa = (block_ipa - base) + (gva & (BLOCK_2MIB - 1));
    if (gpa >= g->guest_size)
        return -1;

    out->gpa = gpa;
    out->chunk = BLOCK_2MIB - (gva & (BLOCK_2MIB - 1));

    /* Populate TLB cache for this 2MiB block */
    gva_tlb.owner = g;
    gva_tlb.base_gva = gva & ~(BLOCK_2MIB - 1);
    gva_tlb.base_gpa = block_ipa - base;
    gva_tlb.size = BLOCK_2MIB;
    gva_tlb.perms = perms;
    gva_tlb.gen = gen;
    return 0;
}

static uint64_t gva_contiguous_avail(const guest_t *g,
                                     uint64_t gva,
                                     int required_perms,
                                     const gva_translation_t *first,
                                     uint64_t limit)
{
    uint64_t total = 0, next_gva = gva;
    uint64_t expect_gpa = first->gpa;
    gva_translation_t cur = *first;

    if (limit == 0)
        return 0;

    for (;;) {
        uint64_t chunk = cur.chunk;
        if (chunk > g->guest_size - cur.gpa)
            chunk = g->guest_size - cur.gpa;
        if (chunk > limit - total)
            chunk = limit - total;

        total += chunk;
        if (total == limit)
            break;
        if (chunk < cur.chunk)
            break;
        if (next_gva > UINT64_MAX - chunk)
            break;
        next_gva += chunk;
        if (expect_gpa > UINT64_MAX - chunk)
            break;
        expect_gpa += chunk;

        if (gva_translate_perm(g, next_gva, required_perms, &cur) < 0)
            break;
        if (cur.gpa != expect_gpa)
            break;
    }

    return total;
}

/* Resolve a guest virtual address to a host pointer and available size.
 * Returns NULL if the address is not in any mapping.
 *
 * If avail is non-NULL, stores the number of physically contiguous bytes from
 * gva whose page-table entries are valid and satisfy required_perms
 * (MEM_PERM_R/W/X bitmask).  The walk continues across adjacent L2/L3 entries
 * until a mapping, permission, or physical-contiguity break is found.
 */
static void *gva_resolve_perm(const guest_t *g,
                              uint64_t gva,
                              uint64_t *avail,
                              int required_perms,
                              uint64_t avail_limit)
{
    /* Always walk page tables to enforce permissions.  The guest slab is
     * identity-mapped (GVA == GPA == offset), but L2 block descriptors carry
     * permission bits and L3 page tables have per-4KiB permissions after
     * guest_split_block.  Skipping the walk would bypass W^X enforcement for
     * all normal guest addresses.
     */
    gva_translation_t first;
    if (gva_translate_perm(g, gva, required_perms, &first) < 0)
        return NULL;

    if (avail) {
        *avail =
            gva_contiguous_avail(g, gva, required_perms, &first, avail_limit);
    }
    return (uint8_t *) g->host_base + first.gpa;
}

void *guest_ptr(const guest_t *g, uint64_t gva)
{
    return gva_resolve_perm(g, gva, NULL, MEM_PERM_R, UINT64_MAX);
}

void *guest_ptr_w(const guest_t *g, uint64_t gva)
{
    return gva_resolve_perm(g, gva, NULL, MEM_PERM_W, UINT64_MAX);
}

void *guest_ptr_avail(const guest_t *g,
                      uint64_t gva,
                      uint64_t *avail,
                      int required_perms)
{
    return gva_resolve_perm(g, gva, avail, required_perms, UINT64_MAX);
}

void *guest_ptr_bound(const guest_t *g,
                      uint64_t gva,
                      uint64_t *avail,
                      int required_perms,
                      uint64_t len_limit)
{
    return gva_resolve_perm(g, gva, avail, required_perms, len_limit);
}

static inline int guest_copy(const guest_t *g,
                             uint64_t gva,
                             void *dst,
                             const void *src,
                             size_t len,
                             int required_perms)
{
    if (len == 0)
        return 0;
    if ((required_perms == MEM_PERM_R && !dst) ||
        (required_perms == MEM_PERM_W && !src))
        return -1;
    if (gva > UINT64_MAX - len)
        return -1;

    size_t copied = 0;
    while (copied < len) {
        uint64_t avail;
        void *ptr = gva_resolve_perm(g, gva + copied, &avail, required_perms,
                                     (uint64_t) (len - copied));
        if (!ptr)
            return -1;
        size_t chunk = len - copied;
        if (chunk > avail)
            chunk = avail;
        if (required_perms == MEM_PERM_R)
            memcpy((uint8_t *) dst + copied, ptr, chunk);
        else
            memcpy(ptr, (const uint8_t *) src + copied, chunk);
        copied += chunk;
    }
    return 0;
}

int guest_read(const guest_t *g, uint64_t gva, void *dst, size_t len)
{
    return guest_copy(g, gva, dst, NULL, len, MEM_PERM_R);
}

int guest_read_small(const guest_t *g, uint64_t gva, void *dst, size_t len)
{
    uint64_t avail = 0;
    void *src = guest_ptr_bound(g, gva, &avail, MEM_PERM_R, (uint64_t) len);
    if (src && avail >= len) {
        memcpy(dst, src, len);
        return 0;
    }

    return guest_read(g, gva, dst, len);
}

int guest_write(guest_t *g, uint64_t gva, const void *src, size_t len)
{
    return guest_copy(g, gva, NULL, src, len, MEM_PERM_W);
}

int guest_write_small(guest_t *g, uint64_t gva, const void *src, size_t len)
{
    uint64_t avail = 0;
    void *dst = guest_ptr_bound(g, gva, &avail, MEM_PERM_W, (uint64_t) len);
    if (dst && avail >= len) {
        memcpy(dst, src, len);
        return 0;
    }

    return guest_write(g, gva, src, len);
}

int guest_read_str(const guest_t *g, uint64_t gva, char *dst, size_t max)
{
    if (max == 0)
        return -1;
    size_t copied = 0, limit = max - 1;

    while (copied < limit) {
        if (gva > UINT64_MAX - copied)
            break;
        uint64_t avail;
        void *ptr = gva_resolve_perm(g, gva + copied, &avail, MEM_PERM_R,
                                     (uint64_t) (limit - copied));
        if (!ptr)
            break;

        size_t remain = limit - copied, chunk = avail < remain ? avail : remain;
        const char *src = (const char *) ptr;

        const void *nul = memchr(src, '\0', chunk);
        if (nul) {
            size_t slen = (const char *) nul - src;
            memcpy(dst + copied, src, slen + 1);
            return (int) (copied + slen);
        }
        memcpy(dst + copied, src, chunk);
        copied += chunk;
    }

    dst[copied] = '\0';
    return -1;
}

int guest_read_str_small(const guest_t *g, uint64_t gva, char *dst, size_t max)
{
    if (max == 0)
        return -1;

    size_t limit = max - 1;
    uint64_t avail = 0;
    const char *src =
        guest_ptr_bound(g, gva, &avail, MEM_PERM_R, (uint64_t) limit);
    if (src && avail >= limit) {
        const char *nul = memchr(src, '\0', limit);
        if (nul) {
            size_t slen = (size_t) (nul - src);
            memcpy(dst, src, slen + 1);
            return (int) slen;
        }
    }

    return guest_read_str(g, gva, dst, max);
}

/* guest_reset. */

void guest_reset(guest_t *g)
{
    /* Zero only actually-used memory regions. With a potentially 1TiB address
     * space, memset of the entire range would fault in all demand-paged memory
     * for no benefit. PROT_NONE regions (e.g., a managed runtime's heap
     * reservation) were never written to, so they're already in the MAP_ANON
     * zero-fill-on-demand state.
     */

    /* Zero tracked regions (ELF segments, heap, stack, mmap allocations).
     * Skip PROT_NONE regions because they were never touched.
     * Skip regions with GPAs beyond the primary buffer.
     */
    for (int i = 0; i < g->nregions; i++) {
        guest_region_t *r = &g->regions[i];
        if (r->prot != 0 /* PROT_NONE */ && r->end > r->start &&
            r->end <= g->guest_size) {
            memset((uint8_t *) g->host_base + r->start, 0, r->end - r->start);
        }
    }

    /* Zero page table pool (not tracked in region array) */
    if (g->pt_pool_next > PT_POOL_BASE)
        memset((uint8_t *) g->host_base + PT_POOL_BASE, 0,
               g->pt_pool_next - PT_POOL_BASE);

    /* Zero shim code + data (not tracked in region array by guest_reset
     * callers; shim regions are added AFTER reset by the exec path)
     */
    memset((uint8_t *) g->host_base + SHIM_BASE, 0,
           SHIM_DATA_BASE + BLOCK_2MIB - SHIM_BASE);

    /* Reset allocation state */
    guest_pt_gen_bump(g);
    guest_tlb_flush();
    __atomic_store_n(&pt_pool_warned, false, __ATOMIC_RELAXED);
    g->pt_pool_next = PT_POOL_BASE;
    g->brk_base = BRK_BASE_DEFAULT;
    g->brk_current = BRK_BASE_DEFAULT;
    g->mmap_next = MMAP_BASE;
    g->mmap_end = MMAP_INITIAL_END;
    g->mmap_rx_next = MMAP_RX_BASE;
    g->mmap_rx_end = MMAP_RX_INITIAL_END;
    g->mmap_rw_gap_hint = 0;
    g->mmap_rx_gap_hint = 0;
    g->ttbr0 = 0;
    g->need_tlbi = false;

    /* Clear semantic region tracking (will be re-populated after exec) */
    guest_region_clear(g);
}

/* Used region enumeration. */

int guest_get_used_regions(const guest_t *g,
                           unsigned int shim_size,
                           used_region_t *out,
                           int max)
{
    int n = 0;

    /* Page table pool */
    if (n < max && g->pt_pool_next > PT_POOL_BASE) {
        out[n].offset = PT_POOL_BASE;
        out[n].size = g->pt_pool_next - PT_POOL_BASE;
        n++;
    }

    /* Shim code */
    if (n < max && shim_size > 0) {
        out[n].offset = SHIM_BASE;
        out[n].size = shim_size;
        n++;
    }

    /* Shim data/stack (full 2MiB block) */
    if (n < max) {
        out[n].offset = SHIM_DATA_BASE;
        out[n].size = BLOCK_2MIB;
        n++;
    }

    /* ELF + brk region: from ELF_DEFAULT_BASE to brk_current.
     * guest memory does not track the exact ELF load range, but static musl
     * binaries always load at or above ELF_DEFAULT_BASE (0x400000).
     */
    if (n < max && g->brk_current > ELF_DEFAULT_BASE) {
        out[n].offset = ELF_DEFAULT_BASE;
        out[n].size = g->brk_current - ELF_DEFAULT_BASE;
        n++;
    }

    /* Stack (dynamic position, stored in guest_t) */
    if (n < max) {
        out[n].offset = g->stack_base;
        out[n].size = g->stack_top - g->stack_base;
        n++;
    }

    /* mmap RW region (up to high-water mark). With the gap-finding allocator,
     * mmap_next is a high-water mark; freed regions within this range may
     * contain PROT_NONE pages (zero-fill, no cost to copy). This is
     * conservative but correct for fork state transfer.
     */
    if (n < max && g->mmap_next > MMAP_BASE) {
        out[n].offset = MMAP_BASE;
        out[n].size = g->mmap_next - MMAP_BASE;
        n++;
    }

    /* mmap RX region (code mappings from dynamic linker) */
    if (n < max && g->mmap_rx_next > MMAP_RX_BASE) {
        out[n].offset = MMAP_RX_BASE;
        out[n].size = g->mmap_rx_next - MMAP_RX_BASE;
        n++;
    }

    return n;
}

/* Semantic region tracking.
 *
 * Check whether two adjacent regions can be merged. They must be contiguous in
 * address space, have identical protection/flags/name, and have contiguous file
 * offsets (so the merged region still represents valid mapping). For anonymous
 * regions the offset is meaningless (always 0, but may become non-zero after
 * split/trim), so the contiguity check is skipped. Without this, adjacent
 * anonymous mmaps (common in megablock-style allocators) each create separate
 * entries that exhaust the region table.
 */
static bool regions_mergeable(const guest_region_t *a, const guest_region_t *b)
{
    if (a->end != b->start)
        return false;
    if (a->prot != b->prot)
        return false;
    if (a->flags != b->flags)
        return false;
    if (a->backing_fd != b->backing_fd)
        return false;

    /* Do not merge noreserve with non-noreserve: the noreserve flag controls
     * lazy page fault behavior and merging would either lose the flag
     * (disabling lazy faults) or apply it to committed pages (causing spurious
     * re-zeroing on faults).
     */
    if (a->noreserve != b->noreserve)
        return false;
    if (a->overlay_active || b->overlay_active)
        return false;
    if (strcmp(a->name, b->name) != 0)
        return false;

    /* Anonymous regions have no file offset to preserve. The flags field
     * reliably distinguishes anonymous from file-backed because every
     * guest_region_add call site sets LINUX_MAP_ANONYMOUS explicitly.
     */
    if ((a->flags & LINUX_MAP_ANONYMOUS) && (b->flags & LINUX_MAP_ANONYMOUS))
        return true;
    return a->offset + (a->end - a->start) == b->offset;
}

/* Merge region at index i with its right neighbor (i+1) when their layouts
 * agree. No-op if i is the last region or layouts differ.
 */
static void try_merge_right(guest_t *g, int i)
{
    if (i + 1 >= g->nregions)
        return;
    if (!regions_mergeable(&g->regions[i], &g->regions[i + 1]))
        return;

    g->regions[i].end = g->regions[i + 1].end;
    memmove(&g->regions[i + 1], &g->regions[i + 2],
            (g->nregions - i - 2) * sizeof(guest_region_t));
    g->nregions--;
}

/* Merge region at index i with its left neighbor (i-1) when their layouts
 * agree. No-op if i is out of range or the layouts differ.
 */
static void try_merge_left(guest_t *g, int i)
{
    if (i <= 0 || i >= g->nregions)
        return;
    /* nregions is bounded by GUEST_MAX_REGIONS so i is in range. */
    /* cppcheck-suppress arrayIndexOutOfBoundsCond */
    if (!regions_mergeable(&g->regions[i - 1], &g->regions[i]))
        return;

    g->regions[i - 1].end = g->regions[i].end;
    memmove(&g->regions[i], &g->regions[i + 1],
            (g->nregions - i - 1) * sizeof(guest_region_t));
    g->nregions--;
}

int guest_region_add(guest_t *g,
                     uint64_t start,
                     uint64_t end,
                     int prot,
                     int flags,
                     uint64_t offset,
                     const char *name)
{
    return guest_region_add_ex(g, start, end, prot, flags, offset, name, -1);
}

int guest_region_add_ex(guest_t *g,
                        uint64_t start,
                        uint64_t end,
                        int prot,
                        int flags,
                        uint64_t offset,
                        const char *name,
                        int backing_fd)
{
    int owned_backing_fd = -1;
    if (backing_fd >= 0) {
        owned_backing_fd = dup(backing_fd);
        if (owned_backing_fd < 0)
            return -1;
    }

    return guest_region_add_ex_owned(g, start, end, prot, flags, offset, name,
                                     owned_backing_fd);
}

int guest_region_add_ex_owned(guest_t *g,
                              uint64_t start,
                              uint64_t end,
                              int prot,
                              int flags,
                              uint64_t offset,
                              const char *name,
                              int owned_backing_fd)
{
    if (g->nregions >= GUEST_MAX_REGIONS) {
        log_error(
            "guest: region table full (%d/%d), "
            "cannot track [0x%llx-0x%llx) %s",
            g->nregions, GUEST_MAX_REGIONS, (unsigned long long) start,
            (unsigned long long) end, name ? name : "");
        if (owned_backing_fd >= 0)
            close(owned_backing_fd);
        return -1;
    }

    /* Find insertion point (keep sorted by start address) */
    int i = g->nregions;
    while (i > 0 && g->regions[i - 1].start > start) {
        g->regions[i] = g->regions[i - 1];
        i--;
    }

    guest_region_t *r = &g->regions[i];
    r->start = start;
    r->end = end;
    r->prot = prot;
    r->flags = flags;
    r->offset = offset;
    r->backing_fd = owned_backing_fd;
    r->shared = (flags & 0x01) != 0;      /* LINUX_MAP_SHARED = 0x01 */
    r->noreserve = (flags & 0x4000) != 0; /* LINUX_MAP_NORESERVE = 0x4000 */
    guest_region_clear_overlay(r);
    if (name) {
        str_copy_trunc(r->name, name, sizeof(r->name));
    } else {
        r->name[0] = '\0';
    }
    g->nregions++;

    /* Try to merge with adjacent regions to reduce table pressure.
     * Merge right first, then left (order matters: right merge does not change
     * the index of the left neighbor).
     */
    try_merge_right(g, i);
    try_merge_left(g, i);

    return 0;
}

void guest_region_remove(guest_t *g, uint64_t start, uint64_t end)
{
    int i = 0;
    while (i < g->nregions) {
        guest_region_t *r = &g->regions[i];

        /* No overlap: region is entirely before the removal range */
        if (r->end <= start) {
            i++;
            continue;
        }

        /* No overlap: region is entirely after the removal range */
        if (r->start >= end)
            break; /* sorted, so done */

        /* Full containment: remove the entire region */
        if (r->start >= start && r->end <= end) {
            if (r->backing_fd >= 0)
                close(r->backing_fd);
            memmove(&g->regions[i], &g->regions[i + 1],
                    (g->nregions - i - 1) * sizeof(guest_region_t));
            g->nregions--;
            continue; /* do not increment i */
        }

        /* Partial overlap: removal range cuts the beginning */
        if (r->start >= start && r->end > end) {
            uint64_t trimmed = end - r->start;
            r->offset += trimmed;
            r->start = end;
            guest_region_clip_overlay(r);
            i++;
            continue;
        }

        /* Partial overlap: removal range cuts the end */
        if (r->start < start && r->end > start && r->end <= end) {
            r->end = start;
            guest_region_clip_overlay(r);
            i++;
            continue;
        }

        /* Split: removal range is entirely inside the region */
        if (r->start < start && r->end > end) {
            /* Need to split into two regions: [r->start, start) and [end,
             * r->end)
             */
            if (g->nregions >= GUEST_MAX_REGIONS) {
                /* Region table is full; trim to [r->start, start) and drop
                 * the tail. The tail [end, r->end) becomes untracked in
                 * /proc/self/maps but remains mapped in page tables.
                 */
                log_error(
                    "guest: region table full, "
                    "munmap split drops tail [0x%llx-0x%llx)",
                    (unsigned long long) end, (unsigned long long) r->end);
                r->end = start;
                i++;
                continue;
            }
            /* Make room for the new region after i */
            memmove(&g->regions[i + 2], &g->regions[i + 1],
                    (g->nregions - i - 1) * sizeof(guest_region_t));

            /* Right half: [end, old_end) */
            guest_region_t *right = &g->regions[i + 1];
            *right = *r; /* Copy attributes */
            right->offset += (end - r->start);
            right->start = end;
            if (r->backing_fd >= 0) {
                /* A dup failure leaves backing_fd=-1, silently converting
                 * this half to anonymous semantics (msync and MADV_DONTNEED
                 * skip regions with backing_fd<0). Propagating the error would
                 * require making all region split callers (mprotect, munmap)
                 * fallible.
                 */
                right->backing_fd = dup(r->backing_fd);
                if (right->backing_fd < 0)
                    log_error(
                        "guest: dup() failed for region split "
                        "backing fd %d: %s",
                        r->backing_fd, strerror(errno));
            }

            /* Left half keeps the original entry and shortens its end. */
            r->end = start;
            guest_region_clip_overlay(r);
            guest_region_clip_overlay(right);

            g->nregions++;
            i += 2; /* skip both halves */
            continue;
        }

        i++;
    }
}

const guest_region_t *guest_region_find(const guest_t *g, uint64_t addr)
{
    /* Binary search in sorted array */
    int lo = 0, hi = g->nregions - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (addr < g->regions[mid].start) {
            hi = mid - 1;
        } else if (addr >= g->regions[mid].end) {
            lo = mid + 1;
        } else {
            return &g->regions[mid];
        }
    }
    return NULL;
}

void guest_region_set_prot(guest_t *g, uint64_t start, uint64_t end, int prot)
{
    /* Walk regions overlapping [start, end), split at boundaries, update prot.
     * Track the range of indices that were modified so the code can merge
     * afterward.
     */
    int first_modified = -1, last_modified = -1;

    for (int i = 0; i < g->nregions; i++) {
        guest_region_t *r = &g->regions[i];
        if (r->end <= start)
            continue;
        if (r->start >= end)
            break;

        /* If region extends before start, split at start */
        if (r->start < start) {
            if (g->nregions >= GUEST_MAX_REGIONS) {
                log_error(
                    "guest: region table full, "
                    "mprotect split skipped at 0x%llx",
                    (unsigned long long) start);
                continue;
            }
            memmove(&g->regions[i + 1], &g->regions[i],
                    (g->nregions - i) * sizeof(guest_region_t));
            g->nregions++;
            /* Left half keeps original prot and backing_fd */
            g->regions[i].end = start;
            guest_region_clip_overlay(&g->regions[i]);
            /* Right half will be processed next iteration */
            g->regions[i + 1].offset += (start - g->regions[i + 1].start);
            g->regions[i + 1].start = start;
            if (g->regions[i + 1].backing_fd >= 0) {
                g->regions[i + 1].backing_fd =
                    dup(g->regions[i + 1].backing_fd);
                if (g->regions[i + 1].backing_fd < 0)
                    log_error(
                        "guest: dup() failed for mprotect "
                        "split: %s",
                        strerror(errno));
            }
            guest_region_clip_overlay(&g->regions[i + 1]);
            i++; /* advance to the right half */
            r = &g->regions[i];
        }

        /* If region extends past end, split at end */
        if (r->end > end) {
            if (g->nregions >= GUEST_MAX_REGIONS) {
                /* Split failure applies prot to the whole region.
                 * The tail [end, r->end) gets new prot too.
                 */
                log_error(
                    "guest: region table full, "
                    "mprotect split skipped at 0x%llx "
                    "(region [0x%llx-0x%llx) gets prot %d entirely)",
                    (unsigned long long) end, (unsigned long long) r->start,
                    (unsigned long long) r->end, prot);
                r->prot = prot;
                if (first_modified < 0)
                    first_modified = i;
                last_modified = i;
                continue;
            }
            memmove(&g->regions[i + 1], &g->regions[i],
                    (g->nregions - i) * sizeof(guest_region_t));
            g->nregions++;
            /* Left half: [r->start, end) with new prot */
            g->regions[i].end = end;
            g->regions[i].prot = prot;
            guest_region_clip_overlay(&g->regions[i]);
            /* Right half: [end, old_end) keeps original prot */
            g->regions[i + 1].offset += (end - g->regions[i + 1].start);
            g->regions[i + 1].start = end;
            if (g->regions[i + 1].backing_fd >= 0) {
                g->regions[i + 1].backing_fd =
                    dup(g->regions[i + 1].backing_fd);
                if (g->regions[i + 1].backing_fd < 0)
                    log_error(
                        "guest: dup() failed for mprotect "
                        "end-split: %s",
                        strerror(errno));
            }
            guest_region_clip_overlay(&g->regions[i + 1]);
            if (first_modified < 0)
                first_modified = i;
            last_modified = i;
            break; /* done, right half is past the current range */
        }

        /* Region lies fully inside [start, end), so only prot changes. */
        r->prot = prot;
        if (first_modified < 0)
            first_modified = i;
        last_modified = i;
    }

    /* After updating prot, try to merge modified regions with neighbors.
     * Work right-to-left so index shifts do not invalidate earlier indices.
     */
    if (first_modified >= 0) {
        /* Merge last modified with its right neighbor */
        try_merge_right(g, last_modified);
        /* Merge adjacent modified regions (right to left) */
        for (int i = last_modified; i > first_modified; i--)
            try_merge_left(g, i);
        /* Merge first modified with its left neighbor */
        try_merge_left(g, first_modified);
    }
}

static void guest_region_clear(guest_t *g)
{
    for (int i = 0; i < g->nregions; i++) {
        if (g->regions[i].backing_fd >= 0) {
            close(g->regions[i].backing_fd);
            g->regions[i].backing_fd = -1;
        }
    }
    g->nregions = 0;
}

/* Page table builder. */

/* Build block descriptor for a 2MiB block at the given GPA with perms. */
static uint64_t make_block_desc(uint64_t gpa, int perms)
{
    uint64_t desc = (gpa & L2_BLOCK_ADDR_MASK) /* PA bits */
                    | PT_AF | PT_SH_ISH | PT_NS |
                    PT_ATTR1    /* Normal WB cacheable */
                    | PT_BLOCK; /* Valid block */

    /* Execute permissions: XN bits disable execution */
    if (!(perms & MEM_PERM_X)) {
        desc |= PT_UXN | PT_PXN;
    }

    /* Write permissions via AP bits:
     * AP[2:1]=01 -> RW for EL1 and EL0
     * AP[2:1]=11 -> RO for EL1 and EL0
     */
    if (perms & MEM_PERM_W) {
        desc |= PT_AP_RW_EL0;
    } else {
        desc |= PT_AP_RO;
    }

    return desc;
}

/* Convert mixed-permission and partially-covered 2MiB blocks into L3 4KiB
 * pages.
 *
 * The block-emit loop in guest_build_page_tables uses 2MiB block descriptors
 * and OR-merges permissions when multiple regions touch the same block. The
 * merge is correct only when every region in the block agrees on perms AND the
 * union of those regions covers the entire block; otherwise it leaves
 * over-permissive PTEs (e.g. .text RX + .data RW + heap RW in one 2MiB block
 * collapses to RWX) and grants access to gap pages that should fault.
 *
 * For each unique 2MiB block touched by the input regions, this pass either
 * keeps the block descriptor in place (single-perm full coverage) or splits it
 * into 512 L3 pages, invalidates the lot, and re-validates each region's pages
 * with the correct perms. Pages with no region coverage stay invalid, matching
 * Linux semantics for inter-segment gaps in small static binaries.
 */
static bool finalize_block_perms(guest_t *g, const mem_region_t *regions, int n)
{
    /* Walk every 2MiB block touched by any region. Blocks shared by multiple
     * regions are processed multiple times; the underlying split / invalidate /
     * re-validate sequence is idempotent (guest_split_block is a no-op once
     * the L2 entry is a table descriptor; guest_invalidate_ptes + per-region
     * guest_update_perms produce the same final L3 state on every pass), so
     * dedup is an optimization the heap-region scale (~127 blocks for the
     * default brk window) does not justify against a fixed-size visited set.
     */
    for (int r = 0; r < n; r++) {
        uint64_t r_block_lo = ALIGN_2MIB_DOWN(regions[r].gpa_start);
        uint64_t r_block_hi = ALIGN_2MIB_UP(regions[r].gpa_end);

        for (uint64_t b = r_block_lo; b < r_block_hi; b += BLOCK_2MIB) {
            /* Walk all regions touching this block. Track perm uniformity and
             * collect them into idx[] sorted by start so coverage can be
             * checked with a single sweep.
             */
            int idx[GUEST_MAX_REGIONS];
            int nidx = 0;
            int first_perm = -1;
            bool same_perm = true;

            for (int s = 0; s < n; s++) {
                if (regions[s].gpa_end <= b ||
                    regions[s].gpa_start >= b + BLOCK_2MIB)
                    continue;
                if (first_perm < 0)
                    first_perm = regions[s].perms;
                else if (regions[s].perms != first_perm)
                    same_perm = false;

                int pos = nidx;
                while (pos > 0 &&
                       regions[idx[pos - 1]].gpa_start > regions[s].gpa_start) {
                    idx[pos] = idx[pos - 1];
                    pos--;
                }
                idx[pos] = s;
                nidx++;
            }

            /* Coverage sweep: regions are sorted by start, so the union covers
             * the block iff each region begins at or before the running
             * high-water mark.
             */
            uint64_t covered_until = b;
            bool full_coverage = true;
            for (int i = 0; i < nidx; i++) {
                uint64_t cs = regions[idx[i]].gpa_start;
                uint64_t ce = regions[idx[i]].gpa_end;
                if (cs > covered_until) {
                    full_coverage = false;
                    break;
                }
                if (ce > covered_until)
                    covered_until = ce;
            }
            if (covered_until < b + BLOCK_2MIB)
                full_coverage = false;

            /* Single perm covering the whole block: the existing 2MiB
             * descriptor is already correct.
             */
            if (same_perm && full_coverage)
                continue;

            /* Split into L3 pages, invalidate the lot, then rebuild the block
             * from per-page unions. This preserves the required permission
             * union when adjacent ELF segments share a 4KiB page after
             * page-granularity rounding.
             */
            if (guest_split_block(g, b) < 0)
                return false;
            if (guest_invalidate_ptes(g, b, b + BLOCK_2MIB) < 0)
                return false;

            int page_perms[BLOCK_2MIB / PAGE_SIZE] = {0};
            for (int i = 0; i < nidx; i++) {
                uint64_t s_start = regions[idx[i]].gpa_start;
                uint64_t s_end = regions[idx[i]].gpa_end;
                uint64_t apply_start = (s_start > b) ? s_start : b;
                uint64_t apply_end =
                    (s_end < b + BLOCK_2MIB) ? s_end : b + BLOCK_2MIB;
                /* Page-align to 4KiB so partially covered pages are recreated
                 * with the union of all overlapping segment permissions.
                 */
                apply_start = ALIGN_DOWN(apply_start, PAGE_SIZE);
                apply_end = PAGE_ALIGN_UP(apply_end);
                if (apply_end > b + BLOCK_2MIB)
                    apply_end = b + BLOCK_2MIB;

                for (uint64_t pa = apply_start; pa < apply_end;
                     pa += PAGE_SIZE) {
                    unsigned page_idx = (unsigned) ((pa - b) / PAGE_SIZE);
                    page_perms[page_idx] |= regions[idx[i]].perms;
                }
            }

            for (int i = 0; i < (int) ARRAY_SIZE(page_perms);) {
                int perms = page_perms[i];
                int run_start = i;

                while (i < (int) ARRAY_SIZE(page_perms) &&
                       page_perms[i] == perms)
                    i++;
                if (!perms)
                    continue;

                uint64_t run_gpa_start = b + (uint64_t) run_start * PAGE_SIZE;
                uint64_t run_gpa_end = b + (uint64_t) i * PAGE_SIZE;
                if (guest_update_perms(g, run_gpa_start, run_gpa_end, perms) <
                    0)
                    return false;
            }
        }
    }

    return true;
}

uint64_t guest_build_page_tables(guest_t *g, const mem_region_t *regions, int n)
{
    uint64_t base = g->ipa_base;

    /* Allocate L0 table */
    uint64_t l0_gpa = pt_alloc_page(g);
    if (!l0_gpa)
        return 0;

    uint64_t *l0 = pt_at(g, l0_gpa);

    /* For each region, determine which 2MiB blocks need mapping.
     * Identity-mapped: VA == GPA, so L0/L1/L2 indices and the block
     * descriptor output address are both derived from gpa_start + ipa_base.
     */
    for (int r = 0; r < n; r++) {
        uint64_t gpa_start = ALIGN_2MIB_DOWN(regions[r].gpa_start);
        uint64_t gpa_end = ALIGN_2MIB_UP(regions[r].gpa_end);
        int perms = regions[r].perms;

        for (uint64_t gpa = gpa_start; gpa < gpa_end; gpa += BLOCK_2MIB) {
            uint64_t lookup_addr = base + gpa;

            /* L0 index: which 512GiB slot this VA falls in */
            unsigned l0_idx = (unsigned) (lookup_addr / (512ULL * BLOCK_1GIB));
            if (l0_idx >= 512) {
                log_error("guest: VA 0x%llx out of L0 range",
                          (unsigned long long) lookup_addr);
                continue;
            }

            /* Allocate L1 table on first access to each L0 slot */
            if (!(l0[l0_idx] & PT_VALID)) {
                uint64_t l1_gpa = pt_alloc_page(g);
                if (!l1_gpa)
                    return 0;
                l0[l0_idx] = (base + l1_gpa) | PT_VALID | PT_TABLE;
            }
            uint64_t l1_ipa = l0[l0_idx] & 0xFFFFFFFFF000ULL;
            uint64_t *l1 = pt_at(g, l1_ipa - base);

            /* L1 index within the 512GiB L0 entry (from VA) */
            unsigned l1_idx =
                (unsigned) ((lookup_addr % (512ULL * BLOCK_1GIB)) / BLOCK_1GIB);
            if (l1_idx >= 512) {
                log_error("guest: VA 0x%llx out of L1 range",
                          (unsigned long long) lookup_addr);
                continue;
            }

            /* Ensure L1 entry points to an L2 table */
            if (!(l1[l1_idx] & PT_VALID)) {
                uint64_t l2_gpa = pt_alloc_page(g);
                if (!l2_gpa)
                    return 0;
                l1[l1_idx] = (base + l2_gpa) | PT_VALID | PT_TABLE;
            }

            /* L2 table for this 1GiB region (stored in host at gpa offset) */
            uint64_t l2_ipa = l1[l1_idx] & 0xFFFFFFFFF000ULL;
            uint64_t l2_gpa_off = l2_ipa - base;
            uint64_t *l2 = pt_at(g, l2_gpa_off);

            /* L2 index: which 2MiB block within the 1GiB region (from VA) */
            unsigned l2_idx =
                (unsigned) ((lookup_addr % BLOCK_1GIB) / BLOCK_2MIB);

            /* If block already mapped, merge permissions (most permissive).
             * Use a local variable for the merged perms. Do NOT modify the
             * outer perms variable, which would leak accumulated permissions
             * to subsequent 2MiB blocks in the same region.
             */
            int block_perms = perms;
            if (l2[l2_idx] & PT_BLOCK) {
                int old_perms = 0;
                if (!(l2[l2_idx] & PT_UXN))
                    old_perms |= MEM_PERM_X;
                if ((l2[l2_idx] & (3ULL << 6)) == PT_AP_RW_EL0)
                    old_perms |= MEM_PERM_W;
                old_perms |= MEM_PERM_R;
                block_perms |= old_perms;
            }

            /* Block descriptor: output IPA (where data physically lives) */
            l2[l2_idx] = make_block_desc(lookup_addr, block_perms);
        }
    }

    /* Store TTBR0 for later use by guest_extend_page_tables */
    uint64_t ttbr0 = base + l0_gpa;
    g->ttbr0 = ttbr0;

    /* Convert blocks shared by regions with mixed perms or partial coverage
     * into L3 4KiB pages so each segment's permissions are honored exactly.
     */
    if (!finalize_block_perms(g, regions, n))
        return 0;

    guest_pt_gen_bump(g);
    return ttbr0;
}

/* Extend page tables to cover [start, end) with 2MiB block descriptors.
 * Walks the existing L0->L1 structure (from g->ttbr0) and allocates new
 * L2 tables as needed. This is safe to call while the vCPU is paused
 * (during HVC #5 handling). Sets g->need_tlbi so the shim flushes the
 * TLB before returning to EL0.
 */
int guest_extend_page_tables(guest_t *g,
                             uint64_t start,
                             uint64_t end,
                             int perms)
{
    uint64_t base = g->ipa_base;

    /* Navigate to L0 table */
    uint64_t l0_gpa_off = g->ttbr0 - base;
    uint64_t *l0 = pt_at(g, l0_gpa_off);

    /* Walk 2MiB blocks in [start, end) */
    uint64_t addr_start = ALIGN_2MIB_DOWN(start), addr_end = ALIGN_2MIB_UP(end);

    for (uint64_t addr = addr_start; addr < addr_end; addr += BLOCK_2MIB) {
        uint64_t ipa = base + addr;

        /* L0 index: which 512GiB slot (>512GiB addresses need L0[1]+) */
        unsigned l0_idx = (unsigned) (ipa / (512ULL * BLOCK_1GIB));
        if (l0_idx >= 512) {
            log_error("guest: IPA 0x%llx out of L0 range in extend",
                      (unsigned long long) ipa);
            return -1;
        }

        /* Allocate L1 table on first access to each L0 slot */
        if (!(l0[l0_idx] & PT_VALID)) {
            uint64_t l1_gpa = pt_alloc_page(g);
            if (!l1_gpa)
                return -1;
            l0[l0_idx] = (base + l1_gpa) | PT_VALID | PT_TABLE;
        }

        uint64_t l1_ipa = l0[l0_idx] & 0xFFFFFFFFF000ULL;
        uint64_t *l1 = pt_at(g, l1_ipa - base);

        unsigned l1_idx =
            (unsigned) ((ipa % (512ULL * BLOCK_1GIB)) / BLOCK_1GIB);
        if (l1_idx >= 512) {
            log_error("guest: IPA 0x%llx out of L1 range in extend",
                      (unsigned long long) ipa);
            return -1;
        }

        /* Ensure L1 entry points to an L2 table */
        if (!(l1[l1_idx] & PT_VALID)) {
            uint64_t l2_gpa = pt_alloc_page(g);
            if (!l2_gpa)
                return -1;
            l1[l1_idx] = (base + l2_gpa) | PT_VALID | PT_TABLE;
        }

        /* Navigate to L2 table */
        uint64_t l2_ipa = l1[l1_idx] & 0xFFFFFFFFF000ULL;
        uint64_t *l2 = pt_at(g, l2_ipa - base);

        unsigned l2_idx = (unsigned) ((ipa % BLOCK_1GIB) / BLOCK_2MIB);

        /* Only map if not already mapped */
        if (!(l2[l2_idx] & PT_BLOCK)) {
            l2[l2_idx] = make_block_desc(ipa, perms);
        }
    }

    g->need_tlbi = true;
    guest_pt_gen_bump(g);
    return 0;
}

/* L3 page table splitting. */

/* L3 page descriptor: bits[1:0]=11 = valid page at level 3.
 * This is distinct from L2 block descriptors (bits[1:0]=01).
 */
#define PT_L3_PAGE (3ULL)

/* Build a 4KiB L3 page descriptor with the given permissions.
 * Layout matches block descriptors (AF, SH, NS, MAIR, AP, XN) except
 * bits[1:0]=11 instead of 01.
 */
static uint64_t make_page_desc(uint64_t pa, int perms)
{
    uint64_t desc = (pa & 0xFFFFFFFFF000ULL) /* PA bits [47:12] */
                    | PT_AF | PT_SH_ISH | PT_NS | PT_ATTR1 | PT_L3_PAGE;

    if (!(perms & MEM_PERM_X))
        desc |= PT_UXN | PT_PXN;

    if (perms & MEM_PERM_W)
        desc |= PT_AP_RW_EL0;
    else
        desc |= PT_AP_RO;

    return desc;
}

/* Extract MEM_PERM_* flags from a page table descriptor (block or page). */
static int desc_to_perms(uint64_t desc)
{
    int perms = MEM_PERM_R;
    if (!(desc & PT_UXN))
        perms |= MEM_PERM_X;
    if ((desc & (3ULL << 6)) == PT_AP_RW_EL0)
        perms |= MEM_PERM_W;
    return perms;
}

/* Navigate L0->L1->L2 to find the L2 entry for a given GPA offset.
 * Returns a pointer to the L2 entry, or NULL if not mapped.
 */
static uint64_t *find_l2_entry(guest_t *g, uint64_t gpa_offset)
{
    uint64_t base = g->ipa_base, ipa = base + gpa_offset;

    uint64_t l0_gpa_off = g->ttbr0 - base;
    uint64_t *l0 = pt_at(g, l0_gpa_off);

    /* L0 index from actual IPA (not base), correct for >512GiB */
    unsigned l0_idx = (unsigned) (ipa / (512ULL * BLOCK_1GIB));
    if (l0_idx >= 512 || !(l0[l0_idx] & PT_VALID))
        return NULL;

    uint64_t l1_ipa = l0[l0_idx] & 0xFFFFFFFFF000ULL;
    uint64_t *l1 = pt_at(g, l1_ipa - base);

    unsigned l1_idx = (unsigned) ((ipa % (512ULL * BLOCK_1GIB)) / BLOCK_1GIB);
    if (l1_idx >= 512 || !(l1[l1_idx] & PT_VALID))
        return NULL;

    uint64_t l2_ipa = l1[l1_idx] & 0xFFFFFFFFF000ULL;
    uint64_t *l2 = pt_at(g, l2_ipa - base);

    unsigned l2_idx = (unsigned) ((ipa % BLOCK_1GIB) / BLOCK_2MIB);
    return &l2[l2_idx];
}

/* Split a 2MiB L2 block descriptor into 512 × 4KiB L3 page descriptors.
 * The caller provides the L2 entry via find_l2_entry.
 * Extracts the output IPA from the existing descriptor.
 */
static int split_l2_block(guest_t *g, uint64_t *l2_entry)
{
    if (!l2_entry)
        return -1;

    /* Already a table descriptor (previously split); nothing to do */
    if ((*l2_entry & 3) == 3)
        return 0;

    /* Must be a valid block descriptor: bit[0]=1, bit[1]=0 */
    if (!(*l2_entry & PT_BLOCK))
        return -1;

    int old_perms = desc_to_perms(*l2_entry);

    uint64_t l3_gpa = pt_alloc_page(g);
    if (!l3_gpa)
        return -1;
    uint64_t *l3 = pt_at(g, l3_gpa);

    /* Fill 512 L3 entries with 4KiB page descriptors inheriting the
     * block's permissions.  Extract the output IPA from bits [47:21]
     * of the existing descriptor (not from the caller's address).
     */
    uint64_t block_ipa = *l2_entry & L2_BLOCK_ADDR_MASK;
    for (int i = 0; i < 512; i++)
        l3[i] = make_page_desc(block_ipa + (uint64_t) i * PAGE_SIZE, old_perms);

    *l2_entry = (g->ipa_base + l3_gpa) | PT_VALID | PT_TABLE;
    g->need_tlbi = true;
    return 0;
}

int guest_split_block(guest_t *g, uint64_t block_gpa)
{
    uint64_t block_start = ALIGN_2MIB_DOWN(block_gpa);
    uint64_t *l2_entry = find_l2_entry(g, block_start);
    return split_l2_block(g, l2_entry);
}

int guest_invalidate_ptes(guest_t *g, uint64_t start, uint64_t end)
{
    uint64_t base = g->ipa_base;

    /* Page-align the range */
    start = start & ~(PAGE_SIZE - 1);
    end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uint64_t addr = start; addr < end;) {
        uint64_t *l2_entry = find_l2_entry(g, addr);
        if (!l2_entry) {
            /* No L2 entry (already unmapped); skip this 2MiB block */
            addr = ALIGN_2MIB_UP(addr + 1);
            continue;
        }

        uint64_t block_start = ALIGN_2MIB_DOWN(addr);
        uint64_t block_end = block_start + BLOCK_2MIB;

        /* Not mapped at all: skip */
        if (!(*l2_entry & 1)) {
            addr = block_end;
            continue;
        }

        /* Check if this is a 2MiB block or already an L3 table */
        if ((*l2_entry & 3) == 1) {
            /* 2MiB block descriptor */
            if (start <= block_start && end >= block_end) {
                /* Invalidating the entire 2MiB block: clear the L2 entry */
                *l2_entry = 0;
                g->need_tlbi = true;
                addr = block_end;
                continue;
            }

            /* Partial invalidation within a 2MiB block: split first,
             * then invalidate individual L3 pages below.
             */
            if (guest_split_block(g, block_start) < 0)
                return -1;
        }

        /* L3 table: invalidate individual 4KiB page descriptors */
        uint64_t l3_ipa = *l2_entry & 0xFFFFFFFFF000ULL;
        uint64_t *l3 = pt_at(g, l3_ipa - base);

        uint64_t page_start = (addr > block_start) ? addr : block_start;
        uint64_t page_end = (end < block_end) ? end : block_end;

        for (uint64_t pa = page_start; pa < page_end; pa += PAGE_SIZE) {
            unsigned l3_idx =
                (unsigned) (((base + pa) % BLOCK_2MIB) / PAGE_SIZE);
            l3[l3_idx] = 0; /* Invalid descriptor */
        }

        g->need_tlbi = true;
        addr = page_end;
    }

    guest_pt_gen_bump(g);
    return 0;
}

int guest_update_perms(guest_t *g, uint64_t start, uint64_t end, int perms)
{
    uint64_t base = g->ipa_base;

    /* Page-align the range */
    start = start & ~(PAGE_SIZE - 1);
    end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uint64_t addr = start; addr < end;) {
        uint64_t *l2_entry = find_l2_entry(g, addr);
        if (!l2_entry) {
            /* Skip unmapped 2MiB blocks */
            addr = ALIGN_2MIB_UP(addr + 1);
            continue;
        }

        uint64_t block_start = ALIGN_2MIB_DOWN(addr);
        uint64_t block_end = block_start + BLOCK_2MIB;

        /* Not mapped at all: skip */
        if (!(*l2_entry & 1)) {
            addr = block_end;
            continue;
        }

        /* Check if this is a 2MiB block or already an L3 table */
        if ((*l2_entry & 3) == 1) {
            /* 2MiB block descriptor */
            int old_perms = desc_to_perms(*l2_entry);

            /* If the whole 2MiB block changes permissions, rewrite the block
             * descriptor without splitting. Extract the output IPA from the
             * existing descriptor, correct for both identity and non-identity
             * mapped regions.
             */
            if (start <= block_start && end >= block_end) {
                if (old_perms != perms) {
                    uint64_t ipa = *l2_entry & L2_BLOCK_ADDR_MASK;
                    *l2_entry = make_block_desc(ipa, perms);
                    g->need_tlbi = true;
                }
                addr = block_end;
                continue;
            }

            /* Partial update: split the 2MiB block into L3 pages first, then
             * fall through to update individual pages below.
             */
            if (old_perms != perms) {
                if (guest_split_block(g, block_start) < 0)
                    return -1;
            } else {
                /* Same permissions; no change needed */
                addr = block_end;
                continue;
            }
        }

        /* L3 table: update individual 4KiB page descriptors */
        uint64_t l3_ipa = *l2_entry & 0xFFFFFFFFF000ULL;
        uint64_t *l3 = pt_at(g, l3_ipa - base);

        /* Update pages within this 2MiB block that fall in [start, end) */
        uint64_t page_start = (addr > block_start) ? addr : block_start;
        uint64_t page_end = (end < block_end) ? end : block_end;

        for (uint64_t pa = page_start; pa < page_end; pa += PAGE_SIZE) {
            unsigned l3_idx =
                (unsigned) (((base + pa) % BLOCK_2MIB) / PAGE_SIZE);
            /* Extract the existing output IPA from the L3 entry. For
             * non-identity mapped regions, pa is a VA not a GPA, so the builder
             * must use the IPA already stored in the descriptor (set by
             * guest_split_block).
             *
             * For invalidated entries (set to 0 by guest_invalidate_ptes), the
             * stored IPA is 0, which is wrong. Fall back to computing the
             * identity-mapped IPA (base + pa). This is correct for TTBR0
             * user-space regions where VA == IPA == GPA.
             */
            uint64_t page_ipa;
            if (l3[l3_idx] & PT_VALID)
                page_ipa = l3[l3_idx] & 0xFFFFFFFFF000ULL;
            else
                page_ipa = base + (pa & ~(PAGE_SIZE - 1));
            l3[l3_idx] = make_page_desc(page_ipa, perms);
        }

        g->need_tlbi = true;
        addr = page_end;
    }

    guest_pt_gen_bump(g);
    return 0;
}

/* Lazy page materialization for MAP_NORESERVE. */

int guest_materialize_lazy(guest_t *g, uint64_t fault_offset)
{
    /* Find the noreserve region containing this offset */
    const guest_region_t *region = NULL;
    for (int i = 0; i < g->nregions; i++) {
        if (g->regions[i].start <= fault_offset &&
            g->regions[i].end > fault_offset && g->regions[i].noreserve) {
            region = &g->regions[i];
            break;
        }
    }

    if (!region)
        return -1; /* Not a noreserve region */

    /* Materialize one 2MiB block containing the fault address. This is
     * the smallest granule that guest_extend_page_tables works with.
     * For the common case (sparse heap touch), materializing one block
     * at a time is the right trade-off: it avoids over-committing the
     * large reservation while keeping the fault rate manageable.
     */
    uint64_t block_start = fault_offset & ~(BLOCK_2MIB - 1);
    uint64_t block_end = block_start + BLOCK_2MIB;

    /* Clamp to guest size */
    if (block_end > g->guest_size)
        block_end = g->guest_size;

    uint64_t materialize_start =
        (block_start > region->start) ? block_start : region->start;
    uint64_t materialize_end =
        (block_end < region->end) ? block_end : region->end;
    bool partial_block = region->start > block_start || region->end < block_end;
    uint64_t *l2_entry = find_l2_entry(g, block_start);
    int had_mapping = l2_entry && (*l2_entry & PT_VALID);

    /* Convert prot flags to page table perms */
    int perms = 0;
    if (region->prot & LINUX_PROT_READ)
        perms |= MEM_PERM_R;
    if (region->prot & LINUX_PROT_WRITE)
        perms |= MEM_PERM_W;
    if (region->prot & LINUX_PROT_EXEC)
        perms |= MEM_PERM_X;
    if (perms == 0)
        perms = MEM_PERM_R; /* At minimum readable */

    /* Create page table entries. guest_extend_page_tables creates L2 block
     * descriptors but skips existing table descriptors (L2->L3 splits).
     * guest_update_perms handles the L3 case: if guest_invalidate_ptes
     * previously split the block and invalidated the L3 entries,
     * update_perms recreates them with correct perms.
     */
    if (guest_extend_page_tables(g, block_start, block_end, perms) < 0)
        return -1;

    if (partial_block) {
        if (guest_split_block(g, block_start) < 0)
            return -1;

        /* If this block had no page-table entry before the lazy fault,
         * guest_extend_page_tables() necessarily created a full 2MiB block.
         * Split it and remove pages outside this noreserve region so holes and
         * guards in the same 2MiB block remain faults. Existing split blocks
         * already encode neighboring mappings, so leave them intact.
         */
        if (!had_mapping) {
            if (block_start < materialize_start &&
                guest_invalidate_ptes(g, block_start, materialize_start) < 0)
                return -1;
            if (materialize_end < block_end &&
                guest_invalidate_ptes(g, materialize_end, block_end) < 0)
                return -1;
        }
    }

    guest_update_perms(g, materialize_start, materialize_end, perms);

    /* Zero the materialized memory. Only zero within the region boundaries to
     * avoid clobbering adjacent data.
     */
    if (materialize_end > materialize_start)
        memset((uint8_t *) g->host_base + materialize_start, 0,
               materialize_end - materialize_start);

    g->need_tlbi = true;
    return 0;
}
