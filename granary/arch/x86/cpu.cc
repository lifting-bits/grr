/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"
#include "granary/base/breakpoint.h"

namespace granary {
namespace arch {

void Relax(void) {
  GRANARY_INLINE_ASSEMBLY("pause;" ::: "memory");
}

void SerializePipeline(void) {
  GRANARY_INLINE_ASSEMBLY("cpuid;" ::: "eax", "ebx", "ecx", "edx", "memory");
}

}  // namespace arch
}  // namespace granary
