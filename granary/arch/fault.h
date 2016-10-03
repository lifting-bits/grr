/* Copyright 2016 Peter Goodman (peter@trailofbits.com), all rights reserved. */

#ifndef GRANARY_ARCH_FAULT_H_
#define GRANARY_ARCH_FAULT_H_

#include "granary/base/base.h"

namespace granary {
namespace os {
class Process32;
}  // namespace os
namespace arch {

void DecomposeFaultAddr(
    const os::Process32 *process,
    Addr32 *base,
    Addr32 *index,
    Addr32 *scale,
    Addr32 *disp,
    Addr32 fault_addr);

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_ARCH_FAULT_H_
