/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_BREAKPOINT_H_
#define GRANARY_BASE_BREAKPOINT_H_

#include "granary/base/base.h"

extern "C" {

extern intptr_t granary_crash_pc;
extern intptr_t granary_crash_addr;

// Note: Not marked as no-return so that compile won't warn us when we actually
//       have some kind of "backup" case in the event of the assertion.
void granary_unreachable(const char *cond, const char *loc);

void granary_curiosity(const char *cond=nullptr);

}  // extern C

#endif  // GRANARY_BASE_BREAKPOINT_H_
