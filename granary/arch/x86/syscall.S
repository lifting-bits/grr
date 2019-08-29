/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifdef __APPLE__
# define SYMBOL(x) _ ## x
#else
# define SYMBOL(x) x
#endif

    .file "granary/arch/x86/syscall.S"
    .intel_syntax noprefix
    .text

    // NextAppPC32 cache::Call(os::Process32 *process, CachePC block);
    .align 16
    .globl SYMBOL(sys_sigreturn);
SYMBOL(sys_sigreturn):
    .cfi_startproc
    mov     eax, 15  // `__NR_rt_sigreturn`.
    syscall
    .cfi_endproc
    ud2
