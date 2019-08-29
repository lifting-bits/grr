/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_OS_PROCESS_H_
#define GRANARY_OS_PROCESS_H_

#include "granary/os/page.h"
#include "granary/os/user.h"

#include <vector>

#include <setjmp.h>

#define _XOPEN_SOURCE
#include <ucontext.h>

#include <sys/types.h>


namespace granary {
namespace os {

enum class ProcessStatus {
  kError,
  kIgnorableError,
  kDone,
  kSystemCall
};

enum class ExecStatus {
  kInvalid,
  kReady,
  kBlocked
};

namespace detail {

template <size_t> struct Value;

template <>
struct Value<1> {
  typedef uint8_t Type;
};

template <>
struct Value<2> {
  typedef uint16_t Type;
};

template <>
struct Value<4> {
  typedef uint32_t Type;
};

template <>
struct Value<8> {
  typedef uint64_t Type;
};

}  // namespace detail

class Snapshot32;
class Process32;

typedef std::vector<os::Process32 *> Process32Group;

extern GRANARY_THREAD_LOCAL(Process32 *) gProcess;

// Represents a single-threaded 32-bit process's state.
class Process32 final {
 public:
  static Process32 *Revive(const Snapshot32 *snapshot);
  ~Process32(void);

  // Converts a 32-bit pointer into a 64-bit pointer.
  inline Addr64 ConvertAddress(Addr32 addr32) const {
    return reinterpret_cast<Addr64>(
        reinterpret_cast<uintptr_t>(base) + addr32);
  }

  // Converts a 64-bit code pointer into a 32-bit pointer.
  inline Addr32 ConvertAddress(Addr64 addr64) const {
    return static_cast<Addr32>(reinterpret_cast<uintptr_t>(addr64) -
                               reinterpret_cast<uintptr_t>(base));
  }

  inline bool IsProcessAddress(uintptr_t addr64) const {
    auto base_addr = reinterpret_cast<uintptr_t>(base);
    return base_addr <= addr64 && addr64 < (base_addr + kProcessSize);
  }

  // Converts a 32-bit code pointer into a 64-bit code pointer.
  inline AppPC64 ConvertPC(AppPC32 pc32) const {
    return reinterpret_cast<AppPC64>(base) + pc32;
  }

  // Returns this process's ID.
  inline pid_t Id(void) const {
    return pid;
  }

  // Returns the next PC to emulate, or the PC of a recent interrupt.
  inline AppPC32 PC(void) const {
    return regs.eip;
  }

  // Returns the PC of the branch instruction.
  inline AppPC32 LastBranchPC(void) const {
    return last_branch_pc;
  }

  // Allocates some memory.
  Addr32 Allocate(size_t num_bytes, PagePerms perms);

  // Frees some memory.
  void Deallocate(Addr32 addr, size_t num_bytes);

  // Invalidates the page hash if the pages associated with a PC can be
  // put into an executable state.
  bool TryMakeExecutable(void);

  // Returns the page hash for this process.
  inline uint32_t PageHash(void) {
    if (GRANARY_LIKELY(0 != page_hash_is_valid)) return page_hash;
    return HashPageRange();
  }

  // Returns true if the page associated with `pc32` is already executable, or
  // can be placed into an executable state.
  bool CanExecute(AppPC32 pc32);

  // Returns true if writing to read-only page should invalidate the page
  // hash and make the page read-write (in the case that the page is RWX).
  bool TryMakeWritable(Addr32 addr32);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"

  // Returns `true` if we're able to write some data to the process.
  template <typename T, typename V,
            typename std::enable_if<sizeof(T) <= 4,int>::type=0>
  inline bool TryWrite(T *ptr, V val) const {
    typedef typename detail::Value<sizeof(T)>::Type U;
    return DoTryWrite((U *) ptr, (U) (T) val);
  }

  template <typename T, typename V,
            typename std::enable_if<4 < sizeof(T),int>::type=0>
  inline bool TryWrite(T *ptr, const T &val) const {
    auto dst = (uint8_t *) ptr;
    auto src = (const uint8_t *) &val;
    for (auto i = 0UL; i < sizeof(T); ++i) {
      if (!DoTryWrite(dst++, *src++)) return false;
    }
    return true;
  }

  // Returns `true` if we're able to read some data from the process into `val`.
  template <typename T, typename std::enable_if<sizeof(T) <= 4,int>::type=0>
  inline bool TryRead(const T *ptr, T &val) const {
    typedef typename detail::Value<sizeof(T)>::Type U;
    return DoTryRead((const U *) ptr, (U *) &val);
  }

