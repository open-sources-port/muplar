/* linker.c — muplar-linker64: minimal AArch64 dynamic linker
 *
 * Self-relocation is handled entirely in entry.S before this file runs.
 * By the time linker_main() is called, all R_AARCH64_RELATIVE slots have
 * been patched and it is safe to use globals and string literals.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "elf.h"
#include "syscall.h"

/* Guard: some toolchain elf.h versions omit packed-reloc tags */
#ifndef DT_RELR
#define DT_RELR    0x6fffe000
#define DT_RELRSZ  0x6fffe001
#define DT_RELRENT 0x6fffe003
#endif

/* -------------------------------------------------------------------------
 * Configuration
 * ---------------------------------------------------------------------- */

#define MAX_OBJECTS 32
#define MAX_NEEDED  16

/* Page size */
#define PAGE_SIZE      4096ULL
#define PAGE_MASK      (~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(x)   (((x) + PAGE_SIZE - 1) & PAGE_MASK)
#define PAGE_ALIGN_DOWN(x) ((x) & PAGE_MASK)

/* -------------------------------------------------------------------------
 * Freestanding string/memory utilities
 * ---------------------------------------------------------------------- */

static size_t lnk_strlen(const char *s) {
    size_t n = 0; while (s[n]) n++; return n;
}
static int lnk_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static char *lnk_strcpy(char *d, const char *s) {
    char *r = d; while ((*d++ = *s++)) { /* copy */ } return r;
}
static char *lnk_strcat(char *d, const char *s) {
    char *r = d; while (*d) d++; while ((*d++ = *s++)) { /* copy */ } return r;
}
static void *lnk_memset(void *s, int c, size_t n) {
    uint8_t *p = s; while (n--) *p++ = (uint8_t)c; return s;
}

static void lnk_puts(const char *s) {
    sys_write(2, s, lnk_strlen(s));
}

#define FATAL(msg) do { lnk_puts("[linker64] FATAL: " msg "\n"); sys_exit(127); } while(0)

/* -------------------------------------------------------------------------
 * Global state (safe to use — entry.S applied relocations before we run)
 * ---------------------------------------------------------------------- */

typedef struct {
    uint64_t    load_base;
    const char *strtab;
    size_t      strsz;
    Elf64_Sym  *symtab;
    Elf64_Rela *rela;     size_t rela_count;
    Elf64_Rela *jmprel;   size_t jmprel_count;
    uint64_t   *relr;     size_t relr_count;    /* packed relative relocs */
    uint32_t    needed_off[MAX_NEEDED];
    int         needed_count;
    char        path[256];
    char        soname[128];
} Object;

static Object g_objects[MAX_OBJECTS];
static int    g_nobjects = 0;

/* -------------------------------------------------------------------------
 * Auxv helper
 * ---------------------------------------------------------------------- */

static uint64_t auxv_get(Elf64_auxv_t *auxv, uint64_t type) {
    for (; auxv->a_type != AT_NULL; auxv++)
        if (auxv->a_type == type) return auxv->a_val;
    return 0;
}

/* -------------------------------------------------------------------------
 * ELF loading
 * ---------------------------------------------------------------------- */

