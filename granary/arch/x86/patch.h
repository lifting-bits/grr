/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/arch/patch.h"

#ifndef GRANARY_ARCH_X86_PATCH_H_
#define GRANARY_ARCH_X86_PATCH_H_

namespace granary {
namespace arch {

// Add a new patch point.
void AddPatchPoint(CachePC rel32, AppPC32 target);

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_ARCH_X86_PATCH_H_
