/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/arch/cpu.h"

#include "granary/arch/x86/abi.h"
#include "granary/arch/x86/patch.h"

#include "granary/code/block.h"
#include "granary/code/cache.h"

#include "granary/arch/instrument.h"

#ifndef GRANARY_REPORT_ENCODER_ERRORS
# define GRANARY_REPORT_ENCODER_ERRORS 0
#endif

#ifndef GRANARY_REPORT_EMULATE_RRORS
# define GRANARY_REPORT_EMULATE_RRORS 0
#endif

namespace granary {
namespace arch {
extern const xed_state_t kXEDState64;
}  // namespace arch
namespace {

struct PatchPoint {
  AppPC32 app_pc32;
  const arch::Instruction *cache_instr;
};

static PatchPoint gBranchTaken = {0, nullptr};
static PatchPoint gBranchNotTaken = {0, nullptr};

// Inject an instrumentation function call.
static void Instrument(Block *block, code::InstrumentationPoint ipoint) {
  auto func_pc = arch::GetInstrumentationFunction(ipoint);
  auto instr = block->cache_instructions.Add();
  instr->has_pc_rel_op = true;
  instr->reencode_pc_rel_op = true;
  xed_inst1(instr, arch::kXEDState64, XED_ICLASS_CALL_NEAR,
            arch::kAddrWidthBits_amd64, xed_relbr(0, 32));
  instr->operands[1].u.imm0 = reinterpret_cast<uintptr_t>(func_pc);
}

// Inject an instrumentation function call.
static void InstrumentPC(Block *block, Addr32 pc) {
  const auto &ids = code::GetInstrumentationIds(pc);
  if (ids.empty()) {
    return;
  }

  // This will end up applying the IDs in reverse order (desirable) because of
  // the way we build up basic blocks.
  for (auto id : ids) {
    Instrument(block, code::InstrumentationPoint::kInstrumentPC);
    auto instr = block->cache_instructions.Add();
    xed_inst2(instr, arch::kXEDState64, XED_ICLASS_MOV, 32,
              xed_reg(GRANARY_ABI_VAL32), xed_imm0(id, 32));
  }
}

// Re-encodes a relative branch operand.
void UpdateRelBranch(arch::Instruction *instr, CachePC encode_pc) {
  if (!instr->reencode_pc_rel_op) return;
  auto target = reinterpret_cast<CachePC>(instr->operands[1].u.imm0);
  auto next_pc = encode_pc + instr->encoded_length;
  GRANARY_ASSERT(32 == instr->operands[0].width);
  instr->operands[0] = xed_relbr(target - next_pc, 32);
  instr->is_valid = false;
}

// Resizes `reg` to the desired `size` in bits.
static xed_reg_enum_t ResizeReg(xed_reg_enum_t reg, size_t size) {
  enum {
    kReg64To32 = XED_REG_RAX - XED_REG_EAX,
    kReg64To16 = XED_REG_RAX - XED_REG_AX,
    kReg64To8 = XED_REG_AL - XED_REG_RAX
  };
  auto reg64 = xed_get_largest_enclosing_register(reg);
  switch (size) {
    case 8: return static_cast<xed_reg_enum_t>(reg64 + kReg64To8);
    case 16: return static_cast<xed_reg_enum_t>(reg64 - kReg64To16);
    case 32: return static_cast<xed_reg_enum_t>(reg64 - kReg64To32);
    case 64: return reg64;
    default:
      GRANARY_ASSERT(false && "Non-GPR cannot be resized.");
      return XED_REG_INVALID;
  }
}

// Loads the address computed by `op` into the `ADDR32` reg.
static void LoadEffectiveAddress(Block *block,
                                 const xed_encoder_operand_t &op) {
  auto instr = block->cache_instructions.Add();
  xed_inst2(instr, arch::kXEDState64, XED_ICLASS_LEA, arch::kAddrWidthBits_x86,
            xed_reg(GRANARY_ABI_ADDR32), op);
  instr->operands[1].u.mem.seg = XED_REG_INVALID;
}

// Returns `true` if this instruction has an effective address.
static bool IsEffectiveAddress(const arch::Instruction *app_instr) {
  return XED_ICLASS_BNDCL == app_instr->iclass ||
         XED_ICLASS_BNDCN == app_instr->iclass ||
         XED_ICLASS_BNDCU == app_instr->iclass ||
         XED_ICLASS_BNDMK == app_instr->iclass ||
         //XED_ICLASS_CLFLUSH == app_instr->iclass ||
         //XED_ICLASS_CLFLUSHOPT == app_instr->iclass ||
         XED_ICLASS_LEA == app_instr->iclass;
         //(XED_ICLASS_PREFETCHNTA <= app_instr->iclass &&
         // XED_ICLASS_PREFETCH_RESERVED >= app_instr->iclass);
}

// Relativizes a PC-relative memory operand.
static void Relativize(const arch::Instruction *app_instr,
                       xed_encoder_operand_t &op) {
  auto logical_eip = app_instr->EndPC();
  auto curr_disp = static_cast<int32_t>(op.u.mem.disp.displacement);
  auto pc_disp = static_cast<int32_t>(logical_eip);
  auto new_disp = pc_disp + curr_disp;
  GRANARY_ASSERT(0 <= new_disp);
  // TODO(pag): We assume user space addresses that are positive.

  op.u.mem.disp.displacement = static_cast<uint32_t>(new_disp);
  op.u.mem.disp.displacement_width = 32;

  if (IsEffectiveAddress(app_instr)) {
    op.u.mem.base = XED_REG_INVALID;
    op.u.mem.seg = XED_REG_INVALID;
  } else {
    op.u.mem.base = GRANARY_ABI_MEM64;
  }
}

// Virtualize a register to be the ABI version of the stack pointer.
static void VirtualizeStack(xed_reg_enum_t &reg) {
  if (XED_REG_ESP == reg) reg = GRANARY_ABI_SP32;
  if (XED_REG_SP == reg) reg = GRANARY_ABI_SP16;
}

// Rebase a memory operand to use the memory base register.
static void Rebase(Block *block, xed_encoder_operand_t &op) {
  GRANARY_ASSERT(32 >= op.u.mem.disp.displacement_width);
  auto ea = op;
  Instrument(block, code::InstrumentationPoint::kInstrumentMemoryAddress);
  VirtualizeStack(ea.u.mem.base);
  LoadEffectiveAddress(block, ea);

  op.u.mem.base = GRANARY_ABI_MEM64;
  op.u.mem.index = GRANARY_ABI_ADDR64;
  op.u.mem.scale = 1;
  op.u.mem.disp.displacement = 0;
  op.u.mem.disp.displacement_width = 0;
}

// Loads the register `src` into the register `reg`.
static void LoadReg(Block *block, xed_reg_enum_t dst, xed_reg_enum_t src);

// Resize a legacy register to be 8 bits wide.
static xed_reg_enum_t ResizeLegacy8(xed_reg_enum_t reg) {
  switch (reg) {
    case XED_REG_RAX: return XED_REG_AL;
    case XED_REG_RCX: return XED_REG_CL;
    case XED_REG_RDX: return XED_REG_DL;
    case XED_REG_RBX: return XED_REG_BL;
    default:
      GRANARY_ASSERT(false && "Could not resize non-legacy register.");
      return XED_REG_INVALID;
  }
}

// Returns whether or not an operand is a destination operand. This is quite a
// hack :-P
static bool OpIsWrite(const xed_encoder_operand_t &op) {
  return XED_ENCODER_OPERAND_TYPE_INVALID != (&op)[1].type;
}

// Virtualize a memory operand.
static void VirtualizeMem(Block *block, const arch::Instruction *app_instr,
                          xed_encoder_operand_t &op) {
  if (!IsEffectiveAddress(app_instr)) {
    Rebase(block, op);

  // We don't want to rebase effective addresses with `GRANARY_ABI_MEM64`.
  } else {
    VirtualizeStack(op.u.mem.base);
  }
}

// Virtualize a memory operand that is being used in an instruction where a
// legacy reg (AH, CH, BH, DH) in the instruction.
static void VirtualizeLegacyMem(Block *block,
                                const arch::Instruction *app_instr,
                                xed_encoder_operand_t &op,
                                xed_reg_enum_t stolen_reg64) {
  // We have something like `MOV MEM, legacy_reg8`; convert into:
  //
  //    LEA stolen_reg64, MEM
  //    MOV [stolen_reg64], legacy_reg8
  if (OpIsWrite(op)) {
    auto instr = block->cache_instructions.Add();
    xed_inst2(instr, arch::kXEDState64, XED_ICLASS_LEA,
              arch::kAddrWidthBits_amd64, xed_reg(stolen_reg64), op);
    instr->operands[1].u.mem.seg = XED_REG_INVALID;
    VirtualizeMem(block, app_instr, instr->operands[1]);

    op.u.mem.base = stolen_reg64;
    op.u.mem.index = XED_REG_INVALID;
    op.u.mem.scale = 0;
    op.u.mem.seg = XED_REG_INVALID;
    op.u.mem.disp.displacement = 0;
    op.u.mem.disp.displacement_width = 0;

  // We have something like `MOV legacy_reg8, MEM`; convert into:
  //
  //    MOV val8, MEM
  //    MOV stolen_reg64, val64
  //    MOV legacy_reg8, stolen_reg8
  } else {
    LoadReg(block, stolen_reg64, GRANARY_ABI_VAL64);

    auto instr = block->cache_instructions.Add();
    xed_inst2(instr, arch::kXEDState64, XED_ICLASS_MOV, 8,
              xed_reg(GRANARY_ABI_VAL8), op);
    VirtualizeMem(block, app_instr, instr->operands[1]);

    op.type = XED_ENCODER_OPERAND_TYPE_REG;
    op.u.reg = ResizeLegacy8(stolen_reg64);
  }
}

// Virtualizes a register or memory operand.
//
// Note: `op` is from the *copy* of `app_instr` that has already been enstackd.
static void Virtualize(Block *block, const arch::Instruction *app_instr,
                       xed_encoder_operand_t &op,
                       xed_reg_enum_t stolen_reg=XED_REG_INVALID) {
  switch (op.type) {
    case XED_ENCODER_OPERAND_TYPE_REG:
      VirtualizeStack(op.u.reg);
      break;

    case XED_ENCODER_OPERAND_TYPE_MEM:
      if (app_instr) {
        if (XED_REG_EIP == op.u.mem.base) {
          GRANARY_ASSERT(!op.u.mem.index);
          Relativize(app_instr, op);
        } else if (app_instr->uses_legacy_registers) {
          VirtualizeLegacyMem(block, app_instr, op, stolen_reg);
        } else {
          VirtualizeMem(block, app_instr, op);
        }
      } else {
        Rebase(block, op);
      }
      break;
    default:
      break;
  }
}

// Loads a value into the value register.
static void LoadValue(Block *block, const arch::Instruction *app_instr,
                      const xed_encoder_operand_t &op) {
  auto instr = block->cache_instructions.Add();
  auto size = op.width;
  xed_inst2(instr, arch::kXEDState64, XED_ICLASS_MOV, size,
            xed_reg(ResizeReg(GRANARY_ABI_VAL64, size)), op);
  Virtualize(block, app_instr, instr->operands[1]);
}

// Sign- or zero-extends the ABI value register.
static void ExtendValue(Block *block, xed_iclass_enum_t extender,
                        size_t from_size, size_t to_size) {
  if (from_size < to_size) {
    auto val_reg = ResizeReg(GRANARY_ABI_VAL64, from_size);
    auto val_reg_ext = ResizeReg(GRANARY_ABI_VAL64, to_size);
    auto instr = block->cache_instructions.Add();
    xed_inst2(instr, arch::kXEDState64, extender, to_size,
              xed_reg(val_reg_ext), xed_reg(val_reg));
  }
}

// Loads a sign-extended value into the ABI value register.
static void LoadSignedValue(Block *block, const arch::Instruction *app_instr,
                            const xed_encoder_operand_t &op) {
  ExtendValue(block, XED_ICLASS_MOVSX, op.width,
              app_instr->effective_operand_width);
  LoadValue(block, app_instr, op);
}

// Ends a block.
static void EndBlock(Block *block) {
  auto instr = block->cache_instructions.Add();
  xed_inst0(instr, arch::kXEDState64, XED_ICLASS_RET_NEAR,
            arch::kAddrWidthBits_amd64);
}

// Save a register's value.
static void SaveReg(Block *block, xed_reg_enum_t reg) {
  auto instr = block->cache_instructions.Add();
  xed_inst1(instr, arch::kXEDState64, XED_ICLASS_PUSH,
            arch::kAddrWidthBits_amd64,
            xed_reg(xed_get_largest_enclosing_register(reg)));
}

// Restore a register's value.
static void RestoreReg(Block *block, xed_reg_enum_t reg) {
  auto instr = block->cache_instructions.Add();
  xed_inst1(instr, arch::kXEDState64, XED_ICLASS_POP,
            arch::kAddrWidthBits_amd64,
            xed_reg(xed_get_largest_enclosing_register(reg)));
}

// Loads the immediate constant into the register `reg`.
static void LoadImm(Block *block, xed_reg_enum_t reg, uint64_t imm) {
  auto instr = block->cache_instructions.Add();
  auto imm_size = xed_get_register_width_bits64(reg);
  xed_inst2(instr, arch::kXEDState64, XED_ICLASS_MOV, imm_size,
            xed_reg(reg), xed_imm0(imm, imm_size));
}

// Loads the register `src` into the register `dst`.
static void LoadReg(Block *block, xed_reg_enum_t dst, xed_reg_enum_t src) {
  auto instr = block->cache_instructions.Add();
  auto reg_size = xed_get_register_width_bits64(dst);
  xed_inst2(instr, arch::kXEDState64, XED_ICLASS_MOV, reg_size,
            xed_reg(dst), xed_reg(src));
}

// Loads the register `src` with the value of the virtual memory location
// addressed by `src_addr`.
static void LoadMem(Block *block, xed_reg_enum_t dst,
                    xed_reg_enum_t src_addr) {
  auto instr = block->cache_instructions.Add();
  auto size = xed_get_register_width_bits64(dst);
  xed_inst2(instr, arch::kXEDState64, XED_ICLASS_MOV, arch::kAddrWidthBits_x86,
            xed_reg(dst),
            xed_mem_bisd(GRANARY_ABI_MEM64, src_addr, 1, xed_disp(0, 8), size));
}

// Loads the value of the operand `op` into the register `dst`. This will
// auto-virtualize the operand. This assumes that the size of `reg` is
// compatible with the size of `op`.
static void LoadOp(Block *block, const arch::Instruction *app_instr,
                   xed_reg_enum_t dst, const xed_encoder_operand_t &op) {
  auto instr = block->cache_instructions.Add();
  xed_inst2(instr, arch::kXEDState64, XED_ICLASS_MOV, arch::kAddrWidthBits_x86,
            xed_reg(dst), op);
  Virtualize(block, app_instr, instr->operands[1]);
}

// Write an immediate integer to the virtual memory location `addr`.
static void StoreImm32(Block *block, xed_reg_enum_t addr, uint32_t val) {
  auto instr = block->cache_instructions.Add();
  xed_inst2(instr, arch::kXEDState64, XED_ICLASS_MOV, arch::kAddrWidthBits_x86,
            xed_mem_bisd(GRANARY_ABI_MEM64, addr, 1,
                         xed_disp(0, 8), arch::kAddrWidthBits_x86),
            xed_imm0(val, 32));
}

// Write an immediate integer to the virtual memory location `addr`.
static void StoreReg(Block *block, xed_reg_enum_t addr, xed_reg_enum_t reg) {
  auto instr = block->cache_instructions.Add();
  auto width = xed_get_register_width_bits64(reg);
  xed_inst2(instr, arch::kXEDState64, XED_ICLASS_MOV, width,
            xed_mem_bisd(GRANARY_ABI_MEM64, addr, 1,
                         xed_disp(0, 8), width),
            xed_reg(reg));
}

// Write the value of a register into an operand. This will virtualize the
// operand if necessary.
static void StoreOp(Block *block, const arch::Instruction *app_instr,
                    const xed_encoder_operand_t &op, xed_reg_enum_t reg) {
  auto instr = block->cache_instructions.Add();
  auto width = xed_get_register_width_bits64(reg);
  xed_inst2(instr, arch::kXEDState64, XED_ICLASS_MOV, width,
            op, xed_reg(reg));
  Virtualize(block, app_instr, instr->operands[0]);
}

// Performs a small constant shift of the virtual stack pointer.
static void BumpSP(Block *block, intptr_t shift_) {
  GRANARY_ASSERT(0 != shift_);
  auto instr = block->cache_instructions.Add();
  auto shift = static_cast<uintptr_t>(shift_);
  xed_inst2(instr, arch::kXEDState64, XED_ICLASS_LEA, arch::kAddrWidthBits_x86,
            xed_reg(GRANARY_ABI_SP32),
            xed_mem_bd(GRANARY_ABI_SP32, xed_disp(shift, 8),
                       arch::kAddrWidthBits_x86));
}

// Pushes a register onto the stack.
static void PushReg(Block *block, xed_reg_enum_t reg) {
  auto reg_size = xed_get_register_width_bits64(reg);
  StoreReg(block, GRANARY_ABI_SP64, reg);
  BumpSP(block, -(reg_size / 8));
}

// Pops the value on the stack into the register `reg`.
static void PopReg(Block *block, xed_reg_enum_t reg) {
  auto reg_size = xed_get_register_width_bits64(reg);
  auto stack_op = xed_mem_b(XED_REG_ESP, reg_size);
  BumpSP(block, reg_size / 8);
  LoadOp(block, nullptr, reg, stack_op);
}

// Record the EIP of block containing the last multi-way branch.
static void RecordLastMultiWayBranch(Block *block) {
  static_assert(52 == offsetof(os::Process32, last_branch_pc),
                "Bad structure packing of `os::Process32`.");
  auto update_last_cfi = block->cache_instructions.Add();
  xed_inst2(update_last_cfi, arch::kXEDState64, XED_ICLASS_MOV,
            arch::kAddrWidthBits_x86,
            xed_mem_bd(GRANARY_ABI_PROCESS64,
                       // offsetof(os::Process32, last_branch_pc)
                       xed_disp(52, 8),
                       arch::kAddrWidthBits_x86),
            xed_reg(GRANARY_ABI_BLOCK32));
}

static const arch::Instruction *PatchableJump(Block *block) {
  auto patch = block->cache_instructions.Add();
  xed_inst1(patch, arch::kXEDState64, XED_ICLASS_JMP,
            arch::kAddrWidthBits_amd64, xed_relbr(0, 32));
  return patch;
}

// Virtualizes an application branch instruction.
static void VirtualizeBranch(Block *block, const arch::Instruction *cfi) {
  gBranchTaken.app_pc32 = cfi->TargetPC();
  gBranchTaken.cache_instr = PatchableJump(block);

  RecordLastMultiWayBranch(block);
  Instrument(block, code::InstrumentationPoint::kInstrumentMultiWayBranch);
  LoadImm(block, GRANARY_ABI_PC32, cfi->TargetPC());

  // If the condition isn't taken then we fall-through and return to here.
  EndBlock(block);

  gBranchNotTaken.app_pc32 = cfi->EndPC();
  gBranchNotTaken.cache_instr = PatchableJump(block);

  RecordLastMultiWayBranch(block);
  Instrument(block, code::InstrumentationPoint::kInstrumentMultiWayBranch);

  auto cond = block->cache_instructions.Add();
  memcpy(cond, cfi, sizeof *cond);
  cond->mode = arch::kXEDState64;
  cond->effective_operand_width = arch::kAddrWidthBits_amd64;
  cond->effective_address_width = arch::kAddrWidthBits_amd64;
  cond->operands[0].width = 8;

  // Jump around the:
  //    1)  Patchable `jmp`. Initially points at (5 bytes).
  //    2)  `call` into the instrumentation point (5 bytes).
  //    3)  `mov` of the block `eip` into the Process' last branch pc (4 bytes).
  //    4)  `ret` back to the dispatcher (1 byte).
  cond->operands[0].u.brdisp = 15;

  LoadImm(block, GRANARY_ABI_PC32, cfi->EndPC());  // 6 bytes.
}

// Emulates a direct jump.
static bool EmulateDirectJump(Block *block, const arch::Instruction *cfi) {
  LoadImm(block, GRANARY_ABI_PC32, cfi->TargetPC());
  return true;
}

// Emulates an indirect jump.
static bool EmulateIndirectJump(Block *block, const arch::Instruction *cfi) {
  RecordLastMultiWayBranch(block);
  Instrument(block, code::InstrumentationPoint::kInstrumentMultiWayBranch);
  LoadOp(block, cfi, GRANARY_ABI_PC32, cfi->operands[0]);
  return true;
}

// Emulates a direct function call.
static bool EmulateDirectFunctionCall(Block *block,
                                      const arch::Instruction *cfi) {
  LoadImm(block, GRANARY_ABI_PC32, cfi->TargetPC());
  StoreImm32(block, GRANARY_ABI_SP64, cfi->EndPC());
  BumpSP(block, -arch::kAddrWidthBytes_x86);
  return true;
}

// Emulates an indirect function call.
static bool EmulateIndirectFunctionCall(Block *block,
                                        const arch::Instruction *cfi) {
  RecordLastMultiWayBranch(block);
  Instrument(block, code::InstrumentationPoint::kInstrumentMultiWayBranch);
  StoreImm32(block, GRANARY_ABI_SP64, cfi->EndPC());
  BumpSP(block, -arch::kAddrWidthBytes_x86);
  LoadOp(block, cfi, GRANARY_ABI_PC32, cfi->operands[0]);
  return true;
}

// Emulates a function return.
static bool EmulateFunctionReturn(Block *block, const arch::Instruction *cfi) {
  const auto &imm = cfi->operands[0];
  if (XED_ENCODER_OPERAND_TYPE_IMM0 == imm.type) {
    BumpSP(block, imm.u.imm0);
  }
  BumpSP(block, arch::kAddrWidthBytes_x86);
  RecordLastMultiWayBranch(block);
  Instrument(block, code::InstrumentationPoint::kInstrumentMultiWayBranch);
  LoadMem(block, GRANARY_ABI_PC32, GRANARY_ABI_SP64);
  return true;
}

// Emulates a JCXZ, which is not valid in amd64.
static void EmulateJcxz(Block *block, const arch::Instruction *cfi) {
  RecordLastMultiWayBranch(block);
  Instrument(block, code::InstrumentationPoint::kInstrumentMultiWayBranch);
  LoadReg(block, XED_REG_ECX, GRANARY_ABI_VAL32);
  LoadImm(block, GRANARY_ABI_PC32, cfi->TargetPC());

  // If the condition isn't taken then we fall-through and return to here.
  EndBlock(block);
  LoadReg(block, XED_REG_ECX, GRANARY_ABI_VAL32);

  RecordLastMultiWayBranch(block);
  Instrument(block, code::InstrumentationPoint::kInstrumentMultiWayBranch);

  // Jump around the `MOV ECX, VAL32; RET` and the instrumentation.
  auto cond = block->cache_instructions.Add();
  memcpy(cond, cfi, sizeof *cond);
  cond->mode = arch::kXEDState64;
  cond->iclass = XED_ICLASS_JECXZ;
  cond->operands[0].u.brdisp = 3 + 1 + 9;

  LoadImm(block, GRANARY_ABI_PC32, cfi->EndPC());

  ExtendValue(block, XED_ICLASS_MOVZX, XED_REG_ECX, XED_REG_CX);
  LoadReg(block, GRANARY_ABI_VAL32, XED_REG_ECX);
}

// Emulates a conditional jump/branch instruction.
static bool EmulateBranch(Block *block, const arch::Instruction *cfi) {
  if (XED_ICLASS_JCXZ == cfi->iclass) {
    EmulateJcxz(block, cfi);
  } else {
    VirtualizeBranch(block, cfi);
  }
  return true;
}

// Add a padding byte before a block.
static void AddPadding(Block *block) {
  auto instr = block->cache_instructions.Add();
  xed_inst0(instr, arch::kXEDState64, XED_ICLASS_INT3, 0);
}

// Convert an instruction into a UD2.
static void ConvertToError(Block *block, arch::Instruction *instr) {
  xed_inst0(instr, arch::kXEDState64, XED_ICLASS_UD2, 0);
  instr->is_valid = false;
  block->has_error = true;
}

// Adds an error instruction (UD2).
static void AddError(Block *block) {
  ConvertToError(block, block->cache_instructions.Add());
}

// Emulates an interrupt call. This actually sets up the fall-through PC as a
// tagged PC (where the high bit is 1 to mark it as a syscall) so that the
// syscall can be handled by the interpreter and so that we don't need to
// inject anything into the code cache that can't be relocated.
static bool EmulateInterruptCall(Block *block, const arch::Instruction *cfi) {
  if (XED_ICLASS_INT == cfi->iclass && 0x80 == cfi->operands[0].u.imm0) {
    block->has_syscall = true;
    LoadImm(block, GRANARY_ABI_PC32, cfi->EndPC());
  } else {
    // E.g. INT1, INT3, INTO, INT imm8 != 0x80.
    AddError(block);
  }
  return true;
}

// Forward declaration.
static bool EmulatePopFlags(Block *block, xed_reg_enum_t rflags);

// Emulates an interrupt return.
static bool EmulateInterruptReturn(Block *block, const arch::Instruction *cfi) {
  GRANARY_ASSERT(XED_ICLASS_IRETD == cfi->iclass);
  EmulatePopFlags(block, GRANARY_ABI_VAL32);
  PopReg(block, XED_REG_CS);
  PopReg(block, GRANARY_ABI_PC32);
  GRANARY_UNUSED(cfi);
  return true;
}

// Returns the set of available legacy registers as dead registers in the
// returned GPRSet.
static xed_reg_enum_t UnusedLegacyReg(const arch::Instruction *app_instr) {
  arch::GPRSet used_regs;
  used_regs.bits = app_instr->gprs_read.bits | app_instr->gprs_written.bits;
  if (!used_regs.gax) return XED_REG_RAX;
  if (!used_regs.gcx) return XED_REG_RCX;
  if (!used_regs.gbx) return XED_REG_RBX;
  if (!used_regs.gdx) return XED_REG_RDX;
  GRANARY_ASSERT(false && "No live legacy regs available.");
  return XED_REG_INVALID;
}

// Add an instruction `instr` to `block`s encode stack.
static void Virtualize(Block *block, const arch::Instruction *app_instr) {
  xed_reg_enum_t stolen_reg = XED_REG_INVALID;
  if (app_instr->uses_legacy_registers &&
      (app_instr->reads_mem || app_instr->writes_mem)) {
    stolen_reg = UnusedLegacyReg(app_instr);
    GRANARY_ASSERT(XED_REG_INVALID != stolen_reg);
    RestoreReg(block, stolen_reg);
  }

  auto instr = block->cache_instructions.Add();
  memcpy(instr, app_instr, sizeof *instr);
  instr->mode = arch::kXEDState64;
  instr->effective_address_width = arch::kAddrWidthBits_amd64;

  for (auto &op : instr->operands) {
    if (XED_ENCODER_OPERAND_TYPE_INVALID == op.type) break;
    Virtualize(block, app_instr, op, stolen_reg);
  }

  if (XED_REG_INVALID != stolen_reg) {
    SaveReg(block, stolen_reg);
  }
}

// Emulate a `SYSCALL` or `SYSENTER` instruction.
static bool EmulateSyscall(Block *block, const arch::Instruction *app_instr) {
  GRANARY_UNUSED(block);
  GRANARY_UNUSED(app_instr);
#if GRANARY_REPORT_EMULATE_RRORS
  GRANARY_ASSERT(false && "Unexpected SYSCALL or SYSENTER instruction.");
#else
  AddError(block);
#endif
  return true;
}

// Emulate a `SYSRET` or `SYSEXIT` instruction.
static bool EmulateSysret(Block *block, const arch::Instruction *app_instr) {
  GRANARY_UNUSED(block);
  GRANARY_UNUSED(app_instr);
#if GRANARY_REPORT_EMULATE_RRORS
  GRANARY_ASSERT(false && "Unexpected SYSRET or SYSEXIT instruction.");
#else
  AddError(block);
#endif
  return true;
}

// Adds a control-flow instruction into the stack. Returns `true` if `cfi`
// is actually a control-flow instruction and has been fully emulated.
static bool EmulateCFI(Block *block, arch::Instruction *cfi) {
  if (cfi->IsDirectJump()) {
    return EmulateDirectJump(block, cfi);
  } else if (cfi->IsIndirectJump()) {
    return EmulateIndirectJump(block, cfi);
  } else if (cfi->IsDirectFunctionCall()) {
    return EmulateDirectFunctionCall(block, cfi);
  } else if (cfi->IsIndirectFunctionCall()) {
    return EmulateIndirectFunctionCall(block, cfi);
  } else if (cfi->IsFunctionReturn()) {
    return EmulateFunctionReturn(block, cfi);
  } else if (cfi->IsBranch()) {
    return EmulateBranch(block, cfi);
  } else if (cfi->IsSystemCall()) {
    block->has_syscall = true;
    return EmulateSyscall(block, cfi);
  } else if (cfi->IsSystemReturn()) {
    return EmulateSysret(block, cfi);
  } else if (cfi->IsInterruptCall()) {
    return EmulateInterruptCall(block, cfi);
  } else if (cfi->IsInterruptReturn()) {
    return EmulateInterruptReturn(block, cfi);

  // Not a control-flow instruction, need to add a fall-through.
  } else {
    LoadImm(block, GRANARY_ABI_PC32, cfi->EndPC());
    return false;
  }
}

// Emulates a push instruction.
static bool EmulatePush(Block *block, const arch::Instruction *app_instr) {
  const auto op_size = app_instr->effective_operand_width;
  const auto &op = app_instr->operands[0];
  PushReg(block, ResizeReg(GRANARY_ABI_VAL64, op_size));
  LoadSignedValue(block, app_instr, op);
  return true;
}

// Emulates a pop instruction.
static bool EmulatePop(Block *block, const arch::Instruction *app_instr) {
  auto op_size = app_instr->effective_operand_width;
  auto stack_op = xed_mem_b(XED_REG_ESP, op_size);
  auto val = ResizeReg(GRANARY_ABI_VAL64, op_size);
  StoreOp(block, app_instr, app_instr->operands[0], val);
  BumpSP(block, op_size / 8);
  LoadSignedValue(block, app_instr, stack_op);
  return true;
}

// Emulates a push-all instruction.
static bool EmulatePusha(Block *block) {
  PushReg(block, XED_REG_DI);
  PushReg(block, XED_REG_SI);
  PushReg(block, XED_REG_BP);
  PushReg(block, GRANARY_ABI_VAL16);
  PushReg(block, XED_REG_BX);
  PushReg(block, XED_REG_DX);
  PushReg(block, XED_REG_CX);
  PushReg(block, XED_REG_AX);
  LoadReg(block, GRANARY_ABI_VAL16, GRANARY_ABI_SP16);
  return true;
}

// Emulates a pop-all instruction.
static bool EmulatePopa(Block *block) {
  PopReg(block, XED_REG_AX);
  PopReg(block, XED_REG_CX);
  PopReg(block, XED_REG_DX);
  PopReg(block, XED_REG_BX);
  BumpSP(block, 2);
  PopReg(block, XED_REG_BP);
  PopReg(block, XED_REG_SI);
  PopReg(block, XED_REG_DI);
  return true;
}

// Emulates a push-all doublewords instruction.
static bool EmulatePushad(Block *block) {
  PushReg(block, XED_REG_EDI);
  PushReg(block, XED_REG_ESI);
  PushReg(block, XED_REG_EBP);
  PushReg(block, GRANARY_ABI_VAL32);
  PushReg(block, XED_REG_EBX);
  PushReg(block, XED_REG_EDX);
  PushReg(block, XED_REG_ECX);
  PushReg(block, XED_REG_EAX);
  LoadReg(block, GRANARY_ABI_VAL32, GRANARY_ABI_SP32);
  return true;
}

// Emulates a pop-all doublewords instruction.
static bool EmulatePopad(Block *block) {
  PopReg(block, XED_REG_EAX);
  PopReg(block, XED_REG_ECX);
  PopReg(block, XED_REG_EDX);
  PopReg(block, XED_REG_EBX);
  BumpSP(block, 4);
  PopReg(block, XED_REG_EBP);
  PopReg(block, XED_REG_ESI);
  PopReg(block, XED_REG_EDI);
  return true;
}

// Emulates pushing some portion of the `RFLAGS` onto the stack.
static bool EmulatePushFlags(Block *block, xed_reg_enum_t rflags) {
  PushReg(block, rflags);
  auto pop_val = block->cache_instructions.Add();
  xed_inst1(pop_val, arch::kXEDState64, XED_ICLASS_POP,
            arch::kAddrWidthBits_amd64, xed_reg(GRANARY_ABI_VAL64));

  auto pushfq = block->cache_instructions.Add();
  xed_inst0(pushfq, arch::kXEDState64, XED_ICLASS_PUSHFQ,
            arch::kAddrWidthBits_amd64);
  return true;
}

// Emulates popping the top of the stack into some portion of `RFLAGS`.
static bool EmulatePopFlags(Block *block, xed_reg_enum_t rflags) {
  auto rflags_width = xed_get_register_width_bits64(rflags);

  auto popfq = block->cache_instructions.Add();
  xed_inst0(popfq, arch::kXEDState64, XED_ICLASS_POPFQ,
            arch::kAddrWidthBits_amd64);

  auto overwrite = block->cache_instructions.Add();
  xed_inst2(overwrite, arch::kXEDState64, XED_ICLASS_MOV, rflags_width,
            xed_mem_b(XED_REG_RSP, rflags_width), xed_reg(rflags));

  // Mask off `ID`, which permits the identification of whether or not
  // the `CPUID` instruction is supported.
  if (32 == rflags_width) {
    auto mask = block->cache_instructions.Add();
    auto rflags_reg64 = xed_get_largest_enclosing_register(rflags);
    xed_inst2(mask, arch::kXEDState64, XED_ICLASS_AND, 64,
              xed_reg(rflags_reg64), xed_imm0(0xffdfffff, 32));
  }

  auto pushfq = block->cache_instructions.Add();
  xed_inst0(pushfq, arch::kXEDState64, XED_ICLASS_PUSHFQ,
            arch::kAddrWidthBits_amd64);

  PopReg(block, rflags);
  return true;
}

// Emulates the `ENTER` instruction.
static bool EmulateEnter(Block *block, const arch::Instruction *app_instr) {
  GRANARY_UNUSED(block);
  GRANARY_UNUSED(app_instr);
#if GRANARY_REPORT_EMULATE_RRORS
  GRANARY_ASSERT("TODO: ENTER" && false);  // TODO(pag): Implement me.
#else
  AddError(block);
#endif
  return true;
}

// Emulates the `LEAVE` instruction.
static bool EmulateLeave(Block *block, const arch::Instruction *app_instr) {
  auto reg_width = app_instr->effective_operand_width;
  auto base_pointer = ResizeReg(XED_REG_EBP, reg_width);
  auto stack_pointer = ResizeReg(GRANARY_ABI_SP64, reg_width);
  PopReg(block, base_pointer);
  LoadReg(block, stack_pointer, base_pointer);
  return true;
}

// Emulates the `BOUND` instruction.
static bool EmulateBound(Block *block, const arch::Instruction *app_instr) {
  GRANARY_UNUSED(block);
  GRANARY_UNUSED(app_instr);
#if GRANARY_REPORT_EMULATE_RRORS
  GRANARY_ASSERT("TODO: BOUND" && false);  // TODO(pag): Implement me.
#else
  AddError(block);
#endif
  return true;
}

// Emulates the `XLAT` instruction.
static bool EmulateXLAT(Block *block, const arch::Instruction *app_instr) {
  GRANARY_UNUSED(block);
  GRANARY_UNUSED(app_instr);
#if GRANARY_REPORT_EMULATE_RRORS
  GRANARY_ASSERT("TODO: XLAT" && false);  // TODO(pag): Implement me.
#else
  AddError(block);
#endif
  return true;
}

// Emulates a string operation. It does this by widening RDI/RSI with the base
// of memory address, then it shorts them back again by adding the two's
// complement of the
static bool EmulateStringOp(Block *block, const arch::Instruction *app_instr) {
  // Shorten RDI and RSI back to 32-bit addresses by adding the complemented
  // 32-bit memory base and 1 (i.e. two's complement negation of the 32-bit
  // memory base).
  auto shorten_rdi = block->cache_instructions.Add();
  xed_inst2(shorten_rdi, arch::kXEDState64, XED_ICLASS_LEA,
            arch::kAddrWidthBits_amd64, xed_reg(XED_REG_RDI),
            xed_mem_gbisd(XED_REG_INVALID, XED_REG_RDI, GRANARY_ABI_VAL64, 1,
                          xed_disp(1, 8), arch::kAddrWidthBits_amd64));

  auto shorten_rsi = block->cache_instructions.Add();
  xed_inst2(shorten_rsi, arch::kXEDState64, XED_ICLASS_LEA,
            arch::kAddrWidthBits_amd64, xed_reg(XED_REG_RSI),
            xed_mem_gbisd(XED_REG_INVALID, XED_REG_RSI, GRANARY_ABI_VAL64, 1,
                          xed_disp(1, 8), arch::kAddrWidthBits_amd64));

  // Complement the base of memory.
  auto not_ = block->cache_instructions.Add();
  xed_inst1(not_, arch::kXEDState64, XED_ICLASS_NOT,
            arch::kAddrWidthBits_amd64, xed_reg(GRANARY_ABI_VAL64));
  LoadReg(block, GRANARY_ABI_VAL64, GRANARY_ABI_MEM64);

  // Virtualize the original instruction, which will make the effective address
  // width of the implicit EDI/ESI operands into RDI/RSI.
  Virtualize(block, app_instr);

  // Widen RSI and RDI by the base of the address space.
  auto widen_rsi = block->cache_instructions.Add();
  xed_inst2(widen_rsi, arch::kXEDState64, XED_ICLASS_LEA,
            arch::kAddrWidthBits_amd64, xed_reg(XED_REG_RSI),
            xed_mem_gbisd(XED_REG_INVALID, XED_REG_RSI, GRANARY_ABI_MEM64, 1,
                          xed_disp(0, 0), arch::kAddrWidthBits_amd64));

  auto widen_rdi = block->cache_instructions.Add();
  xed_inst2(widen_rdi, arch::kXEDState64, XED_ICLASS_LEA,
            arch::kAddrWidthBits_amd64, xed_reg(XED_REG_RDI),
            xed_mem_gbisd(XED_REG_INVALID, XED_REG_RDI, GRANARY_ABI_MEM64, 1,
                          xed_disp(0, 0), arch::kAddrWidthBits_amd64));
  return true;
}

// Add an emulated version of `app_instr` to `block`.
static bool Emulate(Block *block, const arch::Instruction *app_instr) {
  switch (app_instr->iclass) {
    case XED_ICLASS_PUSH:
      return EmulatePush(block, app_instr);
    case XED_ICLASS_POP:
      return EmulatePop(block, app_instr);
    case XED_ICLASS_PUSHA:
      return EmulatePusha(block);
    case XED_ICLASS_POPA:
      return EmulatePopa(block);
    case XED_ICLASS_PUSHAD:
      return EmulatePushad(block);
    case XED_ICLASS_POPAD:
      return EmulatePopad(block);
    case XED_ICLASS_PUSHF:
      return EmulatePushFlags(block, GRANARY_ABI_VAL16);
    case XED_ICLASS_POPF:
      return EmulatePopFlags(block, GRANARY_ABI_VAL16);
    case XED_ICLASS_PUSHFD:
      return EmulatePushFlags(block, GRANARY_ABI_VAL32);
    case XED_ICLASS_POPFD:
      return EmulatePopFlags(block, GRANARY_ABI_VAL32);
    case XED_ICLASS_ENTER:
      return EmulateEnter(block, app_instr);
    case XED_ICLASS_LEAVE:
      return EmulateLeave(block, app_instr);
    case XED_ICLASS_BOUND:
      return EmulateBound(block, app_instr);
    case XED_ICLASS_UD2:
    case XED_ICLASS_RDTSC:
    case XED_ICLASS_RDTSCP:
    case XED_ICLASS_RDPMC:
    case XED_ICLASS_RDRAND:
      AddError(block);
      return true;
    case XED_ICLASS_XLAT:
      return EmulateXLAT(block, app_instr);
    case XED_ICLASS_NOP:
    case XED_ICLASS_NOP2:
    case XED_ICLASS_NOP3:
    case XED_ICLASS_NOP4:
    case XED_ICLASS_NOP5:
    case XED_ICLASS_NOP6:
    case XED_ICLASS_NOP7:
    case XED_ICLASS_NOP8:
    case XED_ICLASS_NOP9:
      return true;  // Don't encode NOPs.
    case XED_ICLASS_INSB: case XED_ICLASS_REP_INSB:
    case XED_ICLASS_INSD: case XED_ICLASS_REP_INSD:
    case XED_ICLASS_INSW: case XED_ICLASS_REP_INSW:
      return EmulateStringOp(block, app_instr);
    case XED_ICLASS_OUTSB: case XED_ICLASS_REP_OUTSB:
    case XED_ICLASS_OUTSD: case XED_ICLASS_REP_OUTSD:
    case XED_ICLASS_OUTSW: case XED_ICLASS_REP_OUTSW:
      return EmulateStringOp(block, app_instr);
    case XED_ICLASS_MOVSB: case XED_ICLASS_REP_MOVSB:
    case XED_ICLASS_MOVSW: case XED_ICLASS_REP_MOVSW:
    case XED_ICLASS_MOVSD: case XED_ICLASS_REP_MOVSD:
    case XED_ICLASS_MOVSQ: case XED_ICLASS_REP_MOVSQ:
      return EmulateStringOp(block, app_instr);
    case XED_ICLASS_STOSB: case XED_ICLASS_REP_STOSB:
    case XED_ICLASS_STOSD: case XED_ICLASS_REP_STOSW:
    case XED_ICLASS_STOSW: case XED_ICLASS_REP_STOSD:
    case XED_ICLASS_STOSQ: case XED_ICLASS_REP_STOSQ:
      return EmulateStringOp(block, app_instr);
    case XED_ICLASS_SCASB:
    case XED_ICLASS_SCASD:
    case XED_ICLASS_SCASW:
    case XED_ICLASS_SCASQ:
      return EmulateStringOp(block, app_instr);
    case XED_ICLASS_CMPSB:
    case XED_ICLASS_CMPSD:
    case XED_ICLASS_CMPSW:
    case XED_ICLASS_CMPSQ:
      return EmulateStringOp(block, app_instr);
    case XED_ICLASS_LODSB: case XED_ICLASS_REP_LODSB:
    case XED_ICLASS_LODSD: case XED_ICLASS_REP_LODSW:
    case XED_ICLASS_LODSW: case XED_ICLASS_REP_LODSD:
    case XED_ICLASS_LODSQ: case XED_ICLASS_REP_LODSQ:
      return EmulateStringOp(block, app_instr);
    case XED_ICLASS_REPE_CMPSB:
    case XED_ICLASS_REPE_CMPSD:
    case XED_ICLASS_REPE_CMPSQ:
    case XED_ICLASS_REPE_CMPSW:
    case XED_ICLASS_REPE_SCASB:
    case XED_ICLASS_REPE_SCASD:
    case XED_ICLASS_REPE_SCASQ:
    case XED_ICLASS_REPE_SCASW:
    case XED_ICLASS_REPNE_CMPSB:
    case XED_ICLASS_REPNE_CMPSD:
    case XED_ICLASS_REPNE_CMPSQ:
    case XED_ICLASS_REPNE_CMPSW:
    case XED_ICLASS_REPNE_SCASB:
    case XED_ICLASS_REPNE_SCASD:
    case XED_ICLASS_REPNE_SCASQ:
    case XED_ICLASS_REPNE_SCASW:
      return EmulateStringOp(block, app_instr);
    default:
      return false;
  }
}

// Adds a patch point. Patchable jumps are `JMP rel32`, where the JMP opcode
// is 1 byte, and the rel32 is an int32_t.
static void AddPatchPoint(AppPC32 target_pc, CachePC encoded_pc) {
  arch::AddPatchPoint(&(encoded_pc[1]), target_pc);
}

// Report an encoding failure; we need to print out as much info as we can
// get.
static void ReportEncoderFailure(Block *block, arch::Instruction &instr) {
#if GRANARY_REPORT_ENCODER_ERRORS
  char buff[4096] = {'\0'};
  auto offs = sprintf(buff, "Could not encode block [%x, %x):\n  Block Data:",
                      block->StartPC(), block->EndPC());

  offs += sprintf(buff + offs, "\n  Instruction Data:");
  auto instr_bytes = reinterpret_cast<uint8_t *>(
      reinterpret_cast<uintptr_t>(&instr));
  for (auto i = 0UL; i < sizeof instr; ++i) {
    offs += sprintf(buff + offs, " %02x",
                    static_cast<unsigned>(instr_bytes[i]));
  }

  granary_unreachable(buff, nullptr, false /* Don't die */);
#else
  GRANARY_UNUSED(block);
#endif

  // Convert into a `UD2`.
  xed_inst0(&instr, arch::kXEDState64, XED_ICLASS_UD2, 0);
  instr.is_valid = false;
  ConvertToError(block, &instr);
}

}  // namespace

// Encodes the the block and returns a pointer to the location in the code
// cache at which the block was encoded.
//
// Note: This function is NOT thread safe. `EncodedSize` and `Encode` should
//       be performed in a transaction.
void Block::Encode(index::Value &val) {
  EndBlock(this);

  if (GRANARY_UNLIKELY(!num_app_instructions)) {
    val.ends_with_error = true;
    LoadImm(this, GRANARY_ABI_PC32, StartPC());

  } else {
    auto ainstr = &(app_instructions[num_app_instructions - 1].instruction);
    auto first_ainstr = &(app_instructions[0].instruction);

    // Try to emulate the last instruction as a control-flow instruction.
    if (EmulateCFI(this, ainstr)) {
      val.has_one_successor = ainstr->IsDirectJump() ||
                              ainstr->IsDirectFunctionCall();
      val.ends_with_syscall = has_syscall;

      // Record the PC of the jump instruction. Useful if we have a fault when
      // loading the target (e.g. an indirect jump where the target is stored
      // in memory).
      LoadImm(this, GRANARY_ABI_PC32, ainstr->StartPC());

      --ainstr;

    // It wasn't a control-flow instruction, so we emulated it with a direct
    // jump, and so it only has one successor.
    } else {
      val.has_one_successor = !ainstr->IsSerializing();
      val.ends_with_error = ainstr->IsUndefined();
    }

    // For each remaining app instruction (in reverse order), try to emulate,
    // andif not, virtualize each instruction.
    for (; ainstr >= first_ainstr; --ainstr) {
      GRANARY_ASSERT(XED_ICLASS_INVALID != ainstr->iclass);

      if (!Emulate(this, ainstr)) {
        Virtualize(this, ainstr);
      }

      // Instrument the instruction at this program counter.
      InstrumentPC(this, ainstr->StartPC());

      // Load the exact PC32 of each instruction just before emulating the
      // instruction. This gives us precise PCs when reporting crashes.
      LoadImm(this, GRANARY_ABI_PC32, ainstr->StartPC());
    }
    //Instrument(this, code::kInstrumentBlockEntry);
  }

  // Find the beginning of the block. We might push
  auto start_cache_pc = cache::Allocate(0);
  auto actual_start_cache_pc = start_cache_pc;
  if (auto extra = reinterpret_cast<uintptr_t>(start_cache_pc) % 8) {
    actual_start_cache_pc += (8 - extra);
  }

  val.cache_offset = cache::PCToOffset(actual_start_cache_pc);

  // On entry to the block, add the block's identifying info in.
  LoadImm(this, GRANARY_ABI_BLOCK64, val.value);

  // Add padding before the entry of the block to align blocks to 8-byte
  // boundaries.
  for (; start_cache_pc < actual_start_cache_pc; ++start_cache_pc) {
    AddPadding(this);
  }

  // Encode the instructions.
  for (auto &einstr : cache_instructions) {
    auto instr_size = einstr.NumEncodedBytes();
    if (!instr_size) {
      ReportEncoderFailure(this, einstr);
      has_error = true;
      instr_size = einstr.NumEncodedBytes();
    }

    if (instr_size) {
      auto encode_pc = cache::Allocate(instr_size);
      UpdateRelBranch(&einstr, encode_pc);
      einstr.Encode(encode_pc);

      if (&einstr == gBranchTaken.cache_instr) {
        GRANARY_ASSERT(5 == instr_size);
        AddPatchPoint(gBranchTaken.app_pc32, encode_pc);
        memset(&gBranchTaken, 0, sizeof gBranchTaken);
      }

      if (&einstr == gBranchNotTaken.cache_instr) {
        GRANARY_ASSERT(5 == instr_size);
        AddPatchPoint(gBranchNotTaken.app_pc32, encode_pc);
        memset(&gBranchNotTaken, 0, sizeof gBranchNotTaken);
      }
    }
  }

  // Report back up the chain (to the indexer) that this block has an error
  // in it (somewhere). This will prevent us from even executing it.
  if (has_error) {
    val.ends_with_error = true;
  }

  arch::SerializePipeline();
}

}  // namespace granary

