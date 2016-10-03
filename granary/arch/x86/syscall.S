/* Copyright 2015 Peter Goodman, all rights reserved. */

    .file "granary/arch/x86/syscall.S"
    .intel_syntax noprefix
    .text

    // NextAppPC32 cache::Call(os::Process32 *process, CachePC block);
    .align 16
    .globl sys_sigreturn;
    .type sys_sigreturn, @function
sys_sigreturn:
    .cfi_startproc
    mov     eax, 15  // `__NR_rt_sigreturn`.
    syscall
    .cfi_endproc
    .size   sigreturn, .-sigreturn
    ud2
