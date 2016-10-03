/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"

#ifndef GRANARY_ARCH_PATCH_H_
#define GRANARY_ARCH_PATCH_H_

namespace granary {
namespace arch {

void InitPatcher(void);
void ExitPatcher(void);

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_ARCH_PATCH_H_
