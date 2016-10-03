/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_X86_ABI_H_
#define GRANARY_ARCH_X86_ABI_H_

// The 64-bit base address of a 32-bit process.
#define GRANARY_ABI_MEM64 XED_REG_R8

// The 32-bit address of the virtual thread's stack. This is a byte offset
// within `GRANARY_ABI_MEM`.
#define GRANARY_ABI_SP16 XED_REG_R9W
#define GRANARY_ABI_SP32 XED_REG_R9D
#define GRANARY_ABI_SP64 XED_REG_R9

// 32-bit program counter of the current / next block.
#define GRANARY_ABI_PC8 XED_REG_R10B
#define GRANARY_ABI_PC16 XED_REG_R10W
#define GRANARY_ABI_PC32 XED_REG_R10D
#define GRANARY_ABI_PC64 XED_REG_R10

// A scratch register for storing a computed value.
#define GRANARY_ABI_VAL8 XED_REG_R11B
#define GRANARY_ABI_VAL16 XED_REG_R11W
#define GRANARY_ABI_VAL32 XED_REG_R11D
#define GRANARY_ABI_VAL64 XED_REG_R11

// Scratch register for storing a computed address.
#define GRANARY_ABI_ADDR8 XED_REG_R12B
#define GRANARY_ABI_ADDR16 XED_REG_R12W
#define GRANARY_ABI_ADDR32 XED_REG_R12D
#define GRANARY_ABI_ADDR64 XED_REG_R12

// The process structure for saving/restoring things.
#define GRANARY_ABI_PROCESS64 XED_REG_R15

// The `index::Value` of the cached block being executed.
#define GRANARY_ABI_BLOCK64 XED_REG_R14
#define GRANARY_ABI_BLOCK32 XED_REG_R14D

#endif  // GRANARY_ARCH_X86_ABI_H_
