/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifdef __APPLE__
# define SYMBOL(x) _ ## x
#else
# define SYMBOL(x) x
#endif

    .file "granary/arch/x86/cache.S"
    .intel_syntax noprefix

    .data
SYMBOL(granary_stack_pointer):
    .quad 0

    .extern SYMBOL(gInlineCache)


    // Used to return from the code cache with a bad block.
    .align 16
    .globl SYMBOL(granary_bad_block)
SYMBOL(granary_bad_block):
    .cfi_startproc
    xor r14, r14
    not r14
    mov r11, [RIP + SYMBOL(granary_stack_pointer)]
    sub r11, 8
    mov rsp, r11
    ret
    .cfi_endproc
    ud2

    // CachePC cache::Call(os::Process32 *process, CachePC block);
    .align 16
    .globl SYMBOL(_ZN7granary5cache4CallEPNS_2os9Process32EPh);
SYMBOL(_ZN7granary5cache4CallEPNS_2os9Process32EPh):
    .cfi_startproc

    /* Save the Linux ABI callee-saved regs */
    push rbx ; .cfi_def_cfa_offset 16
    push rbp ; .cfi_def_cfa_offset 24
    push r12 ; .cfi_def_cfa_offset 32
    push r13 ; .cfi_def_cfa_offset 40
    push r14 ; .cfi_def_cfa_offset 48
    push r15 ; .cfi_def_cfa_offset 56

    /* Process address */
    mov r15, rdi

    /* Block address */
    mov r14, rsi

    /* Base of memory */
    mov r8, [r15]

    /* Restore EFLAGS */
    push word ptr [r15 + 44] ; .cfi_def_cfa_offset 60
    popf ; .cfi_def_cfa_offset 56

    /* Restore the process state */
    mov edi, dword ptr [r15 + 8]
    mov esi, dword ptr [r15 + 12]
    mov ebp, dword ptr [r15 + 16]
    mov ebx, dword ptr [r15 + 20]
    mov edx, dword ptr [r15 + 24]
    mov ecx, dword ptr [r15 + 28]
    mov eax, dword ptr [r15 + 32]
    mov r9d, dword ptr [r15 + 36]  /* Emulated stack pointer */
                                   /* Don't restore the emulated pc */

    // So that we can jump back into the top-level cache call.
    mov [RIP + SYMBOL(granary_stack_pointer)], rsp

    /* Call into the block */
.Lenter_cache:
    call r14

    /* Check the inline cache */
    pushfq
    push r11

    /* The target PC is NULL, this can trivially match against empty cache
     * entries, and it's an error case, so we need to handle it properly. */
    test r10, r10
    jz .Lexit_cache

    /* After the call, r14 will be the index::Val of the last executed block.
     * Check that this block doesn't end with a system call. */
    bt r14, 62
    jc .Lexit_cache

    /* Find the cache offset for the probes. We use the last branch PC to
     * index into the inline cache, and scale it by the number of probes per
     * cache entry. */
    mov r11d, dword ptr [r15 + 52]
    and r11, 0x7ff
    shl r11, 3  /* Scale by 8 bytes, the size of each entry. */

    /* Get first probe point into the inline cache. */
    push r12
    lea r12, [RIP + SYMBOL(gInlineCache)]
    lea r11, [r11 + r12]
    pop r12

#define CHECK_CACHE(entry) \
    cmp dword ptr [r11 + (entry * 8)], r10d ; \
    jnz .Lentry_invalid ## entry ; \
        mov r14d, dword ptr [r11 + (entry * 8 + 4)] ; \
        jmp .Lre_enter_cache ; \
    \
    .Lentry_invalid ## entry:

    CHECK_CACHE(0)
    CHECK_CACHE(1)
    CHECK_CACHE(2)
    CHECK_CACHE(3)

    jmp .Lexit_cache

.Lre_enter_cache:
    pop r11
    popfq
    jmp .Lenter_cache

.Lexit_cache:
    pop r11
    popfq

    /* Save the process state */
    mov dword ptr [r15 + 8], edi
    mov dword ptr [r15 + 12], esi
    mov dword ptr [r15 + 16], ebp
    mov dword ptr [r15 + 20], ebx
    mov dword ptr [r15 + 24], edx
    mov dword ptr [r15 + 28], ecx
    mov dword ptr [r15 + 32], eax
    mov dword ptr [r15 + 36], r9d   /* Emulated stack pointer */
    mov dword ptr [r15 + 40], r10d  /* Emulated program counter */

    /* After the call, r14 will be the index::Val of the last executed block */
    mov rax, r14

    /* Save the flags */
    pushf ; .cfi_def_cfa_offset 60
    pop word ptr [r15 + 44] ; .cfi_def_cfa_offset 56

    /* Restore the Linux ABI caller-saved regs */
    pop r15 ; .cfi_def_cfa_offset 48
    pop r14 ; .cfi_def_cfa_offset 40
    pop r13 ; .cfi_def_cfa_offset 32
    pop r12 ; .cfi_def_cfa_offset 24
    pop rbp ; .cfi_def_cfa_offset 16
    pop rbx ; .cfi_def_cfa_offset 8

    ret
    .cfi_endproc
    ud2
