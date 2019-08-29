/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifdef __APPLE__
# define SYMBOL(x) _ ## x
#else
# define SYMBOL(x) x
#endif

    .file "granary/arch/x86/coverage.S"

    .intel_syntax noprefix
    .text

    .extern SYMBOL(gPathEntries)
    .extern SYMBOL(gNextPathEntry)
    .extern SYMBOL(UpdateCoverageSet)
    .extern SYMBOL(gInputIndex)

    // r15      os::Process32 *     Process32 object.
    // r14      index::Value        Meta-data about this block.
    // r13                          Scratch.
    // r12                          Scratch.
    // r11                          Scratch.
    // r10      Addr32              EIP.
    // r9       Addr32              ESP.
    // r8       Addr64              64-bit base of 32-bit address space.
    //
    // void CoverPath(void);


    // Add an entry to the path coverage list. If the list get full, then
    // call into UpdateCoverageSet to flush it and reset.
    .align 16
    .globl SYMBOL(CoverPath)
SYMBOL(CoverPath):
    .cfi_startproc
    pushfq

    // If we haven't read any input then it's not possible to have any
    // input-dependent code coverage.
    cmp qword ptr [RIP + SYMBOL(gInputIndex)], 0
    jz .Ldone

    mov r13d, dword ptr [RIP + SYMBOL(gNextPathEntry)]
    cmp r13, 4096 * 3 * 4
    jz .Lupdate_coverage_map

.Ladd_entry:
    lea r11, [RIP + SYMBOL(gPathEntries)]
    mov r12d, dword ptr [r15 + 52]  // Prev branch EIP

    mov dword ptr [r11 + r13 + 0], r12d  // Prev branch EIP.
    mov dword ptr [r11 + r13 + 4], r14d  // Block EIP
    mov dword ptr [r11 + r13 + 8], r10d  // Target EIP
    mov dword ptr [r11 + r13 + 12], 1  // Exec count

    // Move to the next path entry.
    add dword ptr [RIP + SYMBOL(gNextPathEntry)], 4 * 4

.Ldone:
    popfq
    ret

.Lupdate_coverage_map:
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

    push r15  // For alignment

    // push rsp
    // push [rsp]
    // and rsp, -16

    call    SYMBOL(UpdateCoverageSet)

    // pop rsp
    pop r15

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

    mov dword ptr [RIP + SYMBOL(gNextPathEntry)], 0
    xor r13, r13
    jmp .Ladd_entry

    .cfi_endproc
    ud2

