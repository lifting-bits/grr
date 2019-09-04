/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/os/process.h"

#include <setjmp.h>

#include "granary/os/snapshot.h"

#include "granary/code/cache.h"
#include "granary/code/execute.h"

namespace granary {
namespace os {
namespace {
static sigjmp_buf gSigRecoverState;
}  // namespace

// Tries to do a write of a specific size.
bool Process32::DoTryWrite(uint32_t *ptr, uint32_t val) const {
  PushProcess32 set_process(this);
  fault_can_recover = true;
#ifdef __APPLE__
  if (sigsetjmp(gSigRecoverState, true)) {
    *ptr = val;
  }
#else
  __asm__ __volatile__ (
    "jmp 1f;"
    ".align 16, 0x90;"
    "1:"
    "mov %0, (%1);"
    "jmp 2f;"
    ".align 16, 0x90;"
    "2:"
    :
    : "r"(val), "r"(ptr)
    : "memory"
  );
#endif
  auto ret = fault_can_recover;
  fault_can_recover = false;
  return ret;
}

bool Process32::DoTryWrite(uint16_t *ptr, uint16_t val) const {
  PushProcess32 set_process(this);
  fault_can_recover = true;
#ifdef __APPLE__
  if (sigsetjmp(gSigRecoverState, true)) {
    *ptr = val;
  }
#else
  __asm__ __volatile__ (
    "jmp 1f;"
    ".align 16, 0x90;"
    "1:"
    "movw %0, (%1);"
    "jmp 2f;"
    ".align 16, 0x90;"
    "2:"
    :
    : "r"(val), "r"(ptr)
    : "memory"
  );
#endif
  auto ret = fault_can_recover;
  fault_can_recover = false;
  return ret;
}

bool Process32::DoTryWrite(uint8_t *ptr, uint8_t val) const {
  PushProcess32 set_process(this);
  fault_can_recover = true;
#ifdef __APPLE__
  if (sigsetjmp(gSigRecoverState, true)) {
    *ptr = val;
  }
#else
  __asm__ __volatile__ (
    "jmp 1f;"
    ".align 16, 0x90;"
    "1:"
    "movb %0, (%1);"
    "jmp 2f;"
    ".align 16, 0x90;"
    "2:"
    :
    : "r"(val), "r"(ptr)
    : "memory"
  );
#endif
  auto ret = fault_can_recover;
  fault_can_recover = false;
  return ret;
}

// Tries to do a read of a specific size.
bool Process32::DoTryRead(const uint32_t *ptr, uint32_t *val) const {
  PushProcess32 set_process(this);
  fault_can_recover = true;
#ifdef __APPLE__
  if (sigsetjmp(gSigRecoverState, true)) {
    *val = *ptr;
  }
#else
  __asm__ __volatile__ (
    "jmp 1f;"
    ".align 16, 0x90;"
    "1:"
    "mov (%0), %%r12d;"
    "mov %%r12d, (%1);"
    "jmp 2f;"
    ".align 16, 0x90;"
    "2:"
    :
    : "r"(ptr), "r"(val)
    : "r12", "memory"
  );
#endif
  auto ret = fault_can_recover;
  fault_can_recover = false;
  return ret;
}

bool Process32::DoTryRead(const uint16_t *ptr, uint16_t *val) const {
  PushProcess32 set_process(this);
  fault_can_recover = true;
#ifdef __APPLE__
  if (sigsetjmp(gSigRecoverState, true)) {
    *val = *ptr;
  }
#else
  __asm__ __volatile__ (
    "jmp 1f;"
    ".align 16, 0x90;"
    "1:"
    "movw (%0), %%r12w;"
    "movw %%r12w, (%1);"
    "jmp 2f;"
    ".align 16, 0x90;"
    "2:"
    :
    : "r"(ptr), "r"(val)
    : "r12", "memory"
  );
#endif
  auto ret = fault_can_recover;
  fault_can_recover = false;
  return ret;
}

bool Process32::DoTryRead(const uint8_t *ptr, uint8_t *val) const {
  PushProcess32 set_process(this);
  fault_can_recover = true;
#ifdef __APPLE__
  if (sigsetjmp(gSigRecoverState, true)) {
    *val = *ptr;
  }
#else
  __asm__ __volatile__ (
    "jmp 1f;"
    ".align 16, 0x90;"
    "1:"
    "movb (%0), %%r12b;"
    "movb %%r12b, (%1);"
    "jmp 2f;"
    ".align 16, 0x90;"
    "2:"
    :
    : "r"(ptr), "r"(val)
    : "r12", "memory"
  );
#endif
  auto ret = fault_can_recover;
  fault_can_recover = false;
  return ret;
}

// Returns true if a signal handler can recover from this fault by returning.
bool Process32::RecoverFromTryReadWrite(ucontext_t *context) const {
  if (!fault_can_recover) {
    return false;
  }
  fault_can_recover = false;
#ifdef __APPLE__
  siglongjmp(gSigRecoverState, false);
#else
  auto &pc = context->uc_mcontext.gregs[REG_RIP];
  GRANARY_ASSERT(pc == (pc & ~15LL) && "Crash wasn't in a TryRead/TryWrite.");
  pc += 16LL;
  return true;
#endif
}

// Initialize the register state.
void Process32::InitRegs(const Snapshot32 *snapshot) {
  auto file = snapshot->file;

  regs.edi = static_cast<uint32_t>(file->meta.gregs.rdi);
  regs.esi = static_cast<uint32_t>(file->meta.gregs.rsi);
  regs.ebp = static_cast<uint32_t>(file->meta.gregs.rbp);
  regs.ebx = static_cast<uint32_t>(file->meta.gregs.rbx);
  regs.edx = static_cast<uint32_t>(file->meta.gregs.rdx);
  regs.ecx = static_cast<uint32_t>(file->meta.gregs.rcx);
  regs.eax = static_cast<uint32_t>(file->meta.gregs.rax);
  regs.esp = static_cast<uint32_t>(file->meta.gregs.rsp);
  regs.eip = static_cast<uint32_t>(file->meta.gregs.rip);
  regs.eflags = static_cast<uint32_t>(file->meta.gregs.eflags);

  memcpy(&fpregs, &(file->meta.fpregs), sizeof fpregs);
  last_branch_pc = 0;
}

void Process32::SynchronizeRegState(ucontext_t *context) {
#ifdef __APPLE__
  regs.edi = static_cast<uint32_t>(context->uc_mcontext->__ss.__rdi);
  regs.esi = static_cast<uint32_t>(context->uc_mcontext->__ss.__rsi);
  regs.ebp = static_cast<uint32_t>(context->uc_mcontext->__ss.__rbp);
  regs.ebx = static_cast<uint32_t>(context->uc_mcontext->__ss.__rbx);
  regs.edx = static_cast<uint32_t>(context->uc_mcontext->__ss.__rdx);
  regs.ecx = static_cast<uint32_t>(context->uc_mcontext->__ss.__rcx);
  regs.eax = static_cast<uint32_t>(context->uc_mcontext->__ss.__rax);
  regs.esp = static_cast<uint32_t>(context->uc_mcontext->__ss.__rsp);
  regs.eip = static_cast<uint32_t>(context->uc_mcontext->__ss.__rip);
  regs.eflags = static_cast<uint32_t>(context->uc_mcontext->__ss.__rflags);
#else
  regs.edi = static_cast<uint32_t>(context->uc_mcontext.gregs[REG_RDI]);
  regs.esi = static_cast<uint32_t>(context->uc_mcontext.gregs[REG_RSI]);
  regs.ebp = static_cast<uint32_t>(context->uc_mcontext.gregs[REG_RBP]);
  regs.ebx = static_cast<uint32_t>(context->uc_mcontext.gregs[REG_RBX]);
  regs.edx = static_cast<uint32_t>(context->uc_mcontext.gregs[REG_RDX]);
  regs.ecx = static_cast<uint32_t>(context->uc_mcontext.gregs[REG_RCX]);
  regs.eax = static_cast<uint32_t>(context->uc_mcontext.gregs[REG_RAX]);
  regs.esp = static_cast<uint32_t>(context->uc_mcontext.gregs[REG_R9]);
  regs.eip = static_cast<uint32_t>(context->uc_mcontext.gregs[REG_R10]);
  regs.eflags = static_cast<uint32_t>(context->uc_mcontext.gregs[REG_EFL]);
#endif
}

void Process32::RestoreFPUState(void) const {
  __asm__ __volatile__ (
      "fxrstor %0"
      :
      : "m"(fpregs)
      : "memory");
}

void Process32::SaveFPUState(void) {
  __asm__ __volatile__ (
      "fxsave %0"
      :
      : "m"(fpregs)
      : "memory");
}
}  // namespace os
}  // namespace granary
