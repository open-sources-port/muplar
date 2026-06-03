/* entry.S — AArch64 ELF entry point for muplar-linker64
 *
 * CRITICAL: self-relocation must happen here in assembly, before ANY C code
 * runs.  C functions can reference global data (SEARCH_DIRS, g_objects, etc.)
 * via PC-relative GOT/RELA slots that contain unrelocated link-time addresses.
 * Even a function call through a PLT-like thunk may touch a GOT entry.
 *
 * We therefore apply all R_AARCH64_RELATIVE relocations in pure assembly
 * using only registers — no memory reads from globals — before jumping to C.
 *
 * Stack layout on entry (from elfuse / Linux kernel):
 *   SP → [ argc | argv[0..n] | NULL | envp[0..m] | NULL | auxv... | AT_NULL ]
 *
 * Registers on entry: all undefined except SP (kernel ABI).
 * AT_BASE (type=7) in auxv gives our own load base.
 */

    .section .text._start, "ax", @progbits
    .globl _start
    .type  _start, @function
_start:
    /* ------------------------------------------------------------------ *
     * Step 1: find our own load base from AT_BASE in auxv.
     *
     * We cannot use _DYNAMIC or any global symbol yet (unrelocated).
     * Walk the stack: skip argc + argv + envp, then scan auxv for AT_BASE.
     * ------------------------------------------------------------------ */
    mov     x19, sp             /* save original SP for later */

    /* skip argc */
    ldr     x20, [sp]           /* x20 = argc */
    add     x0, sp, #8          /* x0 → argv[0] */

    /* skip argv[0..argc] + NULL */
    add     x0, x0, x20, lsl #3 /* x0 → argv[argc] (the NULL) */
    add     x0, x0, #8          /* x0 → envp[0] */

    /* skip envp until NULL */
1:  ldr     x1, [x0], #8
    cbnz    x1, 1b
    /* x0 now points to auxv[0] */

    /* scan auxv for AT_BASE (type = 7) */
    mov     x21, #0             /* x21 = our load base (default 0) */
2:  ldp     x1, x2, [x0], #16  /* x1=type, x2=val */
    cbz     x1, 3f              /* AT_NULL → done */
    cmp     x1, #7              /* AT_BASE */
    csel    x21, x2, x21, eq
    b       2b
3:
    /* x21 = AT_BASE = our load base */

    /* ------------------------------------------------------------------ *
     * Step 2: find our _DYNAMIC section via PC-relative addressing.
     *
     * The linker emits a PC-relative reference to _DYNAMIC that works
     * regardless of load address.  We use adrp+add to get its runtime VA.
     * ------------------------------------------------------------------ */
    adrp    x22, _DYNAMIC
    add     x22, x22, :lo12:_DYNAMIC
    /* x22 = runtime VA of _DYNAMIC (PT_DYNAMIC contents) */

    /* ------------------------------------------------------------------ *
     * Step 3: walk _DYNAMIC to find DT_RELA (tag=7) and DT_RELASZ (tag=8).
     * Apply only R_AARCH64_RELATIVE (type=1027) entries.
     * Each Elf64_Dyn entry is { int64 tag; uint64 val } = 16 bytes.
     * Each Elf64_Rela entry is { uint64 offset; uint64 info; int64 addend } = 24 bytes.
     * ------------------------------------------------------------------ */
    mov     x23, #0             /* rela address (runtime, before reloc) */
    mov     x24, #0             /* rela size in bytes */

4:  ldp     x1, x2, [x22], #16 /* x1=tag, x2=val */
    cbz     x1, 5f              /* DT_NULL */
    cmp     x1, #7              /* DT_RELA */
    beq     .Lgot_rela
    cmp     x1, #8              /* DT_RELASZ */
    beq     .Lgot_relasz
    b       4b
.Lgot_rela:
    add     x23, x2, x21        /* rela runtime addr = val + load_base */
    b       4b
.Lgot_relasz:
    mov     x24, x2
    b       4b

5:
    /* ------------------------------------------------------------------ *
     * Step 4: apply R_AARCH64_RELATIVE relocations.
     * r_info & 0xffffffff == 1027 (R_AARCH64_RELATIVE)
     * *slot = load_base + r_addend
     * ------------------------------------------------------------------ */
    cbz     x23, .Lreloc_done   /* no RELA section */
    cbz     x24, .Lreloc_done

    mov     x0, x23             /* current rela entry pointer */
    add     x1, x23, x24        /* end pointer */

6:  cmp     x0, x1
    bge     .Lreloc_done

    ldp     x2, x3, [x0]        /* x2=r_offset, x3=r_info */
    ldr     x4, [x0, #16]       /* x4=r_addend (signed, but ldr is fine for add) */
    add     x0, x0, #24         /* advance to next entry */

    /* check type == R_AARCH64_RELATIVE (1027 = 0x403) */
    mov     x5, #0x403
    and     x6, x3, #0xffffffff
    cmp     x6, x5
    bne     6b

    /* *slot = load_base + addend */
    add     x2, x2, x21         /* slot address = r_offset + load_base */
    add     x4, x4, x21         /* value = addend + load_base */
    str     x4, [x2]
    b       6b

.Lreloc_done:
    /* ------------------------------------------------------------------ *
     * Step 5: self-relocation complete.  Now safe to call C.
     * Pass the original SP so linker_main can re-parse argc/argv/envp/auxv.
     * ------------------------------------------------------------------ */
    mov     x0, x19
    bl      linker_main

    /* Never reached */
1:  b       1b

    .size _start, . - _start

    /* Make _DYNAMIC visible to the assembler for the adrp above */
    .extern _DYNAMIC

    .globl tlsdesc_static_resolver
    .type tlsdesc_static_resolver, @function
tlsdesc_static_resolver:
    ldr     x0, [x0, #8]
    ret
    .size tlsdesc_static_resolver, . - tlsdesc_static_resolver
