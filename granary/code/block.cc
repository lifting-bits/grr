/* Copyright 2015 Peter Goodman, all rights reserved. */

#include <gflags/gflags.h>

#include "granary/code/cache.h"
#include "granary/code/block.h"

DEFINE_int32(max_instructions_per_block, 32,
             "Maximum number of instructions per basic block.");

namespace granary {

// Initialize a block.
Block::Block(void)
    : start_app_pc(0),
      end_app_pc(0),
      num_app_instructions(0),
      has_syscall(false),
      has_error(false),
      app_instructions(),
      cache_instructions() {}

AppPC32 Block::StartPC(void) const {
  return start_app_pc;
}

AppPC32 Block::EndPC(void) const {
  return end_app_pc;
}

size_t Block::NumInstructions(void) const {
  return num_app_instructions;
}

size_t Block::NumBytes(void) const {
  return static_cast<size_t>(end_app_pc - start_app_pc);
}

// Return the first instruction of the block.
Instruction *Block::FirstInstruction(void) {
  return &(app_instructions[0]);
}

Instruction *Block::LastInstruction(void) {
  return &(app_instructions[num_app_instructions - 1]);
}

// Iterate over application instructions.
InstructionIterator Block::begin(void) {
  return InstructionIterator(FirstInstruction());
}

InstructionIterator Block::end(void) {
  return InstructionIterator(LastInstruction() + 1);
}

ReverseInstructionIterator Block::rbegin(void) {
  return ReverseInstructionIterator(LastInstruction());
}

ReverseInstructionIterator Block::rend(void) {
  return ReverseInstructionIterator(FirstInstruction() - 1);
}

namespace {

// Returns true if `instr` should mark the end of a block.
static bool AtBlockEnd(const Instruction &instr) {
  return instr.IsBranch() || instr.IsJump() ||
         instr.IsFunctionCall() || instr.IsFunctionReturn() ||
         instr.IsSystemCall() || instr.IsSystemReturn() ||
         instr.IsInterruptCall() || instr.IsInterruptReturn() ||
         instr.IsSerializing() || instr.IsUndefined();
}

// Read the bytes of an instruction.
static size_t ReadInstructionBytes(
    const std::function<bool(AppPC32 pc, uint8_t &byte)> &try_read_byte,
    AppPC32 pc32, uint8_t *bytes) {
  for (auto i = 0U; i < arch::kMaxNumInstructionBytes; ++i) {
    if (!try_read_byte(pc32 + i, bytes[i])) {
      return i;
    }
  }
  return arch::kMaxNumInstructionBytes;
}

// Decode an instruction.
static Instruction DecodeInstruction(Block *block, AppPC32 pc32, uint8_t *bytes,
                                     size_t num_bytes) {
  Instruction instr;
  auto &ainstr(instr.instruction);
  if (!ainstr.TryDecode(bytes, num_bytes, arch::ISA::x86)) {
    block->has_error = true;
  }
  ainstr.SetStartPC(pc32);
  return instr;
}


}  // namespace

// Decodes and returns the block starting at `start_app_pc`.
void Block::DoDecode(
    AppPC32 pc32,
    const std::function<bool(AppPC32 pc, uint8_t &byte)> &try_read_byte) {
  start_app_pc = pc32;
  end_app_pc = pc32;

  app_instructions.reserve(FLAGS_max_instructions_per_block);

  uint8_t instr_bytes[arch::kMaxNumInstructionBytes] = {0};
  for (int i = 0; i < FLAGS_max_instructions_per_block; ++i) {
    auto num_bytes = ReadInstructionBytes(try_read_byte, pc32, instr_bytes);
    auto instr = DecodeInstruction(this, pc32, instr_bytes, num_bytes);
    end_app_pc += instr.NumBytes();
    pc32 = end_app_pc;
    num_app_instructions += 1;
    app_instructions.push_back(instr);
    if (AtBlockEnd(instr)) break;
  }
}

}  // namespace granary
