/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_INSTRUMENT_H_
#define GRANARY_INSTRUMENT_H_

#include <vector>

#include "granary/base/base.h"

namespace granary {
namespace code {

enum InstrumentationPoint {
  kInstrumentMultiWayBranch = 0,
  kInstrumentBlockEntry = 1,
  kInstrumentPC = 2,
  kInstrumentMemoryAddress = 3,
  kInvalid
};

typedef std::vector<unsigned> InstrumentationIds;

// Adds an instrumentation function.
void AddInstrumentationFunction(InstrumentationPoint ipoint,
                                void (*func)(void));

void AddPCInstrumentation(Addr32 pc);

// Returns the location in the code cache of where this instrumentation
// function is.
uintptr_t GetInstrumentationFunction(InstrumentationPoint ipoint);

const InstrumentationIds &GetInstrumentationIds(Addr32 pc);

}  // namespace code
}  // namespace granary

#endif  // GRANARY_INSTRUMENT_H_