static int read_at(int fd, uint64_t offset, void *buf, size_t len) {
    if (sys_lseek(fd, (long)offset, 0) < 0) return -1;
    size_t got = 0;
    while (got < len) {
        long r = sys_read(fd, (char *)buf + got, len - got);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

static int pf_to_prot(uint32_t flags) {
    int p = 0;
    if (flags & PF_R) p |= PROT_READ;
    if (flags & PF_W) p |= PROT_WRITE;
    if (flags & PF_X) p |= PROT_EXEC;
    return p;
}

static void parse_dynamic(Object *obj, uint64_t dyn_va) {
    Elf64_Dyn *dyn = (Elf64_Dyn *)dyn_va;
    uint64_t strtab_va = 0;
    for (; dyn->d_tag != DT_NULL; dyn++) {
        switch (dyn->d_tag) {
        case DT_STRTAB:   strtab_va       = dyn->d_val + obj->load_base; break;
        case DT_STRSZ:    obj->strsz       = (size_t)dyn->d_val; break;
        case DT_SYMTAB:   obj->symtab      = (Elf64_Sym *)(dyn->d_val + obj->load_base); break;
        case DT_RELA:     obj->rela         = (Elf64_Rela *)(dyn->d_val + obj->load_base); break;
        case DT_RELASZ:   obj->rela_count   = dyn->d_val / sizeof(Elf64_Rela); break;
        case DT_JMPREL:   obj->jmprel       = (Elf64_Rela *)(dyn->d_val + obj->load_base); break;
        case DT_PLTRELSZ: obj->jmprel_count = dyn->d_val / sizeof(Elf64_Rela); break;
        case DT_RELR:     obj->relr         = (uint64_t *)(dyn->d_val + obj->load_base); break;
        case DT_RELRSZ:   obj->relr_count   = dyn->d_val / sizeof(uint64_t); break;
        case DT_NEEDED:
            if (obj->needed_count < MAX_NEEDED)
                obj->needed_off[obj->needed_count++] = (uint32_t)dyn->d_val;
            break;
        default: break;
        }
    }
    if (strtab_va) obj->strtab = (const char *)strtab_va;
}

static void load_so(Object *obj, int fd) {
    Elf64_Ehdr ehdr;
    if (read_at(fd, 0, &ehdr, sizeof(ehdr)) < 0) FATAL("read ELF header");

    Elf64_Phdr phdrs[16];
    if (ehdr.e_phnum > 16) FATAL("too many phdrs");
    if (read_at(fd, ehdr.e_phoff, phdrs, ehdr.e_phnum * sizeof(Elf64_Phdr)) < 0)
        FATAL("read phdrs");

    uint64_t load_min = UINT64_MAX, load_max = 0;
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;
        if (phdrs[i].p_vaddr < load_min) load_min = phdrs[i].p_vaddr;
        uint64_t end = phdrs[i].p_vaddr + phdrs[i].p_memsz;
        if (end > load_max) load_max = end;
    }
    load_min = PAGE_ALIGN_DOWN(load_min);
    load_max = PAGE_ALIGN_UP(load_max);

    void *base = sys_mmap(NULL, load_max - load_min, PROT_NONE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)base < 0) FATAL("mmap reserve");

    obj->load_base = (uint64_t)base - load_min;

    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;

        uint64_t seg_start = PAGE_ALIGN_DOWN(ph->p_vaddr) + obj->load_base;
        uint64_t seg_end   = PAGE_ALIGN_UP(ph->p_vaddr + ph->p_memsz) + obj->load_base;
        long     file_off  = (long)PAGE_ALIGN_DOWN(ph->p_offset);
        int      final_prot = pf_to_prot(ph->p_flags);

        /* Phase 1: always map RW so we can write file data and zero BSS.
         * Never combine PROT_WRITE|PROT_EXEC — Apple HVF (W^X) rejects it
         * and elfuse's shim raises an unrecoverable fault. */
        void *r = sys_mmap((void *)seg_start, seg_end - seg_start,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_FIXED, fd, file_off);
        if ((long)r < 0) FATAL("mmap segment");

        /* Zero BSS region (bytes beyond p_filesz within the segment) */
        if (ph->p_memsz > ph->p_filesz) {
            uint8_t *bss = (uint8_t *)(ph->p_vaddr + ph->p_filesz + obj->load_base);
            lnk_memset(bss, 0, ph->p_memsz - ph->p_filesz);
        }

        /* Phase 2: apply final permissions now that writes are done */
        if (final_prot != (PROT_READ | PROT_WRITE))
            sys_mprotect((void *)seg_start, seg_end - seg_start, final_prot);
    }

    /* Find PT_DYNAMIC */
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            parse_dynamic(obj, phdrs[i].p_vaddr + obj->load_base);
            break;
        }
    }
}

