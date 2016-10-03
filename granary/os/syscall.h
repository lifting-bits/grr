/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_OS_SYSCALL_H_
#define GRANARY_OS_SYSCALL_H_

#include "granary/os/file.h"

namespace granary {
namespace os {

enum class SystemCallStatus {
  kTerminated,
  kComplete,
  kInProgress,
  kSleeping
};

SystemCallStatus SystemCall(Process32 *process, FileTable &files);

}  // namespace os
}  // namespace granary

#endif  // GRANARY_OS_SYSCALL_H_
