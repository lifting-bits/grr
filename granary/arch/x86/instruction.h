/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_X86_INSTRUCTION_H_
#define GRANARY_ARCH_X86_INSTRUCTION_H_

#include "granary/arch/isa.h"
#include "granary/arch/x86/xed-intel64.h"

#include "granary/base/base.h"

namespace granary {
namespace arch {

enum {
  // Maximum number of bytes in an x86/x64 instruction.
  kMaxNumInstructionBytes = 15,

  kAddrWidthBytes_x86 = 4,
  kAddrWidthBits_x86 = 32,
  kAddrWidthBytes_amd64 = 8,
  kAddrWidthBits_amd64 = 64
};

// Bit set of all general purpose registers.
union GPRSet {
  struct {
    bool r15:1;
    bool r14:1;
    bool r13:1;
    bool r12:1;
    bool r11:1;
    bool r10:1;
    bool r9:1;
    bool r8:1;
    bool gdi:1;
    bool gsi:1;
    bool gbp:1;
    bool gbx:1;
    bool gdx:1;
    bool gcx:1;
    bool gax:1;
    bool gsp:1;
  } __attribute__((packed));

  uint16_t bits;

  // Revives and returns the next dead register in the register set.
  xed_reg_enum_t ReviveNextDeadReg(void);
};

static_assert(sizeof(GPRSet) == sizeof(uint16_t),
              "Invalid structure packing of `union GPRSet`.");

// Represents an instruction.
struct Instruction final : public xed_encoder_instruction_t {
  inline bool IsFunctionCall(void) const {
    return XED_ICLASS_CALL_NEAR == iclass || XED_ICLASS_CALL_FAR == iclass;
  }

  inline bool IsFunctionReturn(void) const {
    return XED_ICLASS_RET_NEAR == iclass || XED_ICLASS_RET_FAR == iclass;
  }

  // TODO(pag): Add support for ABI-specific system calls (e.g. `int 0x80`).
  // TODO(pag): What about call gates?
  inline bool IsSystemCall(void) const {
    return XED_ICLASS_SYSCALL == iclass || XED_ICLASS_SYSCALL_AMD == iclass ||
           XED_ICLASS_SYSENTER == iclass;
  }

  inline bool IsSystemReturn(void) const {
    return XED_ICLASS_SYSRET == iclass || XED_ICLASS_SYSRET_AMD == iclass ||
           XED_ICLASS_SYSEXIT == iclass;
  }

  inline bool IsInterruptCall(void) const {
    return XED_ICLASS_INT <= iclass && XED_ICLASS_INTO >= iclass;
  }

  inline bool IsInterruptReturn(void) const {
    return XED_ICLASS_IRET <= iclass && XED_ICLASS_IRETQ >= iclass;
  }

  // This includes `JRCXZ`.
  inline bool IsBranch(void) const {
    return (XED_ICLASS_JB <= iclass && XED_ICLASS_JLE >= iclass) ||
           (XED_ICLASS_JNB <= iclass && XED_ICLASS_JZ >= iclass) ||
           (XED_ICLASS_LOOP <= iclass && XED_ICLASS_LOOPNE >= iclass) ||
           XED_ICLASS_XBEGIN == iclass;
  }

  inline bool IsJump(void) const {
    return XED_ICLASS_JMP == iclass || XED_ICLASS_JMP_FAR == iclass ||
           XED_ICLASS_XEND == iclass || XED_ICLASS_XABORT == iclass;
  }

  inline bool IsDirectFunctionCall(void) const {
    return XED_ICLASS_CALL_NEAR == iclass &&
           XED_ENCODER_OPERAND_TYPE_BRDISP == operands[0].type;
  }

  inline bool IsIndirectFunctionCall(void) const {
    return (XED_ICLASS_CALL_NEAR == iclass &&
            XED_ENCODER_OPERAND_TYPE_BRDISP != operands[0].type) ||
           XED_ICLASS_CALL_FAR == iclass;
  }

  inline bool IsDirectJump(void) const {
    return XED_ICLASS_JMP == iclass &&
           XED_ENCODER_OPERAND_TYPE_BRDISP == operands[0].type;
  }

  inline bool IsIndirectJump(void) const {
    return (XED_ICLASS_JMP == iclass &&
            XED_ENCODER_OPERAND_TYPE_BRDISP != operands[0].type) ||
           XED_ICLASS_JMP_FAR == iclass ||
           XED_ICLASS_XEND == iclass || XED_ICLASS_XABORT == iclass;
  }

  // Returns true if this instruction has a side-effect of serializing the
  // instruction pipeline. This class of instruction is significant for self-
  // and cross-modifying code.
  bool IsSerializing(void) const;

  // Is the instruction an undefined instruction?
  bool IsUndefined(void) const {
    return XED_ICLASS_INVALID == iclass || XED_ICLASS_UD2 == iclass;
  }

  // Target PC of a control-flow instruction.
  inline AppPC32 TargetPC(void) const {
    return EndPC() + operands[0].u.brdisp;
  }

  // Set the starting application PC of this instruction.
  inline void SetStartPC(AppPC32 pc) {
    decoded_pc = pc;
  }

  // Beginning program counter of this instruction.
  inline AppPC32 StartPC(void) const {
    return decoded_pc;
  }

  // Program counter of the next logical instruction after this instruction.
  inline AppPC32 EndPC(void) const {
    return decoded_pc + decoded_length;
  }

  // Size of this instruction in bytes.
  inline size_t NumBytes(void) const {
    return decoded_length;
  }

  // Try to decode an instruction starting at `decode_pc`.
  bool TryDecode(const uint8_t *decode_bytes, size_t max_num_bytes, ISA isa);

  // Computes the encoded length of the instruction.
  size_t NumEncodedBytes(void);

  // Actually encode an instruction. This assumes that `TryEncode` returned
  // `true`.
  void Encode(CachePC encode_pc);

  union {
    AppPC32 decoded_pc;
  };

  union {
    unsigned decoded_length;
    unsigned encoded_length;
  };

  // General purpose registers read and written by this instruction. We treat
  // conditional reads/writes as unconditional, and we treat writes that don't
  // overwrite the full register state as being reads as well.
  GPRSet gprs_read;
  GPRSet gprs_written;

  struct {
    // Are the bytes in `bytes` valid? If so, then we can just copy them. Things
    // that make them invalid would be modification, which usually happens for
    // PC-relative operands.
    bool is_valid:1;

    // Does this instruction have a PC-relative operand?
    bool has_pc_rel_op:1;

    // Does this instruction have a PC-relative operand that needs to be re-
    // encoded?
    bool reencode_pc_rel_op:1;

    // Does this instruction read/write to/from the arithmetic flags? We're
    // conservative and don't particularly care what flags are read/written.
    bool reads_aflags:1;
    bool writes_aflags:1;

    // Does this use "legacy" 32- or 16-bit registers? If so, then this
    // instruction likely cannot also use newer 64-bit registers (r8-r15).
    bool uses_legacy_registers:1;

    // Does this instruction read from or write to memory?
    bool reads_mem:1;
    bool writes_mem:1;

  } __attribute__((packed));

  // Encoded or decoded bytes.
  uint8_t bytes[kMaxNumInstructionBytes];
};

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_ARCH_X86_INSTRUCTION_H_
