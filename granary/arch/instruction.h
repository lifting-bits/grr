/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_INSTRUCTION_H_
#define GRANARY_ARCH_INSTRUCTION_H_

#include "granary/arch/x86/instruction.h"

#include <forward_list>

namespace granary {
namespace arch {

// Used to allocate instructions in the REVERSE of the order that they
// will be encoded.
class InstructionStack : public std::forward_list<arch::Instruction> {
 public:
  inline Instruction *Add(void) {
    Instruction instr;
    memset(&instr, 0, sizeof instr);
    auto item = this->emplace_after(this->before_begin());
    return &*item;
  }

  inline void Reset(void) {
    this->clear();
  }
};

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_ARCH_INSTRUCTION_H_
