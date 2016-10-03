/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_INSTRUMENT_H_
#define GRANARY_ARCH_INSTRUMENT_H_

#include "granary/base/base.h"

#include "granary/code/instrument.h"

namespace granary {
namespace arch {

// Initialize instrumentation routines for use within basic blocks.
void InitInstrumentationFunctions(CachePC instrument_section);

// Returns the location in the code cache of where this instrumentation
// function is.
CachePC GetInstrumentationFunction(code::InstrumentationPoint loc);

}  // namespace arch
}  // namespace granary


#endif  // GRANARY_ARCH_INSTRUMENT_H_