/* -------------------------------------------------------------------------
 * Symbol lookup
 * ---------------------------------------------------------------------- */

static uint64_t lookup_symbol(const char *name) {
    for (int i = 0; i < g_nobjects; i++) {
        Object *obj = &g_objects[i];
        if (!obj->symtab || !obj->strtab) continue;
        for (int j = 1; j < 4096; j++) {
            Elf64_Sym *sym = &obj->symtab[j];
            if (sym->st_shndx == SHN_UNDEF) continue;
            if (sym->st_name >= obj->strsz) break;
            if (lnk_strcmp(obj->strtab + sym->st_name, name) == 0)
                return sym->st_value + obj->load_base;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Relocation
 * ---------------------------------------------------------------------- */

static void apply_rela(Object *obj, Elf64_Rela *rela, size_t count) {
    for (size_t i = 0; i < count; i++) {
        uint32_t sym_idx  = ELF64_R_SYM(rela[i].r_info);
        uint32_t rel_type = ELF64_R_TYPE(rela[i].r_info);
        uint64_t *slot    = (uint64_t *)(rela[i].r_offset + obj->load_base);
        int64_t   addend  = rela[i].r_addend;

        switch (rel_type) {
        case R_AARCH64_RELATIVE:
            *slot = obj->load_base + (uint64_t)addend;
            break;
        case R_AARCH64_GLOB_DAT:
        case R_AARCH64_JUMP_SLOT:
        case R_AARCH64_ABS64: {
            uint64_t sym_val = 0;
            if (sym_idx && obj->symtab && obj->strtab) {
                Elf64_Sym *sym = &obj->symtab[sym_idx];
                if (sym->st_shndx != SHN_UNDEF) {
                    sym_val = sym->st_value + obj->load_base;
                } else {
                    const char *name = obj->strtab + sym->st_name;
                    sym_val = lookup_symbol(name);
                    if (!sym_val) {
                        /* Treat as weak-undefined: leave slot as 0.
                         * Symbols from phantom system libs (libc, libm, libdl)
                         * are never actually called at runtime — the guest exe
                         * is statically linked and provides its own libc. */
                        break;
                    }
                }
            }
            *slot = sym_val + (uint64_t)addend;
            break;
        }
        default: break;
        }
    }
}

/* Apply RELR (packed relative) relocations.
 *
 * RELR encoding: each word is either an address (odd bit clear) or a bitmap
 * (odd bit set). An address word sets the current slot to (load_base + 0)
 * [R_AARCH64_RELATIVE with addend = *slot at link time] and advances by 8.
 * A bitmap word patches up to 63 additional slots relative to the previous
 * address, one per bit starting at bit 1.
 *
 * Reference: https://maskray.me/blog/2021-10-31-relative-relocations-and-relr
 */
static void apply_relr(Object *obj) {
    if (!obj->relr || !obj->relr_count) return;

    uint64_t base = obj->load_base;
    uint64_t *slot = NULL;   /* current output slot (runtime VA) */

    for (size_t i = 0; i < obj->relr_count; i++) {
        uint64_t entry = obj->relr[i];

        if ((entry & 1) == 0) {
            /* Address entry: points to the next slot to patch */
            slot = (uint64_t *)(entry + base);
            /* Apply relocation: *slot += base (RELATIVE with addend = *slot) */
            *slot += base;
            slot++;   /* advance past the patched slot */
        } else {
            /* Bitmap entry: bits 1..63 indicate which of the next 63 slots
             * (relative to the slot BEFORE the current slot pointer) need patching. */
            uint64_t *s = slot;
            uint64_t bitmap = entry >> 1;   /* drop the marker bit */
            while (bitmap) {
                if (bitmap & 1)
                    *s += base;
                s++;
                bitmap >>= 1;
            }
            /* Advance slot by 63 positions (the full bitmap window) */
            slot += 63;
        }
    }
}

static void relocate_object(Object *obj) {
    apply_relr(obj);
    if (obj->rela   && obj->rela_count)   apply_rela(obj, obj->rela,   obj->rela_count);
    if (obj->jmprel && obj->jmprel_count) apply_rela(obj, obj->jmprel, obj->jmprel_count);
}

/* -------------------------------------------------------------------------
 * Library search — using stack-local strings to avoid global pointer issues
 * during early startup (belt-and-suspenders; relocation is done before we
 * get here, but keeping paths on the stack is cleaner anyway).
 * ---------------------------------------------------------------------- */

static void load_needed_so(const char *soname);  /* forward decl */

static bool try_dir(const char *dir, const char *soname,
                    char *path_out, int *fd_out)
{
    char path[256];
    lnk_strcpy(path, dir);
    lnk_strcat(path, "/");
    lnk_strcat(path, soname);
    long fd = sys_openat(AT_FDCWD, path, O_RDONLY);
    if (fd >= 0) { lnk_strcpy(path_out, path); *fd_out = (int)fd; return true; }
    return false;
}

/* Returns true if soname is an Android system library that must not be
 * loaded by our minimal linker (complex bootstrap, TLS init, etc.).
 *
 * IMPORTANT: no static pointer arrays here — those require R_AARCH64_RELATIVE
 * relocations that might not be applied yet when this is first called.
 * Use only direct string literal comparisons via lnk_strcmp(). */
static bool is_system_lib(const char *soname) {
    return (
        lnk_strcmp(soname, "libc.so")          == 0 ||
        lnk_strcmp(soname, "libm.so")          == 0 ||
        lnk_strcmp(soname, "libdl.so")         == 0 ||
        lnk_strcmp(soname, "libdl_android.so") == 0 ||
        lnk_strcmp(soname, "libstdc++.so")     == 0 ||
        lnk_strcmp(soname, "libc++.so")        == 0 ||
        lnk_strcmp(soname, "libc++_shared.so") == 0 ||
        lnk_strcmp(soname, "libandroid.so")    == 0 ||
        lnk_strcmp(soname, "liblog.so")        == 0 ||
        lnk_strcmp(soname, "libz.so")          == 0
    );
}

static void load_needed_so(const char *soname) {
    for (int i = 0; i < g_nobjects; i++)
        if (lnk_strcmp(g_objects[i].soname, soname) == 0) return;

    if (g_nobjects >= MAX_OBJECTS) FATAL("too many objects");

    Object *obj = &g_objects[g_nobjects];
    lnk_memset(obj, 0, sizeof(*obj));
    lnk_strcpy(obj->soname, soname);

    /* Android system libraries (libc, libm, libdl, ...) have complex internal
     * startup code that requires the real Android linker to have initialised
     * TLS, the property system, and __libc_init before any of their code runs.
     * Loading them with our minimal linker crashes immediately.
     *
     * Since the guest main executable is statically linked against bionic and
     * provides all libc symbols itself, slots that point into these libraries
     * are never actually called at runtime — we just need them to not be
     * dangling.  Register a phantom object (no segments, no symbols) so the
     * "already loaded" check above fires for transitive dependencies, and let
     * undefined symbol resolution fall through to 0 (treated as weak). */
    if (is_system_lib(soname)) {
        lnk_puts("[linker64] phantom (system lib): ");
        lnk_puts(soname);
        lnk_puts("\n");
        g_nobjects++;   /* phantom registered; no segments loaded */
        return;
    }

    /* Search directories — declared as stack arrays, no global pointers */
    int fd = -1;
    if (!try_dir("/data/local/tmp", soname, obj->path, &fd))
    if (!try_dir("/system/lib64",   soname, obj->path, &fd))
    if (!try_dir("/vendor/lib64",   soname, obj->path, &fd))
    if (!try_dir("/lib",            soname, obj->path, &fd))
        try_dir("/usr/lib",         soname, obj->path, &fd);

    if (fd < 0) {
        lnk_puts("[linker64] cannot find: "); lnk_puts(soname); lnk_puts("\n");
        sys_exit(127);
    }

    load_so(obj, fd);
    sys_close(fd);
    g_nobjects++;

    if (obj->strtab) {
        for (int i = 0; i < obj->needed_count; i++)
            load_needed_so(obj->strtab + obj->needed_off[i]);
    }
}

/* -------------------------------------------------------------------------
 * linker_main — called from entry.S after self-relocation is complete
 * ---------------------------------------------------------------------- */

__attribute__((noreturn))
void linker_main(uintptr_t *sp)
{
    /* Parse stack */
    int      argc = (int)*sp;
    char   **argv = (char **)(sp + 1);
    char   **envp = argv + argc + 1;
    char   **ep   = envp; while (*ep) ep++;
    Elf64_auxv_t *auxv = (Elf64_auxv_t *)(ep + 1);

    uint64_t    exe_entry   = auxv_get(auxv, AT_ENTRY);
    Elf64_Phdr *exe_phdr    = (Elf64_Phdr *)auxv_get(auxv, AT_PHDR);
    uint16_t    exe_phnum   = (uint16_t)auxv_get(auxv, AT_PHNUM);
    uint16_t    exe_phentsize = (uint16_t)auxv_get(auxv, AT_PHENT);

    /* Derive exe load base.
     *
     * AT_ENTRY and AT_PHDR are already runtime VAs (elfuse adds the slide).
     * We need the slide to adjust PT_DYNAMIC.p_vaddr when calling parse_dynamic.
     *
     * Strategy 1: PT_PHDR — most reliable, p_vaddr is the link-time VA of the
     *             phdr table; AT_PHDR is the runtime VA; slide = AT_PHDR - p_vaddr.
     * Strategy 2: first PT_LOAD — p_vaddr is the link-time base; AT_PHDR points
     *             into the binary so slide = AT_PHDR - (phdr_offset - load_offset).
     *             Simpler: use AT_ENTRY - e_entry_offset.  But we don't have e_entry
     *             here, so fall back to first PT_LOAD: slide = AT_PHDR - (p_vaddr of
     *             the segment that contains the phdr).
     * Strategy 3: for ET_EXEC (non-PIE), slide = 0.
     *
     * In practice all NDK PIE binaries have PT_PHDR, so Strategy 1 always fires. */
    uint64_t exe_load_base = 0;
    {
        bool found = false;
        /* Strategy 1: PT_PHDR */
        for (int i = 0; i < exe_phnum; i++) {
            Elf64_Phdr *ph = (Elf64_Phdr *)((uint8_t *)exe_phdr + i * exe_phentsize);
            if (ph->p_type == PT_PHDR) {
                exe_load_base = (uint64_t)exe_phdr - ph->p_vaddr;
                found = true;
                break;
            }
        }
        /* Strategy 2: first PT_LOAD whose p_offset == 0 (the base segment) */
        if (!found) {
            for (int i = 0; i < exe_phnum; i++) {
                Elf64_Phdr *ph = (Elf64_Phdr *)((uint8_t *)exe_phdr + i * exe_phentsize);
                if (ph->p_type == PT_LOAD && ph->p_offset == 0) {
                    /* AT_PHDR = phdr_vaddr + slide; phdr is in this segment.
                     * phdr link-time VA = p_vaddr + (phdr file offset - p_offset)
                     *                   = p_vaddr + phdr_file_offset  (since p_offset==0)
                     * But we don't know phdr file offset here.  Simpler: the slide
                     * equals AT_ENTRY - exe_raw_entry.  We don't have exe_raw_entry
                     * directly, but AT_ENTRY = raw_entry + slide, and raw_entry is
                     * within [p_vaddr, p_vaddr+p_memsz) of some PT_LOAD.
                     * Best we can do without re-reading the ELF: use p_vaddr of
                     * the base load segment as the link-time base. */
                    exe_load_base = (uint64_t)exe_phdr - ph->p_vaddr;
                    /* This is approximate but works for standard NDK layout where
                     * the phdr immediately follows the ELF header at offset 0x40. */
                    break;
                }
            }
        }
    }

    /* Register exe as objects[0] */
    Object *exe_obj = &g_objects[0];
    lnk_memset(exe_obj, 0, sizeof(*exe_obj));
    lnk_strcpy(exe_obj->soname, argc > 0 && argv[0] ? argv[0] : "main");
    exe_obj->load_base = exe_load_base;

    for (int i = 0; i < exe_phnum; i++) {
        Elf64_Phdr *ph = (Elf64_Phdr *)((uint8_t *)exe_phdr + i * exe_phentsize);
        if (ph->p_type == PT_DYNAMIC) {
            parse_dynamic(exe_obj, ph->p_vaddr + exe_load_base);
            break;
        }
    }
    g_nobjects = 1;

    /* Load DT_NEEDED libraries */
    if (exe_obj->strtab) {
        for (int i = 0; i < exe_obj->needed_count; i++)
            load_needed_so(exe_obj->strtab + exe_obj->needed_off[i]);
    }

    /* Apply relocations to all objects.
     * For the exe (objects[0]), elfuse already mapped the segments — we need
     * to temporarily make the entire load range writable so we can patch GOT
     * slots (the RELRO region is PROT_READ after loading). */
    {
        /* Compute exe load range from phdrs */
        uint64_t load_min = UINT64_MAX, load_max = 0;
        for (int i = 0; i < exe_phnum; i++) {
            Elf64_Phdr *ph = (Elf64_Phdr *)((uint8_t *)exe_phdr + i * exe_phentsize);
            if (ph->p_type != PT_LOAD) continue;
            uint64_t start = PAGE_ALIGN_DOWN(ph->p_vaddr) + exe_load_base;
            uint64_t end   = PAGE_ALIGN_UP(ph->p_vaddr + ph->p_memsz) + exe_load_base;
            if (start < load_min) load_min = start;
            if (end   > load_max) load_max = end;
        }
        if (load_min != UINT64_MAX)
            sys_mprotect((void *)load_min, load_max - load_min, PROT_READ | PROT_WRITE);

        relocate_object(&g_objects[0]);

        /* Restore permissions segment by segment */
        if (load_min != UINT64_MAX) {
            for (int i = 0; i < exe_phnum; i++) {
                Elf64_Phdr *ph = (Elf64_Phdr *)((uint8_t *)exe_phdr + i * exe_phentsize);
                if (ph->p_type != PT_LOAD) continue;
                uint64_t start = PAGE_ALIGN_DOWN(ph->p_vaddr) + exe_load_base;
                uint64_t end   = PAGE_ALIGN_UP(ph->p_vaddr + ph->p_memsz) + exe_load_base;
                int prot = 0;
                if (ph->p_flags & PF_R) prot |= PROT_READ;
                if (ph->p_flags & PF_W) prot |= PROT_WRITE;
                if (ph->p_flags & PF_X) prot |= PROT_EXEC;
                sys_mprotect((void *)start, end - start, prot);
            }
        }

        /* Shared libs: map_start/map_end are set by load_so */
        for (int i = 1; i < g_nobjects; i++)
            relocate_object(&g_objects[i]);
    }

    /* Jump to exe _start, restoring original SP */
    register uint64_t entry __asm__("x0") = exe_entry;
    __asm__ volatile(
        "mov sp, %1\n"
        "br  %0\n"
        :: "r"(entry), "r"((uint64_t)sp) : "memory"
    );
    __builtin_unreachable();
}
