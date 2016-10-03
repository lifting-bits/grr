/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/arch/instruction.h"
#include "granary/arch/instrument.h"

namespace granary {
namespace arch {

extern const xed_state_t kXEDState64;

namespace {

static CachePC gInstFuncs[code::InstrumentationPoint::kInvalid] = {nullptr};

}  // namespace

// Initialize instrumentation routines for use within basic blocks.
void InitInstrumentationFunctions(CachePC instrument_section) {
  auto ipoint_max = static_cast<int>(code::InstrumentationPoint::kInvalid);
  for (auto i = 0; i < ipoint_max; ++i) {
    arch::Instruction instr;
    memset(&instr, 0, sizeof instr);
    memset(instrument_section, 0x90, 8);

    auto ipoint = static_cast<code::InstrumentationPoint>(i);
    if (auto addr = code::GetInstrumentationFunction(ipoint)) {
      auto next_pc = instrument_section + 5;
      auto target_pc = reinterpret_cast<CachePC>(addr);
      xed_inst1(&instr, arch::kXEDState64, XED_ICLASS_JMP,
                kAddrWidthBits_amd64, xed_relbr(target_pc - next_pc, 32));
    } else {
      xed_inst0(&instr, arch::kXEDState64, XED_ICLASS_RET_NEAR,
                kAddrWidthBits_amd64);
    }
    gInstFuncs[ipoint] = instrument_section;
    instr.Encode(instrument_section);
    instrument_section += 8;
  }
}

// Returns the location in the code cache of where this instrumentation
// function is.
CachePC GetInstrumentationFunction(code::InstrumentationPoint ipoint) {
  GRANARY_ASSERT(code::InstrumentationPoint::kInvalid != ipoint);
  return gInstFuncs[ipoint];
}

}  // namespace arch
}  // namespace granary
