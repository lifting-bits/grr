/* Copyright 2015 Peter Goodman, all rights reserved. */

#include <unordered_map>
#include <vector>

#include "granary/code/instrument.h"

namespace granary {
namespace code {
namespace {

static uintptr_t gInstFuncs[InstrumentationPoint::kInvalid] = {0};
static std::unordered_map<Addr32, InstrumentationIds> gPCInstFuncs;

static const InstrumentationIds gNoPCFunc = {};

static unsigned gNextPCId = 0;

}  // namespace

// Adds an instrumentation function.
void AddInstrumentationFunction(InstrumentationPoint ipoint,
                                void (*func)(void)) {
  // This works as long as:
  //    1)  The code cache is allocated as `MAP_32BIT`.
  //    2)  The build of `grr` doesn't change across two
  //        uses of a persisted code cache. This part is *REALLY* important.
  gInstFuncs[ipoint] = reinterpret_cast<uintptr_t>(func);
}

// Set the value of the sole to the PC instrumentation function.
void AddPCInstrumentation(Addr32 pc) {
  auto id = gNextPCId++;
  if (pc) {
    gPCInstFuncs[pc].push_back(id);
  }
}

// Returns the location in the code cache of where this instrumentation
// function is.
uintptr_t GetInstrumentationFunction(InstrumentationPoint ipoint) {
  GRANARY_ASSERT(InstrumentationPoint::kInvalid != ipoint);
  return gInstFuncs[ipoint];
}

const InstrumentationIds &GetInstrumentationIds(Addr32 pc) {
  auto x = gPCInstFuncs.find(pc);
  if (x == gPCInstFuncs.end()) {
    return gNoPCFunc;
  } else {
    return x->second;
  }
}

}  // namespace code
}  // namespace granary
