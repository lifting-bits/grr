/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/code/branch_tracer.h"
#include "granary/code/index.h"
#include "granary/code/instrument.h"

#include "granary/os/process.h"

#include <iostream>

#include <gflags/gflags.h>

DEFINE_bool(branch_tracer, false, "Enable branch tracing? If so, the branch "
                                  "trace is printed to stderr.");

namespace granary {
namespace code {

extern "C" {
void TraceBranchImpl(
    Addr32 block_pc_of_last_branch,
    Addr32 block_pc_of_branch,
    Addr32 target_block_pc_of_branch) {
  os::gProcess->SaveFPUState();
  std::cerr << std::hex
            << block_pc_of_last_branch << " -> "
            << block_pc_of_branch << " -> "
            << target_block_pc_of_branch << std::endl;
  os::gProcess->RestoreFPUState();
}

// Defined in `tracer.S`. Saves some machine state
extern void TraceBranch(void);

}  // extern C

void InitBranchTracer(void) {
  if (!FLAGS_branch_tracer) {
    return;
  }
  code::AddInstrumentationFunction(
      code::InstrumentationPoint::kInstrumentMultiWayBranch,
      TraceBranch);
}

void ExitBranchTracer(void) {

}

}  // namespace code
}  // namespace granary
