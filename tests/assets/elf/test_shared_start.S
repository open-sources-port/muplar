/* test_shared_start.S — minimal _start for test_shared
 *
 * Replaces crtbegin_dynamic.o so we don't call __libc_init (which needs
 * real libc).  Calls main() directly and exits with its return value.
 *
 * Stack on entry (Linux AArch64 ABI):
 *   SP → argc, argv[0..n], NULL, envp[0..m], NULL, auxv...
 */
    .section .text._start, "ax", @progbits
    .globl _start
    .type  _start, @function
_start:
    /* x0 = argc (load from stack) */
    ldr     x0, [sp]
    /* x1 = argv (sp + 8) */
    add     x1, sp, #8
    /* x2 = envp (sp + 8 + (argc+1)*8) */
    add     x2, x1, x0, lsl #3
    add     x2, x2, #8

    bl      main

    /* exit(result): syscall 94 = exit_group */
    mov     x8, #94
    svc     #0
    b       .
    .size _start, . - _start
