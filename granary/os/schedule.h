/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_OS_SCHEDULE_H_
#define GRANARY_OS_SCHEDULE_H_

#include "granary/os/process.h"

namespace granary {
namespace os {

bool Run(Process32Group processes);

}  // namespace os
}  // namespace granary

#endif  // GRANARY_OS_SCHEDULE_H_
