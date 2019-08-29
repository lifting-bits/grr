/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "assembly.S"

    .file "granary/arch/x86/syscall.S"

    TEXT_SECTION

    // NextAppPC32 cache::Call(os::Process32 *process, CachePC block);
    .align 16
    .globl SYMBOL(sys_sigreturn);
SYMBOL(sys_sigreturn):
    .cfi_startproc
    mov     eax, 15  // `__NR_rt_sigreturn`.
    syscall
    .cfi_endproc
    ud2
