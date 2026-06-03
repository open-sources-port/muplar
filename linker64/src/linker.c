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

#define MAX_OBJECTS 1024
#define MAX_NEEDED  256

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

static void lnk_print_hex(uint64_t val) {
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        int nibble = (val >> ((15 - i) * 4)) & 0xf;
        buf[2 + i] = (nibble < 10) ? ('0' + nibble) : ('a' + (nibble - 10));
    }
    buf[18] = '\0';
    lnk_puts(buf);
}

#define FATAL(msg) do { lnk_puts("[linker64] FATAL: " msg "\n"); sys_exit(127); } while(0)

extern void tlsdesc_static_resolver(void);
static uint64_t lookup_symbol(const char *name);
void* __loader_shared_globals(void);
extern uint8_t g_shared_globals[4096];

static uint64_t g_hwcap;
static Elf64_auxv_t *g_auxv;

typedef uint64_t (*resolver_t)(uint64_t, Elf64_auxv_t *);

static void clear_cache(void *start, void *end) {
    uint64_t addr = (uint64_t)start;
    __asm__ volatile (
        "dc cvau, %0\n"
        "dsb ish\n"
        "ic ivau, %0\n"
        "dsb ish\n"
        "isb\n"
        :: "r"(addr) : "memory"
    );
}

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
    const uint8_t *android_rela;
    size_t         android_rela_size;
    uint32_t   *gnu_hash;
    uint32_t    needed_off[MAX_NEEDED];
    int         needed_count;
    char        path[256];
    char        soname[128];
    uint64_t    tls_offset;
    uint64_t    tls_vaddr;
    size_t      tls_filesz;
    size_t      tls_memsz;
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
        case 36:
        case DT_RELR:     obj->relr         = (uint64_t *)(dyn->d_val + obj->load_base); break;
        case 35:
        case DT_RELRSZ:   obj->relr_count   = dyn->d_val / sizeof(uint64_t); break;
        case DT_ANDROID_RELA:   obj->android_rela      = (const uint8_t *)(dyn->d_val + obj->load_base); break;
        case DT_ANDROID_RELASZ: obj->android_rela_size = (size_t)dyn->d_val; break;
        case 0x6ffffef5:  obj->gnu_hash     = (uint32_t *)(dyn->d_val + obj->load_base); break;
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

    /* Find PT_DYNAMIC and PT_TLS */
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            parse_dynamic(obj, phdrs[i].p_vaddr + obj->load_base);
        } else if (phdrs[i].p_type == PT_TLS) {
            obj->tls_vaddr  = phdrs[i].p_vaddr;
            obj->tls_filesz = phdrs[i].p_filesz;
            obj->tls_memsz  = phdrs[i].p_memsz;
        }
    }
}

static const char *lnk_basename(const char *path) {
    const char *slash = path;
    const char *p = path;
    while (*p) {
        if (*p == '/') slash = p + 1;
        p++;
    }
    return slash;
}

__attribute__((noreturn))
static void lnk_libc_init(
    void* raw_args,
    void (*onexit)(void),
    int (*slingshot)(int, char**, char**),
    void* struct_args
) {
    uintptr_t *sp = (uintptr_t *)raw_args;
    int argc = (int)sp[0];
    char **argv = (char **)&sp[1];
    char **envp = &argv[argc + 1];

    /* Initialize Bionic libc's environ global variable if present */
    uint64_t environ_addr = lookup_symbol("environ");
    if (environ_addr) {
        *(char ***)environ_addr = envp;
    }

    /* Initialize Bionic libc's __progname global variable if present */
    uint64_t progname_addr = lookup_symbol("__progname");
    if (progname_addr && argc > 0 && argv[0]) {
        *(const char **)progname_addr = lnk_basename(argv[0]);
    }

    /* Call the main function */
    int rc = slingshot(argc, argv, envp);

    /* Exit with the return value of main */
    sys_exit(rc);
    __builtin_unreachable();
}

