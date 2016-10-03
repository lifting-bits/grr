/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/arch/instruction.h"

namespace granary {
namespace arch {

extern const xed_state_t kXEDState32 = {
    XED_MACHINE_MODE_LONG_COMPAT_32,
    XED_ADDRESS_WIDTH_32b};

extern const xed_state_t kXEDState64 = {
    XED_MACHINE_MODE_LONG_64,
    XED_ADDRESS_WIDTH_64b};

namespace {

// Returns true if we've marked a register as live in the GPR set.
static bool TryAddToGPRSet(GPRSet *set, xed_reg_enum_t reg64) {
  switch (reg64) {
    case XED_REG_RAX: set->gax = true; return true;
    case XED_REG_RCX: set->gcx = true; return true;
    case XED_REG_RDX: set->gdx = true; return true;
    case XED_REG_RBX: set->gbx = true; return true;
    case XED_REG_RSP: set->gsp = true; return true;
    case XED_REG_RBP: set->gbp = true; return true;
    case XED_REG_RSI: set->gsi = true; return true;
    case XED_REG_RDI: set->gdi = true; return true;
    case XED_REG_R8: set->r8 = true; return true;
    case XED_REG_R9: set->r9 = true; return true;
    case XED_REG_R10: set->r10 = true; return true;
    case XED_REG_R11: set->r11 = true; return true;
    case XED_REG_R12: set->r12 = true; return true;
    case XED_REG_R13: set->r13 = true; return true;
    case XED_REG_R14: set->r14 = true; return true;
    case XED_REG_R15: set->r15 = true; return true;
    default: return false;
  }
}

// Updates some GPR sets with a GPR.
static void UpdateGPRSets(Instruction *instr, xed_reg_enum_t reg,
                          xed_operand_action_enum_t action) {
  auto reg64 = xed_get_largest_enclosing_register(reg);
  switch (action) {
    case XED_OPERAND_ACTION_RW:
    case XED_OPERAND_ACTION_RCW:
    case XED_OPERAND_ACTION_CRW:
      TryAddToGPRSet(&(instr->gprs_read), reg64);
      TryAddToGPRSet(&(instr->gprs_written), reg64);
      break;
    case XED_OPERAND_ACTION_R:
    case XED_OPERAND_ACTION_CR:
      TryAddToGPRSet(&(instr->gprs_read), reg64);
      break;

    // Write-only operands are tricky because they might actually not write to
    // the full register, so we want to represent a partial write as a read
    // dependency.
    case XED_OPERAND_ACTION_W:
    case XED_OPERAND_ACTION_CW:
      if (TryAddToGPRSet(&(instr->gprs_written), reg64) &&
          32 > xed_get_register_width_bits64(reg)) {
        TryAddToGPRSet(&(instr->gprs_read), reg64);
      }
      break;

    default: break;
  }
}

// Decode a memory operand.
//
// Note: This only applies to `MEM0` operands.
static xed_encoder_operand_t DecodeMemory0(const xed_decoded_inst_t *xedd,
                                           Instruction *instr,
                                           xed_operand_enum_t op_name,
                                           unsigned op_num) {
  xed_encoder_operand_t op;
  op.type = XED_ENCODER_OPERAND_TYPE_MEM;
  op.u.mem.seg = xed_decoded_inst_get_seg_reg(xedd, 0);
  op.u.mem.base = xed_decoded_inst_get_base_reg(xedd, 0);
  op.u.mem.index = xed_decoded_inst_get_index_reg(xedd, 0);
  op.u.mem.disp.displacement = \
      xed_decoded_inst_get_memory_displacement(xedd, 0);
  op.u.mem.disp.displacement_width = \
      xed_decoded_inst_get_memory_displacement_width_bits(xedd, 0);
  op.u.mem.scale = xed_decoded_inst_get_scale(xedd, 0);
  op.width = xed_decoded_inst_operand_length_bits(xedd, op_num) ?:
             instr->effective_operand_width;
  auto base64 = xed_get_largest_enclosing_register(op.u.mem.base);
  auto index64 = xed_get_largest_enclosing_register(op.u.mem.base);

  if (XED_REG_RIP == base64) instr->has_pc_rel_op = true;
  if (XED_REG_RIP == index64) instr->has_pc_rel_op = true;

  TryAddToGPRSet(&(instr->gprs_written), base64);
  TryAddToGPRSet(&(instr->gprs_written), index64);

  if (XED_OPERAND_AGEN != op_name) {
    instr->reads_mem = xed_decoded_inst_mem_read(xedd, 0);
    instr->writes_mem = xed_decoded_inst_mem_written(xedd, 0);
  }
  return op;
}

// Decode `IMM0` operands.
static xed_encoder_operand_t DecodeImm0(const xed_decoded_inst_t *xedd,
                                        xed_operand_enum_t op_name) {
  xed_encoder_operand_t op;
  if (XED_OPERAND_IMM0SIGNED == op_name ||
      xed_operand_values_get_immediate_is_signed(xedd)) {
    op.type = XED_ENCODER_OPERAND_TYPE_SIMM0;
    op.u.imm0 = static_cast<uintptr_t>(static_cast<intptr_t>(
        xed_decoded_inst_get_signed_immediate(xedd)));
  } else {
    op.type = XED_ENCODER_OPERAND_TYPE_IMM0;
    op.u.imm0 = xed_decoded_inst_get_unsigned_immediate(xedd);
  }
  op.width = xed_decoded_inst_get_immediate_width_bits(xedd);
  return op;
}

// Decode `IMM1` operands.
static xed_encoder_operand_t DecodeImm1(const xed_decoded_inst_t *xedd) {
  xed_encoder_operand_t op;
  op.type = XED_ENCODER_OPERAND_TYPE_IMM1;
  op.u.imm1 = xed_decoded_inst_get_second_immediate(xedd);
  op.width = xed_decoded_inst_get_immediate_width_bits(xedd);
  return op;
}

// Pull out a pointer operand. These are used in things like call/jmp far.
static xed_encoder_operand_t DecodePtr(const xed_decoded_inst_t *) {
  //GRANARY_ASSERT(false && "Unexpected PTR operand.");
  // TODO(pag): Unimplemented.
  //return {};
  xed_encoder_operand_t op;
  op.type = XED_ENCODER_OPERAND_TYPE_INVALID;
  return op;
}

// Pull out a register operand from the XED instruction.
static xed_encoder_operand_t DecodeReg(const xed_decoded_inst_t *xedd,
                                       const xed_operand_t *xedo,
                                       Instruction *instr,
                                       const xed_operand_enum_t op_name) {
  xed_encoder_operand_t op;
  op.type = XED_ENCODER_OPERAND_TYPE_REG;
  op.u.reg = xed_decoded_inst_get_reg(xedd, op_name);
  if (XED_REG_AH <= op.u.reg && op.u.reg <= XED_REG_BH) {
    instr->uses_legacy_registers = true;
  }
  if (XED_ADDRESS_WIDTH_64b == instr->mode.stack_addr_width) {
    op.width = xed_get_register_width_bits64(op.u.reg);
  } else {
    op.width = xed_get_register_width_bits(op.u.reg);
  }
  UpdateGPRSets(instr, op.u.reg, xed_operand_rw(xedo));
  return op;
}

// Pull out a relative branch target.
static xed_encoder_operand_t DecodeRelbr(const xed_decoded_inst_t *xedd) {
  xed_encoder_operand_t op;
  op.type = XED_ENCODER_OPERAND_TYPE_BRDISP;
  op.u.brdisp = xed_decoded_inst_get_branch_displacement(xedd);
  op.width = xed_decoded_inst_get_branch_displacement_width_bits(xedd);
  return op;
}

// Converts a `xed_operand_t` into an operand in `instr`.
static void DecodeOperand(const xed_decoded_inst_t *xedd,
                          const xed_operand_t *xedo,
                          Instruction *instr) {
  auto op_num = instr->noperands++;
  auto &op = instr->operands[op_num];
  switch (auto op_name = xed_operand_name(xedo)) {
    case XED_OPERAND_AGEN:
    case XED_OPERAND_MEM0:
      op = DecodeMemory0(xedd, instr, op_name, op_num);
      break;

    case XED_OPERAND_IMM0SIGNED:
    case XED_OPERAND_IMM0:
      op = DecodeImm0(xedd, op_name);
      break;

    case XED_OPERAND_IMM1_BYTES:
    case XED_OPERAND_IMM1:
      op = DecodeImm1(xedd);
      break;

    case XED_OPERAND_PTR:
      op = DecodePtr(xedd);
      break;

    case XED_OPERAND_REG0:
    case XED_OPERAND_REG1:
    case XED_OPERAND_REG2:
    case XED_OPERAND_REG3:
      op = DecodeReg(xedd, xedo, instr, op_name);
      break;

    case XED_OPERAND_RELBR:
      op = DecodeRelbr(xedd);
      instr->has_pc_rel_op = true;
      break;

    // TODO(pag): Handle `XED_OPERAND_IMM1_BYTES`?
    default:
      GRANARY_ASSERT(false && "Unexpected operand type for encoding.");
      return;
  }
}

// Update flags in the instruction that track registers and arithmetic flags.
static void UpdateFlags(const xed_decoded_inst_t *xedd,
                        const xed_operand_t *xedo, Instruction *instr) {
  switch (auto op_name = xed_operand_name(xedo)) {
    case XED_OPERAND_REG:
    case XED_OPERAND_REG0:
    case XED_OPERAND_REG1:
    case XED_OPERAND_REG2:
    case XED_OPERAND_REG3:
    case XED_OPERAND_REG4:
    case XED_OPERAND_REG5:
    case XED_OPERAND_REG6:
    case XED_OPERAND_REG7:
    case XED_OPERAND_REG8: {
      auto reg = xed_decoded_inst_get_reg(xedd, op_name);
      if (XED_REG_AH <= reg && reg <= XED_REG_BH) {
        instr->uses_legacy_registers = true;
      }
      switch (reg) {
        case XED_REG_STACKPUSH:
        case XED_REG_STACKPOP:
          instr->gprs_read.gsp = true;
          instr->gprs_written.gsp = true;
          break;

        case XED_REG_FLAGS:
        case XED_REG_EFLAGS:
        case XED_REG_RFLAGS:
          instr->reads_aflags = xed_operand_read(xedo);
          instr->writes_aflags = xed_operand_written(xedo);
          break;

        default:
          UpdateGPRSets(instr, reg, xed_operand_rw(xedo));
          break;
      }
      break;
    }
    default: break;
  }
}

// Converts a `xed_decoded_inst_t` into an `Instruction`.
static void DecodeOperands(const xed_decoded_inst_t *xedd,
                           const xed_inst_t *xedi, Instruction *instr) {
  auto num_ops = xed_inst_noperands(xedi);
  for (auto i = 0U; i < num_ops; ++i) {
    auto xedo = xed_inst_operand(xedi, i);
    switch (xed_operand_operand_visibility(xedo)) {
      case XED_OPVIS_EXPLICIT:
      case XED_OPVIS_IMPLICIT:
        DecodeOperand(xedd, xedo, instr);
        break;
      case XED_OPVIS_SUPPRESSED:
        UpdateFlags(xedd, xedo, instr);
        break;
      default: return;
    }
  }
}

// Decode the prefixes. We purposely ignore branch taken/not taken hints.
static void DecodePrefixes(const xed_decoded_inst_t *xedd, Instruction *instr) {
  if (xed_operand_values_has_real_rep(xedd)) {
    instr->prefixes.s.rep = xed_operand_values_has_rep_prefix(xedd);
    instr->prefixes.s.repne = xed_operand_values_has_repne_prefix(xedd);
  }
  instr->prefixes.s.lock = xed_operand_values_has_lock_prefix(xedd);

  // TODO(pag): XACQUIRE, XRELEASE, MPX.
}

// Do an actual decoding of up to `num_bytes` bytes.
static bool DoDecode(xed_decoded_inst_t *xedd, const xed_state_t *dstate,
                     const void *pc_, unsigned num_bytes) {
  xed_decoded_inst_zero_set_mode(xedd, dstate);
  xed_decoded_inst_set_input_chip(xedd, XED_CHIP_INVALID);
  auto pc = reinterpret_cast<const unsigned char *>(pc_);
  if (XED_ERROR_NONE != xed_decode(xedd, pc, num_bytes)) {
    return false;
  }
  switch (xed_decoded_inst_get_category(xedd)) {
    case XED_CATEGORY_AES:
    case XED_CATEGORY_AVX:
    case XED_CATEGORY_AVX2:
    case XED_CATEGORY_AVX2GATHER:
    case XED_CATEGORY_AVX512:
    case XED_CATEGORY_AVX512VBMI:
    case XED_CATEGORY_RDRAND:
    case XED_CATEGORY_RDSEED:
      return false;
    default:
      return true;
  }
}

// Makes an undefined instruction.
static void MakeUD2(Instruction *instr, ISA isa) {
  auto state = ISA::x86 == isa ? kXEDState32 : kXEDState64;
  xed_inst0(instr, state, XED_ICLASS_UD2, 0);
  instr->decoded_length = 2;
  instr->bytes[0] = 0x0F;
  instr->bytes[1] = 0x0B;
  instr->is_valid = true;
}

// Set of registers, in the reverse order from what they appear in the GPRSet,
// such that the index of a register in this array corresponds to the bit
// position of the register in the GPRSet.
static xed_reg_enum_t gGprsInSet[] = {
  XED_REG_RSP,
  XED_REG_RAX,
  XED_REG_RCX,
  XED_REG_RDX,
  XED_REG_RBX,
  XED_REG_RBP,
  XED_REG_RSI,
  XED_REG_RDI,
  XED_REG_R8,
  XED_REG_R9,
  XED_REG_R10,
  XED_REG_R11,
  XED_REG_R12,
  XED_REG_R13,
  XED_REG_R14,
  XED_REG_R15
};

// Try to verify that all operands were successfully decoded.
//
// TODO(pag): This is ugly. That is, our only way of signaling a failure to
//            decode an operand is via its operand type.
static bool VerifyDecodedOperands(const Instruction *instr) {
  for (auto i = 0U; i < instr->noperands; ++i) {
    const auto &op = instr->operands[i];
    if (XED_ENCODER_OPERAND_TYPE_INVALID == op.type) return false;
  }
  return true;
}

}  // namespace

// Revives and returns the next dead register in the register set.
xed_reg_enum_t GPRSet::ReviveNextDeadReg(void) {
  for (auto i = 0U; i < 16U; ++i) {
    auto bit = 1U << i;
    if (!(bit & bits)) {
      bits |= bit;
      return gGprsInSet[16 - i];
    }
  }
  return XED_REG_INVALID;
}

// Decode an instruction.
bool Instruction::TryDecode(const uint8_t *decode_bytes, size_t max_num_bytes,
                            ISA isa) {
  xed_decoded_inst_t xedd;
  memset(this, 0, sizeof *this);

  auto state = ISA::x86 == isa ? &kXEDState32 : &kXEDState64;
  max_num_bytes = std::min<size_t>(kMaxNumInstructionBytes, max_num_bytes);

  // If we come across an invalid instruction, then we'll propagate it out to
  // the real code so that a `SIGILL` in native code turns into a `SIGILL` in
  // translated code.
  if (!max_num_bytes || !DoDecode(&xedd, state, decode_bytes, max_num_bytes)) {
    MakeUD2(this, isa);
    return false;
  }

  decoded_length = xed_decoded_inst_get_length(&xedd);
  is_valid = true;  // Assume there are no PC-relative references.

  iclass = xed_decoded_inst_get_iclass(&xedd);

  // Good defaults, will fixup special cases later.
  effective_address_width = AddressWidth<ISA::x86>();
  effective_operand_width = xed_decoded_inst_get_operand_width(&xedd);

  memcpy(bytes, decode_bytes, decoded_length);
  DecodePrefixes(&xedd, this);
  DecodeOperands(&xedd, xed_decoded_inst_inst(&xedd), this);
  return VerifyDecodedOperands(this);
}

namespace {
// Perform the actual encoding of an instruction.
static bool DoEncode(Instruction *instr, uint8_t *bytes) {
  if (XED_ICLASS_JECXZ == instr->iclass) {
    bytes[0] = 0x67;
    bytes[1] = 0xe3;
    bytes[2] = static_cast<uint8_t>(instr->operands[0].u.brdisp);
    return true;
  } else {
    xed_encoder_request_t xede;
    xed_encoder_request_zero_set_mode(&xede, &(instr->mode));
    if (!xed_convert_to_encoder_request(&xede, instr)) return false;
    return XED_ERROR_NONE == xed_encode(&xede, bytes, kMaxNumInstructionBytes,
                                        &(instr->encoded_length));
  }
}
}  // namespace

size_t Instruction::NumEncodedBytes(void) {
  if (!DoEncode(this, bytes)) return 0;
  is_valid = true;
  return encoded_length;
}

// Copies an already encoded version of the instruction to `encode_pc`, or
// encodes it directly into the right location.
void Instruction::Encode(CachePC encode_pc) {
  if (encoded_length && is_valid) {
    memcpy(encode_pc, bytes, encoded_length);
  } else {
    is_valid = DoEncode(this, encode_pc);
  }
}

// See: http://www.sandpile.org/x86/coherent.htm
bool Instruction::IsSerializing(void) const {
  switch (iclass) {
    case XED_ICLASS_IRET:
    case XED_ICLASS_IRETD:
    case XED_ICLASS_IRETQ:
    case XED_ICLASS_RSM:
    case XED_ICLASS_CPUID:
    case XED_ICLASS_LGDT:
    case XED_ICLASS_LIDT:
    case XED_ICLASS_LLDT:
    case XED_ICLASS_LTR:
    case XED_ICLASS_INVLPG:
    case XED_ICLASS_INVLPGA:
    case XED_ICLASS_INVEPT:
    case XED_ICLASS_INVVPID:
    case XED_ICLASS_INVD:
    case XED_ICLASS_WBINVD:
    case XED_ICLASS_LMSW:
    case XED_ICLASS_WRMSR:
    case XED_ICLASS_SWAPGS:
    case XED_ICLASS_INT1:
    case XED_ICLASS_INT3:
    case XED_ICLASS_INTO:
    case XED_ICLASS_INT:
    case XED_ICLASS_LDS:
    case XED_ICLASS_LSS:
    case XED_ICLASS_LGS:
    case XED_ICLASS_LES:
    case XED_ICLASS_LFS:
      return true;

    // TODO(pag): Check things like `MOV SS, ...`.
    default:
      return false;
  }
}

}  // namespace arch
}  // namespace granary