  template <typename T, typename std::enable_if<4 < sizeof(T),int>::type=0>
  inline bool TryRead(const T *ptr, T &val) const {
    auto dst = (const uint8_t *) ptr;
    auto src = (uint8_t *) &val;
    for (auto i = 0UL; i < sizeof(T); ++i) {
      if (!DoTryRead(dst++, src++)) return false;
    }
    return true;
  }

#pragma clang diagnostic pop

  void SynchronizeRegState(ucontext_t *context);

  // Returns true if a signal handler can recover from this fault by returning.
  bool RecoverFromTryReadWrite(ucontext_t *context) const;

  // Tries to lazily map the address if it is marked as having this capability.
  bool TryLazyMap(Addr32 addr);

  // Restore the saved FPU state.
  void RestoreFPUState(void) const;

  // Save the current FPU state.
  void SaveFPUState(void);

  const Addr64 base;  // 0

  struct GPRs {
    uint32_t edi;  // 8
    uint32_t esi;  // 12
    uint32_t ebp;  // 16
    uint32_t ebx;  // 20
    uint32_t edx;  // 24
    uint32_t ecx;  // 28
    uint32_t eax;  // 32
    uint32_t esp;  // 36

    AppPC32 eip;  // 40

    uint32_t eflags;  // 44
  } regs;

  // Process ID.
  const pid_t pid;  // 48

  // EIP of most recently executed multi-way branch.
  AppPC32 last_branch_pc;  // 52

  // Base of `.text` section on init.
  AppPC32 text_base;  // 56

 private:
  // True if we can recover from a page fault.
  mutable bool fault_can_recover;  // 60

 public:

  // Amount of schedule ticks to wait.
  int schedule_delay;

  // Last signal delivered to the process.
  int signal;

  ProcessStatus status;
  ExecStatus exec_status;

  // The address that caused us to fault.
  Addr32 fault_addr;
  Addr32 fault_base_addr;
  Addr32 fault_index_addr;

 private:
  friend class Snapshot32;

  explicit Process32(const Snapshot32 *snapshot);
  Process32(void) = delete;

  void InitPages(void);
  void InitSnapshotPages(const Snapshot32 *snapshot);
  void InitRegs(const Snapshot32 *snapshot);

  // Computes the page hash.
  uint32_t HashPageRange(void) const;

  // Tries to do a write of a specific size.
  bool DoTryWrite(uint32_t *ptr, uint32_t val) const;
  bool DoTryWrite(uint16_t *ptr, uint16_t val) const;
  bool DoTryWrite(uint8_t *ptr, uint8_t val) const;

  // Tries to do a read of a specific size.
  bool DoTryRead(const uint32_t *ptr, uint32_t *val) const;
  bool DoTryRead(const uint16_t *ptr, uint16_t *val) const;
  bool DoTryRead(const uint8_t *ptr, uint8_t *val) const;

  // Allocates `num_pages` of memory with permission `perms` at a "variable"
  // address. The address at which the pages are allocated is returned.
  //
  // Note: `num_bytes` must be a multiple of `kPageSize`.
  Addr32 AllocateFromHighMem(size_t num_bytes, PagePerms perms);

  // Tries to change the state of some page ranges from `old_state` to
  // `new_state`.
  bool TryChangeState(Addr32 addr, PageState old_state, PageState new_state);

  // 24-bit hash of the RWX pages.
  mutable uint32_t page_hash;

  // Tells us whether the page hash is valid.
  mutable bool page_hash_is_valid;

  // List of all `mmap`d or `allocate`d pages.
  std::vector<PageRange32> pages;

  // FPU register state.
  alignas(16) struct user_fpregs_struct fpregs;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Process32);
};

// Ensures that the correct `Process32` pointer is set up to handle certain
// kinds of faulting conditions. This is important for things like File
// reading/writing where two processes are involved, as well as some cases of
// page hashing.
class PushProcess32 {
 public:
  explicit inline PushProcess32(const Process32 *curr)
      : prev(gProcess) {
    gProcess = const_cast<Process32 *>(curr);
  }

  inline ~PushProcess32(void) {
    gProcess = prev;
  }

 private:
  PushProcess32(void) = delete;

  Process32 *prev;
};

static_assert(0 == __builtin_offsetof(Process32, base),
              "Invalid structure packing of `os::Process32`.");

static_assert(8 == __builtin_offsetof(Process32, regs.edi),
              "Invalid structure packing of `os::Process32`.");

static_assert(40 == __builtin_offsetof(Process32, regs.eip),
              "Invalid structure packing of `os::Process32`.");

static_assert(44 == __builtin_offsetof(Process32, regs.eflags),
              "Invalid structure packing of `os::Process32`.");

}  // namespace os
}  // namespace granary

#endif  // GRANARY_OS_PROCESS_H_
