/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifdef __APPLE__
# define SYMBOL(x) _ ## x
#else
# define SYMBOL(x) x
#endif

    .file "granary/arch/x86/branch_tracer.S"
    .intel_syntax noprefix
    .text

    .extern SYMBOL(TraceBranchImpl)

    // r15      os::Process32 *     Process32 object.
    // r14      index::Value        Meta-data about this block.
    // r13                          Scratch.
    // r12                          Scratch.
    // r11                          Scratch.
    // r10      Addr32              EIP.
    // r9       Addr32              ESP.
    // r8       Addr64              64-bit base of 32-bit address space.
    //
    .align 16
    .globl SYMBOL(TraceBranch)
SYMBOL(TraceBranch):
    .cfi_startproc

    pushfq
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov     edi, dword ptr [r15 + 52]
    mov     esi, r14d
    mov     edx, r10d
    call    SYMBOL(TraceBranchImpl)

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    popfq

    ret
    .cfi_endproc
    ud2
