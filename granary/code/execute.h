/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_EXECUTE_H_
#define GRANARY_EXECUTE_H_

#include "granary/os/schedule.h"

namespace granary {
namespace code {

// Main interpreter loop. This function handles index lookup, block translation,
// trace building, dispatching, and system call handling.
void Execute(os::Process32 *process);

}  // namespace code
}  // namespace granary

#endif  // GRANARY_EXECUTE_H_
