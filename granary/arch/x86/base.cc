/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/arch/base.h"
#include "granary/arch/patch.h"
#include "granary/arch/x86/xed-intel64.h"

namespace granary {
namespace arch {

void Init(void) {
  xed_tables_init();
  InitPatcher();
}

void Exit(void) {
  ExitPatcher();
}

}  // namespace arch
}  // namespace granary
