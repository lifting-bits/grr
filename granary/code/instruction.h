/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_INSTRUCTION_H_
#define GRANARY_CODE_INSTRUCTION_H_

#include "granary/arch/instruction.h"

namespace granary {

// Represents a decoded application instruction that can be instrumented.
class Instruction final {
 public:
  Instruction(void) = default;

  bool IsFunctionCall(void) const;
  bool IsFunctionReturn(void) const;
  bool IsInterruptCall(void) const;
  bool IsInterruptReturn(void) const;
  bool IsSystemCall(void) const;
  bool IsSystemReturn(void) const;
  bool IsJump(void) const;
  bool IsBranch(void) const;

  bool IsDirectFunctionCall(void) const;
  bool IsIndirectFunctionCall(void) const;

  bool IsDirectJump(void) const;
  bool IsIndirectJump(void) const;

  bool IsSerializing(void) const;
  bool IsUndefined(void) const;

  bool IsNoOp(void) const;

  // Beginning program counter of this instruction.
  AppPC32 StartPC(void) const;

  // Program counter of the next logical instruction after this instruction.
  AppPC32 EndPC(void) const;

  // Size of this instruction in bytes.
  size_t NumBytes(void) const;

  // Target of a branch, direct jump, or direct call. These only work on
  // their respective, non-indirect instruction kinds.
  AppPC32 BranchTakenPC(void) const;
  AppPC32 BranchNotTakenPC(void) const;
  AppPC32 JumpTargetPC(void) const;
  AppPC32 FunctionCallTargetPC(void) const;

 GRANARY_PROTECTED:
  // Internal architecture-specific instruction.
  arch::Instruction instruction;
};

// Iterate forward over application instructions.
class InstructionIterator {
 public:
  InstructionIterator(const InstructionIterator &) = default;
  InstructionIterator(InstructionIterator &&) = default;

  explicit InstructionIterator(Instruction *cursor_);

  inline bool operator!=(const InstructionIterator &that) const {
    return cursor != that.cursor;
  }

  Instruction *operator*(void) const;
  InstructionIterator &operator++(void);

 private:
  InstructionIterator(void) = delete;

  Instruction *cursor;
};

// Iterate backward over instructions.
class ReverseInstructionIterator {
 public:
  ReverseInstructionIterator(
      const ReverseInstructionIterator &) = default;
  ReverseInstructionIterator(ReverseInstructionIterator &&) = default;

  explicit ReverseInstructionIterator(Instruction *cursor_);

  inline bool operator!=(const ReverseInstructionIterator &that) const {
    return cursor != that.cursor;
  }

  Instruction *operator*(void) const;
  ReverseInstructionIterator &operator++(void);

 private:
  ReverseInstructionIterator(void) = delete;

  Instruction *cursor;
};

}  // namespace

#endif  // GRANARY_CODE_INSTRUCTION_H_
