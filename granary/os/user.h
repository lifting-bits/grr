/* Copyright 2019 Peter Goodman, all rights reserved. */

#ifndef GRR_GRANARY_OS_USER_H_
#define GRR_GRANARY_OS_USER_H_

#include <cstdint>
#include <sys/user.h>

#ifdef __APPLE__
struct user_fpregs_struct {
  uint16_t cwd;
  uint16_t swd;
  uint16_t ftw;
  uint16_t fop;
  uint64_t rip;
  uint64_t rdp;
  uint32_t mxcsr;
  uint32_t mxcr_mask;
  uint32_t st_space[32];
  uint32_t xmm_space[64];
  uint32_t padding[24];
};

struct user_regs_struct {
  uint64_t r15;
  uint64_t r14;
  uint64_t r13;
  uint64_t r12;
  uint64_t rbp;
  uint64_t rbx;
  uint64_t r11;
  uint64_t r10;
  uint64_t r9;
  uint64_t r8;
  uint64_t rax;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rsi;
  uint64_t rdi;
  uint64_t orig_rax;
  uint64_t rip;
  uint64_t cs;
  uint64_t eflags;
  uint64_t rsp;
  uint64_t ss;
  uint64_t fs_base;
  uint64_t gs_base;
  uint64_t ds;
  uint64_t es;
  uint64_t fs;
  uint64_t gs;
};

#endif

#endif //GRR_GRANARY_OS_USER_H_