/* -------------------------------------------------------------------------
 * Symbol lookup
 * ---------------------------------------------------------------------- */

static uint32_t gnu_hash(const char *name) {
    uint32_t h = 5381;
    for (; *name; name++) {
        h = (h << 5) + h + (uint32_t)(uint8_t)*name;
    }
    return h;
}

static Elf64_Sym *lookup_gnu_hash(Object *obj, const char *name) {
    if (!obj->gnu_hash || !obj->symtab || !obj->strtab) return NULL;
    
    uint32_t hash = gnu_hash(name);
    uint32_t nbuckets    = obj->gnu_hash[0];
    uint32_t symoffset   = obj->gnu_hash[1];
    uint32_t bloom_size  = obj->gnu_hash[2];
    uint32_t bloom_shift = obj->gnu_hash[3];
    
    uint64_t *bloom   = (uint64_t *)(obj->gnu_hash + 4);
    uint32_t *buckets = (uint32_t *)(bloom + bloom_size);
    uint32_t *chains  = buckets + nbuckets;
    
    uint64_t word = bloom[(hash / 64) % bloom_size];
    uint64_t mask = (1ULL << (hash % 64)) | (1ULL << ((hash >> bloom_shift) % 64));
    if ((word & mask) != mask) return NULL;
    
    uint32_t bucket = buckets[hash % nbuckets];
    if (bucket < symoffset) return NULL;
    
    uint32_t loop_limit = 10000;
    for (uint32_t i = bucket; loop_limit > 0; i++, loop_limit--) {
        uint32_t chain_val = chains[i - symoffset];
        if (((chain_val ^ hash) >> 1) == 0) {
            Elf64_Sym *sym = &obj->symtab[i];
            if (sym->st_name < obj->strsz && lnk_strcmp(obj->strtab + sym->st_name, name) == 0) {
                return sym;
            }
        }
        if (chain_val & 1) break;
    }
    return NULL;
}

static void load_needed_so(const char *soname);
static void relocate_object(Object *obj, uint64_t hwcap, Elf64_auxv_t *auxv);

static const char* get_basename(const char* path) {
    const char* base = path;
    while (*path) {
        if (*path == '/') {
            base = path + 1;
        }
        path++;
    }
    return base;
}

void* lnk_dlopen(const char* filename, int flags) {
    lnk_puts("[linker64] lnk_dlopen: "); lnk_puts(filename ? filename : "NULL"); lnk_puts("\n");
    if (!filename) {
        return &g_objects[0];
    }
    
    const char* base_filename = get_basename(filename);
    for (int i = 0; i < g_nobjects; i++) {
        if (lnk_strcmp(g_objects[i].soname, filename) == 0 ||
            lnk_strcmp(g_objects[i].path, filename) == 0 ||
            lnk_strcmp(g_objects[i].soname, base_filename) == 0) {
            return &g_objects[i];
        }
    }
    
    int old_nobjects = g_nobjects;
    load_needed_so(base_filename);
    
    for (int i = old_nobjects; i < g_nobjects; i++) {
        relocate_object(&g_objects[i], g_hwcap, g_auxv);
    }
    
    for (int i = 0; i < g_nobjects; i++) {
        if (lnk_strcmp(g_objects[i].soname, base_filename) == 0) {
            return &g_objects[i];
        }
    }
    return NULL;
}

