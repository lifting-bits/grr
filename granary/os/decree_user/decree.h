/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_OS_DECREE_USER_DECREE_H_
#define GRANARY_OS_DECREE_USER_DECREE_H_

#define DECREE_EBADF           1
#define DECREE_EFAULT          2
#define DECREE_EINVAL          3
#define DECREE_ENOMEM          4
#define DECREE_ENOSYS          5
#define DECREE_EPIPE           6

#define DECREE_PC eip
#define DECREE_SYSCALL_NR eax
#define DECREE_SYSCALL_RET eax
#define DECREE_SYSCALL_ARG1 ebx
#define DECREE_SYSCALL_ARG2 ecx
#define DECREE_SYSCALL_ARG3 edx
#define DECREE_SYSCALL_ARG4 esi
#define DECREE_SYSCALL_ARG5 edi

#define DECREE_STDIN 0
#define DECREE_STDOUT 1
#define DECREE_STDERR 2

#define __NR__terminate 1
#define __NR_transmit 2
#define __NR_receive 3
#define __NR_fdwait 4
#define __NR_allocate 5
#define __NR_deallocate 6
#define __NR_random 7

#include "granary/os/process.h"

namespace granary {
namespace os {

typedef int32_t decree_fd_mask;

enum : size_t {
  kFdSetSize = 1024,
  kNumFdBits = (8 * sizeof(decree_fd_mask))
};

enum : int {
  kUsecPerSec = 1000000,
  kNsecPerUsec = 1000
};

enum : unsigned {
  kSignedSizeMax = 2147483647U
};

struct decree_fd_set {
  decree_fd_mask _fd_bits[kFdSetSize / kNumFdBits];
};

struct decree_timeval {
  int32_t tv_sec;
  int32_t tv_usec;
};

#define DECREE_FD_SET(b, set) \
  ((set)->_fd_bits[b / _NFDBITS] |= (1 << (b & (_NFDBITS - 1))))
#define DECREE_FD_CLR(b, set) \
  ((set)->_fd_bits[b / kNumFdBits] &= ~(1 << (b & (kNumFdBits - 1))))
#define DECREE_FD_ISSET(b, set) \
  ((set)->_fd_bits[b / kNumFdBits] & (1 << (b & (kNumFdBits - 1))))

enum class SystemCallSelector {
  kInvalid = 0,
  kTerminate = 1,
  kTransmit = 2,
  kReceive = 3,
  kFdwait = 4,
  kAllocate = 5,
  kDeallocate = 6,
  kRandom = 7
};

class SystemCallABI {
 public:
  inline explicit SystemCallABI(os::Process32 *process_)
      : process(process_) {}

  // Pointer type.
  template <size_t kArgNum,
            typename T,
            typename std::enable_if<std::is_pointer<T>::value,int>::type=0>
  inline T Arg(void) const {
    auto ptr = static_cast<uintptr_t>(Arg<kArgNum,Addr32>());

    // Note: We only look for `nullptr` and not for the entire zero page
    //       because that could change the `EFAULT`-return behavior of a
    //       system call by making us avoid trying to read/write from a
    //       pointer because we know it's null.
    if (!ptr) {
      return nullptr;
    }

    auto base = reinterpret_cast<uintptr_t>(process->base);
    return reinterpret_cast<T>(base + ptr);
  }

  void SetReturn(int val) const {
    process->regs.eax = static_cast<uint32_t>(val);
  }

#define ARG_GETTER(n) \
  template <size_t kArgNum, \
            typename T=int, \
            typename std::enable_if<!std::is_pointer<T>::value,int>::type=0, \
            typename std::enable_if<kArgNum==n,int>::type=0> \
  inline T Arg(void) const { \
    return static_cast<T>(process->regs. DECREE_SYSCALL_ARG ## n ); \
  }

  // Assume non-pointer integral type.
  ARG_GETTER(1)
  ARG_GETTER(2)
  ARG_GETTER(3)
  ARG_GETTER(4)
  ARG_GETTER(5)

#undef ARG_GETTER

  inline SystemCallSelector Number(void) const {
    auto nr = process->regs.DECREE_SYSCALL_NR;
    if (__NR__terminate <= nr && __NR_random >= nr) {
      return static_cast<SystemCallSelector>(nr);
    } else {
      return SystemCallSelector::kInvalid;
    }
  }

  os::Process32 *process;

 private:
  SystemCallABI(void) = delete;
};

}  // namespace os
}  // namespace granary

#endif  // GRANARY_OS_DECREE_USER_DECREE_H_
