/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/code/instruction.h"

namespace granary {

bool Instruction::IsFunctionCall(void) const {
  return instruction.IsFunctionCall();
}

bool Instruction::IsFunctionReturn(void) const {
  return instruction.IsFunctionReturn();
}

bool Instruction::IsInterruptCall(void) const {
  return instruction.IsInterruptCall();
}

bool Instruction::IsInterruptReturn(void) const {
  return instruction.IsInterruptReturn();
}

bool Instruction::IsSystemCall(void) const {
  return instruction.IsSystemCall();
}

bool Instruction::IsSystemReturn(void) const {
  return instruction.IsSystemReturn();
}

bool Instruction::IsJump(void) const {
  return instruction.IsJump();
}

bool Instruction::IsBranch(void) const {
  return instruction.IsBranch();
}

bool Instruction::IsDirectFunctionCall(void) const {
  return instruction.IsDirectFunctionCall();
}

bool Instruction::IsIndirectFunctionCall(void) const {
  return instruction.IsIndirectFunctionCall();
}

bool Instruction::IsDirectJump(void) const {
  return instruction.IsDirectJump();
}

bool Instruction::IsIndirectJump(void) const {
  return instruction.IsIndirectJump();
}

bool Instruction::IsSerializing(void) const {
  return instruction.IsSerializing();
}

bool Instruction::IsUndefined(void) const {
  return instruction.IsUndefined();
}

// Beginning program counter of this instruction.
AppPC32 Instruction::StartPC(void) const {
  return instruction.StartPC();
}

// Program counter of the next logical instruction after this instruction.
AppPC32 Instruction::EndPC(void) const {
  return instruction.EndPC();
}

// Size of this instruction in bytes.
size_t Instruction::NumBytes(void) const {
  return instruction.NumBytes();
}

// Target of a branch, direct jump, or direct call. These only work on
// their respective, non-indirect instruction kinds.
AppPC32 Instruction::BranchTakenPC(void) const {
  GRANARY_ASSERT(IsBranch());
  return instruction.TargetPC();
}

AppPC32 Instruction::BranchNotTakenPC(void) const {
  GRANARY_ASSERT(IsBranch());
  return EndPC();
}

AppPC32 Instruction::JumpTargetPC(void) const {
  GRANARY_ASSERT(IsDirectJump());
  return instruction.TargetPC();
}

AppPC32 Instruction::FunctionCallTargetPC(void) const {
  GRANARY_ASSERT(IsDirectFunctionCall());
  return instruction.TargetPC();
}
InstructionIterator::InstructionIterator(Instruction *cursor_)
    : cursor(cursor_) {}

Instruction *InstructionIterator::operator*(void) const {
  return cursor;
}

InstructionIterator &InstructionIterator::operator++(void) {
  ++cursor;
  return *this;
}

ReverseInstructionIterator::ReverseInstructionIterator(
    Instruction *cursor_)
    : cursor(cursor_) {}

Instruction *ReverseInstructionIterator::operator*(void) const {
  return cursor;
}

ReverseInstructionIterator &ReverseInstructionIterator::operator++(void) {
  --cursor;
  return *this;
}

}  // namespace granary
