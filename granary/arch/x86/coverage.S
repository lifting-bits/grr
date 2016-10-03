/* Copyright 2015 Peter Goodman, all rights reserved. */

    .file "granary/arch/x86/coverage.S"

    .intel_syntax noprefix
    .text

    .extern gPathEntries
    .extern gNextPathEntry
    .extern UpdateCoverageSet
    .extern gInputIndex

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
    .globl CoverPath
    .type CoverPath, @function
CoverPath:
    .cfi_startproc
    pushf

    // If we haven't read any input then it's not possible to have any
    // input-dependent code coverage.
    cmp qword ptr [RIP + gInputIndex], 0
    jz done

    mov r13d, dword ptr [RIP + gNextPathEntry]
    cmp r13, 4096 * 3 * 4
    jz update_coverage_map

add_entry:
    lea r11, [RIP + gPathEntries]
    mov r12d, dword ptr [r15 + 52]  // Prev branch EIP

    mov dword ptr [r11 + r13 + 0], r12d  // Prev branch EIP.
    mov dword ptr [r11 + r13 + 4], r14d  // Block EIP
    mov dword ptr [r11 + r13 + 8], r10d  // Target EIP

    // Move to the next path entry.
    add dword ptr [RIP + gNextPathEntry], 3 * 4

done:
    popf
    ret

update_coverage_map:
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

    call    UpdateCoverageSet

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

    mov dword ptr [RIP + gNextPathEntry], 0
    xor r13, r13
    jmp add_entry

    .cfi_endproc
    .size   CoverPath, .-CoverPath
    ud2