void* lnk_dlsym(void* handle, const char* symbol) {
    lnk_puts("[linker64] lnk_dlsym: "); lnk_puts(symbol); lnk_puts("\n");
    if (handle == NULL || handle == (void*)-1LL) {
        return (void*)lookup_symbol(symbol);
    }
    
    Object* obj = (Object*)handle;
    if (obj->gnu_hash) {
        Elf64_Sym *sym = lookup_gnu_hash(obj, symbol);
        if (sym) {
            uint64_t sym_val = sym->st_value + obj->load_base;
            if (ELF64_ST_TYPE(sym->st_info) == STT_GNU_IFUNC) {
                resolver_t resolver = (resolver_t)sym_val;
                return (void*)resolver(g_hwcap, g_auxv);
            }
            return (void*)sym_val;
        }
    } else {
        if (obj->symtab && obj->strtab) {
            for (int j = 1; j < 4096; j++) {
                Elf64_Sym *sym = &obj->symtab[j];
                if (sym->st_shndx == SHN_UNDEF) continue;
                if (sym->st_name >= obj->strsz) break;
                if (lnk_strcmp(obj->strtab + sym->st_name, symbol) == 0) {
                    uint64_t sym_val = sym->st_value + obj->load_base;
                    if (ELF64_ST_TYPE(sym->st_info) == STT_GNU_IFUNC) {
                        resolver_t resolver = (resolver_t)sym_val;
                        return (void*)resolver(g_hwcap, g_auxv);
                    }
                    return (void*)sym_val;
                }
            }
        }
    }
    
    return (void*)lookup_symbol(symbol);
}

int lnk_dlclose(void* handle) {
    lnk_puts("[linker64] lnk_dlclose\n");
    return 0;
}

char* lnk_dlerror(void) {
    return NULL;
}

struct dl_phdr_info {
    uint64_t dlpi_addr;
    const char *dlpi_name;
    const Elf64_Phdr *dlpi_phdr;
    uint16_t dlpi_phnum;
};

int lnk_dl_iterate_phdr(int (*callback)(struct dl_phdr_info*, size_t, void*), void* data) {
    lnk_puts("[linker64] lnk_dl_iterate_phdr\n");
    for (int i = 0; i < g_nobjects; i++) {
        Object *obj = &g_objects[i];
        if (obj->load_base == 0 && i > 0) continue;
        
        Elf64_Ehdr *ehdr = (Elf64_Ehdr *)obj->load_base;
        if (ehdr->e_ident[0] != 0x7f || ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
            continue;
        }
        
        struct dl_phdr_info info;
        info.dlpi_addr = obj->load_base;
        info.dlpi_name = obj->soname;
        info.dlpi_phdr = (const Elf64_Phdr *)(obj->load_base + ehdr->e_phoff);
        info.dlpi_phnum = ehdr->e_phnum;
        
        int ret = callback(&info, sizeof(info), data);
        if (ret != 0) return ret;
    }
    return 0;
}

