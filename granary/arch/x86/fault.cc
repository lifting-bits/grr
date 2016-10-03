/* Copyright 2016 Peter Goodman (peter@trailofbits.com), all rights reserved. */

#include "granary/arch/fault.h"
#include "granary/arch/x86/instruction.h"

#include "granary/os/process.h"

namespace granary {
namespace arch {
namespace {

static uint32_t RegToVal(const os::Process32 *process, xed_reg_enum_t reg) {
  switch (reg) {
    case XED_REG_RAX:
    case XED_REG_EAX:
      return process->regs.eax;
    case XED_REG_RCX:
    case XED_REG_ECX:
      return process->regs.ecx;
    case XED_REG_RDX:
    case XED_REG_EDX:
      return process->regs.edx;
    case XED_REG_RBX:
    case XED_REG_EBX:
      return process->regs.ebx;
    case XED_REG_RSP:
    case XED_REG_ESP:
      return process->regs.esp;
    case XED_REG_RBP:
    case XED_REG_EBP:
      return process->regs.ebp;
    case XED_REG_RSI:
    case XED_REG_ESI:
      return process->regs.esi;
    case XED_REG_RDI:
    case XED_REG_EDI:
      return process->regs.edi;
    default:
      return 0;
  }
}

}  // namespace

void DecomposeFaultAddr(
      const os::Process32 *process,
      Addr32 *base,
      Addr32 *index,
      Addr32 *scale,
      Addr32 *disp,
      Addr32 fault_addr) {

  *base = fault_addr;
  *index = 0;
  *scale = 0;
  *disp = 0;

  uint8_t bytes[kMaxNumInstructionBytes] = {0};
  size_t size = 0;
  for (auto i = 0U; i < kMaxNumInstructionBytes; ++i, ++size) {
    auto addr = process->ConvertAddress(process->regs.eip + i);
    auto pc = reinterpret_cast<const uint8_t *>(addr);
    if (!process->TryRead(pc, bytes[i])) {
      break;
    }
  }

  if (!size) {
    return;
  }

  Instruction instr;
  instr.TryDecode(bytes, size, ISA::x86);

  for (const auto &op : instr.operands) {
    if (XED_ENCODER_OPERAND_TYPE_MEM == op.type) {
      auto b = RegToVal(process, op.u.mem.base);
      auto i = RegToVal(process, op.u.mem.index);
      auto s = op.u.mem.scale;
      auto d = static_cast<uint32_t>(op.u.mem.disp.displacement);

      auto mem_addr = b + (i * s) + d;
      if (mem_addr == fault_addr) {
        *base = b;
        *index = i;
        *scale = s;
        *disp = d;
        return;
      }
    }
  }
}

}  // namespace arch
}  // namespace granary

