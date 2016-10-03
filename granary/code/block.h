/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_BLOCK_H_
#define GRANARY_CODE_BLOCK_H_

#include "granary/code/index.h"
#include "granary/code/instruction.h"

#include <functional>
#include <vector>

namespace granary {

// Represents a decoded basic block of application code that can be
// instrumented.
class Block final {
 public:
  Block(void);

  // Beginning program counter of this instruction.
  AppPC32 StartPC(void) const;

  // Program counter of the next logical instruction after this instruction.
  AppPC32 EndPC(void) const;

  // Size of this instruction in bytes.
  size_t NumBytes(void) const;

  // Number of instructions in this block.
  size_t NumInstructions(void) const;

  // Return the first/last instruction of the block.
  Instruction *FirstInstruction(void);
  Instruction *LastInstruction(void);

  InstructionIterator begin(void);
  InstructionIterator end(void);

  ReverseInstructionIterator rbegin(void);
  ReverseInstructionIterator rend(void);

  // Decodes and returns the block starting at `start_pc`.
  template <typename TryRead>
  void Decode(AppPC32 pc32, TryRead try_read_byte) {
    DoDecode(pc32, try_read_byte);
  }

  // Encodes the the block and returns a pointer to the location in the code
  // cache at which the block was encoded.
  //
  // Note: This function is NOT thread safe. `EncodedSize` and `Encode` should
  //       be performed in a transaction.
  void Encode(index::Value &val);

  AppPC32 start_app_pc;
  AppPC32 end_app_pc;  // After the CFI.

  // Number of application instructions in this block.
  unsigned num_app_instructions;

  // Does this block have a system call?
  bool has_syscall;

  // Was there an error (decoding/emulating/encoding)?
  bool has_error;

  // Actual instructions in this block.
  std::vector<Instruction> app_instructions;

  // Queue of instructions that will be encoded.
  arch::InstructionStack cache_instructions;

 private:
  // Decodes and returns the block starting at `start_pc`.
  void DoDecode(
      AppPC32 pc32,
      const std::function<bool(AppPC32 pc, uint8_t &byte)> &try_read_byte);

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Block);
};

}  // namespace granary

#endif  // GRANARY_CODE_BLOCK_H_