static uint64_t lookup_symbol(const char *name) {
    if (lnk_strcmp(name, "__libc_init") == 0) {
        return (uint64_t)(uintptr_t)&lnk_libc_init;
    }
    if (lnk_strcmp(name, "__loader_shared_globals") == 0) {
        return (uint64_t)(uintptr_t)&__loader_shared_globals;
    }
    if (lnk_strcmp(name, "dlopen") == 0) {
        return (uint64_t)(uintptr_t)&lnk_dlopen;
    }
    if (lnk_strcmp(name, "dlsym") == 0) {
        return (uint64_t)(uintptr_t)&lnk_dlsym;
    }
    if (lnk_strcmp(name, "dlclose") == 0) {
        return (uint64_t)(uintptr_t)&lnk_dlclose;
    }
    if (lnk_strcmp(name, "dlerror") == 0) {
        return (uint64_t)(uintptr_t)&lnk_dlerror;
    }
    if (lnk_strcmp(name, "dl_iterate_phdr") == 0) {
        return (uint64_t)(uintptr_t)&lnk_dl_iterate_phdr;
    }
    for (int i = 0; i < g_nobjects; i++) {
        Object *obj = &g_objects[i];
        if (obj->gnu_hash) {
            Elf64_Sym *sym = lookup_gnu_hash(obj, name);
            if (sym) {
                uint64_t sym_val = sym->st_value + obj->load_base;
                if (ELF64_ST_TYPE(sym->st_info) == STT_GNU_IFUNC) {
                    resolver_t resolver = (resolver_t)sym_val;
                    return resolver(g_hwcap, g_auxv);
                }
                return sym_val;
            }
        } else {
            if (!obj->symtab || !obj->strtab) continue;
            for (int j = 1; j < 4096; j++) {
                Elf64_Sym *sym = &obj->symtab[j];
                if (sym->st_shndx == SHN_UNDEF) continue;
                if (sym->st_name >= obj->strsz) break;
                if (lnk_strcmp(obj->strtab + sym->st_name, name) == 0) {
                    uint64_t sym_val = sym->st_value + obj->load_base;
                    if (ELF64_ST_TYPE(sym->st_info) == STT_GNU_IFUNC) {
                        resolver_t resolver = (resolver_t)sym_val;
                        return resolver(g_hwcap, g_auxv);
                    }
                    return sym_val;
                }
            }
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Relocation
 * ---------------------------------------------------------------------- */



static void apply_rela(Object *obj, Elf64_Rela *rela, size_t count, uint64_t hwcap, Elf64_auxv_t *auxv) {
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
                    if (ELF64_ST_TYPE(sym->st_info) == STT_GNU_IFUNC) {
                        resolver_t resolver = (resolver_t)sym_val;
                        sym_val = resolver(g_hwcap, g_auxv);
                    }
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
        case R_AARCH64_TLS_TPREL64: {
            uint64_t sym_val = 0;
            if (sym_idx && obj->symtab && obj->strtab) {
                Elf64_Sym *sym = &obj->symtab[sym_idx];
                if (sym->st_shndx != SHN_UNDEF) {
                    sym_val = obj->tls_offset + sym->st_value;
                } else {
                    const char *name = obj->strtab + sym->st_name;
                    for (int k = 0; k < g_nobjects; k++) {
                        Object *target = &g_objects[k];
                        if (!target->symtab || !target->strtab) continue;
                        for (int j = 1; j < 4096; j++) {
                            Elf64_Sym *s = &target->symtab[j];
                            if (s->st_shndx == SHN_UNDEF) continue;
                            if (s->st_name >= target->strsz) break;
                            if (lnk_strcmp(target->strtab + s->st_name, name) == 0) {
                                sym_val = target->tls_offset + s->st_value;
                                break;
                            }
                        }
                        if (sym_val) break;
                    }
                }
            } else {
                sym_val = obj->tls_offset;
            }
            *slot = sym_val + (uint64_t)addend;
            break;
        }
        case R_AARCH64_TLSDESC: {
            uint64_t sym_val = 0;
            if (sym_idx && obj->symtab && obj->strtab) {
                Elf64_Sym *sym = &obj->symtab[sym_idx];
                if (sym->st_shndx != SHN_UNDEF) {
                    sym_val = obj->tls_offset + sym->st_value;
                } else {
                    const char *name = obj->strtab + sym->st_name;
                    for (int k = 0; k < g_nobjects; k++) {
                        Object *target = &g_objects[k];
                        if (!target->symtab || !target->strtab) continue;
                        for (int j = 1; j < 4096; j++) {
                            Elf64_Sym *s = &target->symtab[j];
                            if (s->st_shndx == SHN_UNDEF) continue;
                            if (s->st_name >= target->strsz) break;
                            if (lnk_strcmp(target->strtab + s->st_name, name) == 0) {
                                sym_val = target->tls_offset + s->st_value;
                                break;
                            }
                        }
                        if (sym_val) break;
                    }
                }
            } else {
                sym_val = obj->tls_offset;
            }
            slot[0] = (uint64_t)&tlsdesc_static_resolver;
            slot[1] = sym_val + (uint64_t)addend;
            break;
        }
        case R_AARCH64_IRELATIVE: {
            resolver_t resolver = (resolver_t)(obj->load_base + (uint64_t)addend);
            *slot = resolver(hwcap, auxv);
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

#define RELOCATION_GROUPED_BY_INFO_FLAG          1
#define RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG  2
#define RELOCATION_GROUPED_BY_ADDEND_FLAG        4
#define RELOCATION_GROUP_HAS_ADDEND_FLAG         8

typedef struct {
    const uint8_t *current;
    const uint8_t *end;
} sleb128_decoder_t;

static inline void sleb128_decoder_init(sleb128_decoder_t *dec, const uint8_t *buf, size_t sz) {
    dec->current = buf;
    dec->end = buf + sz;
}

static inline uint64_t sleb128_decoder_pop_front(sleb128_decoder_t *dec) {
    uint64_t value = 0;
    uint32_t shift = 0;
    uint8_t byte;
    do {
        if (dec->current >= dec->end) {
            FATAL("sleb128_decoder ran out of bounds");
        }
        byte = *dec->current++;
        value |= ((uint64_t)(byte & 127) << shift);
        shift += 7;
    } while (byte & 128);

    if (shift < 64 && (byte & 64)) {
        value |= -(1ULL << shift);
    }
    return value;
}

static void apply_android_rela(Object *obj, const uint8_t *packed_relocs, size_t size, uint64_t hwcap, Elf64_auxv_t *auxv) {
    if (size < 4) return;
    if (packed_relocs[0] != 'A' || packed_relocs[1] != 'P' || packed_relocs[2] != 'S' || packed_relocs[3] != '2') {
        FATAL("invalid Android packed relocations signature");
    }

    sleb128_decoder_t dec;
    sleb128_decoder_init(&dec, packed_relocs + 4, size - 4);

    uint64_t num_relocs = sleb128_decoder_pop_front(&dec);
    uint64_t r_offset = sleb128_decoder_pop_front(&dec);
    uint64_t r_info = 0;
    int64_t r_addend = 0;

    for (uint64_t idx = 0; idx < num_relocs; ) {
        uint64_t group_size = sleb128_decoder_pop_front(&dec);
        uint64_t group_flags = sleb128_decoder_pop_front(&dec);

        uint64_t group_r_offset_delta = 0;
        if (group_flags & RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG) {
            group_r_offset_delta = sleb128_decoder_pop_front(&dec);
        }
        if (group_flags & RELOCATION_GROUPED_BY_INFO_FLAG) {
            r_info = sleb128_decoder_pop_front(&dec);
        }

        uint64_t group_flags_reloc = group_flags & (RELOCATION_GROUP_HAS_ADDEND_FLAG | RELOCATION_GROUPED_BY_ADDEND_FLAG);
        if (group_flags_reloc == RELOCATION_GROUP_HAS_ADDEND_FLAG) {
            // Each relocation has an addend. We will decode it inside the loop.
        } else if (group_flags_reloc == (RELOCATION_GROUP_HAS_ADDEND_FLAG | RELOCATION_GROUPED_BY_ADDEND_FLAG)) {
            r_addend += (int64_t)sleb128_decoder_pop_front(&dec);
        } else {
            r_addend = 0;
        }

        for (uint64_t i = 0; i < group_size; ++i) {
            if (group_flags & RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG) {
                r_offset += group_r_offset_delta;
            } else {
                r_offset += sleb128_decoder_pop_front(&dec);
            }
            if ((group_flags & RELOCATION_GROUPED_BY_INFO_FLAG) == 0) {
                r_info = sleb128_decoder_pop_front(&dec);
            }
            if (group_flags_reloc == RELOCATION_GROUP_HAS_ADDEND_FLAG) {
                r_addend += (int64_t)sleb128_decoder_pop_front(&dec);
            }

            Elf64_Rela rela;
            rela.r_offset = r_offset;
            rela.r_info = r_info;
            rela.r_addend = r_addend;
            apply_rela(obj, &rela, 1, hwcap, auxv);
        }

        idx += group_size;
    }
}

static void relocate_object(Object *obj, uint64_t hwcap, Elf64_auxv_t *auxv) {
    apply_relr(obj);
    if (obj->android_rela && obj->android_rela_size) {
        apply_android_rela(obj, obj->android_rela, obj->android_rela_size, hwcap, auxv);
    }
    if (obj->rela   && obj->rela_count)   apply_rela(obj, obj->rela,   obj->rela_count, hwcap, auxv);
    if (obj->jmprel && obj->jmprel_count) apply_rela(obj, obj->jmprel, obj->jmprel_count, hwcap, auxv);
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
    if (lnk_strcmp(soname, "libdl.so") == 0) return true;
    if (lnk_strcmp(soname, "libdl_android.so") == 0) return true;
    if (lnk_strcmp(soname, "ld-android.so") == 0) return true;
    return false;
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
    if (!try_dir("/apex/com.android.art/lib64", soname, obj->path, &fd))
    if (!try_dir("/apex/com.android.runtime/lib64", soname, obj->path, &fd))
    if (!try_dir("/apex/com.android.i18n/lib64", soname, obj->path, &fd))
    if (!try_dir("/apex/com.android.conscrypt/lib64", soname, obj->path, &fd))
    if (!try_dir("/vendor/lib64",   soname, obj->path, &fd))
    if (!try_dir("/lib",            soname, obj->path, &fd))
        try_dir("/usr/lib",         soname, obj->path, &fd);

    if (fd < 0) {
        lnk_puts("[linker64] cannot find: "); lnk_puts(soname); lnk_puts(" (registering phantom)\n");
        g_nobjects++;   /* phantom registered; no segments loaded */
        return;
    }

    load_so(obj, fd);
    sys_close(fd);
    lnk_puts("[linker64] loaded "); lnk_puts(obj->soname); lnk_puts(" at "); lnk_print_hex(obj->load_base); lnk_puts("\n");
    g_nobjects++;

    if (obj->strtab) {
        for (int i = 0; i < obj->needed_count; i++)
            load_needed_so(obj->strtab + obj->needed_off[i]);
    }
}

static uint8_t g_tls_block[16384] __attribute__((aligned(16)));
static uint8_t g_main_thread[1024] __attribute__((aligned(16)));

uint8_t g_shared_globals[4096] __attribute__((aligned(16)));

void* __loader_shared_globals(void) {
    return &g_shared_globals;
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

    /* Set the auxv pointer inside dynamic globals for getauxval() */
    *(Elf64_auxv_t **)(&g_shared_globals[0x418]) = auxv;

    g_hwcap = auxv_get(auxv, AT_HWCAP);
    g_auxv = auxv;

    uint64_t    hwcap       = g_hwcap;
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
    lnk_puts("[linker64] loaded executable "); lnk_puts(exe_obj->soname); lnk_puts(" at "); lnk_print_hex(exe_obj->load_base); lnk_puts("\n");

    for (int i = 0; i < exe_phnum; i++) {
        Elf64_Phdr *ph = (Elf64_Phdr *)((uint8_t *)exe_phdr + i * exe_phentsize);
        if (ph->p_type == PT_DYNAMIC) {
            parse_dynamic(exe_obj, ph->p_vaddr + exe_load_base);
        } else if (ph->p_type == PT_TLS) {
            exe_obj->tls_vaddr  = ph->p_vaddr;
            exe_obj->tls_filesz = ph->p_filesz;
            exe_obj->tls_memsz  = ph->p_memsz;
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
        /* Assign static TLS offsets and initialize TLS templates */
        {
            uint64_t current_tls_offset = 256; /* start after 256-byte TCB/Bionic slots */
            for (int i = 0; i < g_nobjects; i++) {
                Object *obj = &g_objects[i];
                if (obj->tls_memsz > 0) {
                    current_tls_offset = (current_tls_offset + 15) & ~15ULL; /* align to 16 bytes */
                    obj->tls_offset = current_tls_offset;
                    current_tls_offset += obj->tls_memsz;
                }
            }

            lnk_memset(g_tls_block, 0, sizeof(g_tls_block));
            for (int i = 0; i < g_nobjects; i++) {
                Object *obj = &g_objects[i];
                if (obj->tls_memsz > 0 && obj->tls_vaddr > 0) {
                    void *src = (void *)(obj->load_base + obj->tls_vaddr);
                    void *dst = (void *)(g_tls_block + obj->tls_offset);
                    for (size_t k = 0; k < obj->tls_filesz; k++) {
                        ((uint8_t *)dst)[k] = ((uint8_t *)src)[k];
                    }
                }
            }

            /* Initialize Bionic's TLS slots in the TCB */
            uint64_t *tcb_slots = (uint64_t *)g_tls_block;
            tcb_slots[0] = (uint64_t)g_tls_block;       /* TLS_SLOT_SELF */
            tcb_slots[1] = (uint64_t)g_main_thread;     /* TLS_SLOT_THREAD_ID */
            
            /* Initialize mock pthread_internal_t for main thread */
            lnk_memset(g_main_thread, 0, sizeof(g_main_thread));
            
            /* Put tid at offset 0x10 inside pthread_internal_t */
            *(uint32_t *)&g_main_thread[0x10] = (uint32_t)sys_gettid();

            uint64_t tp = (uint64_t)g_tls_block;
            __asm__ volatile("msr tpidr_el0, %0" :: "r"(tp));
        }

        /* Print loaded objects metadata */
        for (int i = 0; i < g_nobjects; i++) {
            Object *obj = &g_objects[i];
            lnk_puts("[linker64] Object: "); lnk_puts(obj->soname);
            lnk_puts(" base: "); lnk_print_hex(obj->load_base);
            lnk_puts(" relr: "); lnk_print_hex((uint64_t)obj->relr);
            lnk_puts(" relr_count: "); lnk_print_hex(obj->relr_count);
            lnk_puts(" gnu_hash: "); lnk_print_hex((uint64_t)obj->gnu_hash);
            lnk_puts("\n");
        }

        if (load_min != UINT64_MAX)
            sys_mprotect((void *)load_min, load_max - load_min, PROT_READ | PROT_WRITE);

        relocate_object(&g_objects[0], hwcap, auxv);

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
        for (int i = 1; i < g_nobjects; i++) {
            if (lnk_strcmp(g_objects[i].soname, "libandroid_runtime.so") == 0) {
                uint64_t *slot = (uint64_t *)(g_objects[i].load_base + 0x2e37e8);
                lnk_puts("[linker64] libandroid_runtime.so GOT[2e37e8] before: ");
                lnk_print_hex(*slot);
                lnk_puts("\n");
            }
            relocate_object(&g_objects[i], hwcap, auxv);
            if (lnk_strcmp(g_objects[i].soname, "libandroid_runtime.so") == 0) {
                uint64_t *slot = (uint64_t *)(g_objects[i].load_base + 0x2e37e8);
                lnk_puts("[linker64] libandroid_runtime.so GOT[2e37e8] after: ");
                lnk_print_hex(*slot);
                lnk_puts("\n");
            }
        }
    }

    /* Patch rtld_db_dlactivity in ld-android.so if present to avoid SIGTRAP */
    {
        uint64_t dlactivity_addr = lookup_symbol("rtld_db_dlactivity");
        if (dlactivity_addr) {
            lnk_puts("[linker64] Patching rtld_db_dlactivity at ");
            lnk_print_hex(dlactivity_addr);
            lnk_puts("\n");

            uint64_t page_start = PAGE_ALIGN_DOWN(dlactivity_addr);
            
            /* Temporarily make page writable */
            sys_mprotect((void *)page_start, PAGE_SIZE, PROT_READ | PROT_WRITE);

            uint32_t *instr = (uint32_t *)(dlactivity_addr + 4);
            
            /* Overwrite brk #0x1 (0xd4200020) with ret (0xd65f03c0) */
            *instr = 0xd65f03c0;

            /* Invalidate instruction cache for this cache line */
            clear_cache((void *)dlactivity_addr, (void *)(dlactivity_addr + 8));

            /* Restore page permissions to read + execute */
            sys_mprotect((void *)page_start, PAGE_SIZE, PROT_READ | PROT_EXEC);
            
            lnk_puts("[linker64] rtld_db_dlactivity successfully patched to ret\n");
        }
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
