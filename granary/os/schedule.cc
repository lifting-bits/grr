/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/os/schedule.h"

#include "granary/base/base.h"

#include "granary/arch/fault.h"

#include "granary/os/file.h"
#include "granary/os/syscall.h"

#include "granary/code/cache.h"
#include "granary/code/coverage.h"
#include "granary/code/execute.h"

#include <iostream>
#include <signal.h>
#include <gflags/gflags.h>

namespace granary {
namespace os {
namespace {
enum : size_t {
  kMaxNumSyscalls = 10485760
};

extern "C" void granary_bad_block(void);

// State that can be restored so that we can recover from `SIGINT` and `SIGTERM`
// and gracefully shut down in a way that persists our runtime state.
static sigjmp_buf gSigTermState;
static auto gSigTermStateValid = false;
static auto gIsRunning = true;

// The signal that terminated `Schedule`, if any.
static auto gSigTermSignal = 0;

// Create the file table.
static FileTable CreateFiles(size_t num_processes) {
  FileTable files;
  auto num_fds = 3 + (1 < num_processes ? num_processes * 2 : 0);
  files.reserve(num_fds);
  files.push_back(nullptr);  // stdin.
  files.push_back(nullptr);  // stdout.
  files.push_back(nullptr);  // stderr.
  if (1 < num_processes) {
    for (auto i = 0UL; i < num_processes; ++i) {
      auto file = new File;
      files.push_back(file);
      files.push_back(file);
    }
  }
  return files;
}

// Delete all open files.
static void DeleteFiles(FileTable files) {
  for (auto fd = 3ULL; fd < files.size(); fd += 2) {
    delete files[fd];
  }
}

// Attaches a signal using `sigaction`.
extern "C" void sys_sigreturn();
typedef void (SignalHandler) (int, siginfo_t *, void *);
static void signal(int signum, SignalHandler *handler) {
  struct sigaction sig;
  sig.sa_sigaction = handler;
  sig.sa_flags = SA_SIGINFO;
#ifndef __APPLE__
  sig.sa_restorer = sys_sigreturn;
#endif
  sigfillset(&(sig.sa_mask));
  sigaction(signum, &sig, nullptr);
}

// Handle termination (via an external or keyboard event).
static void CatchInterrupt(int sig, siginfo_t *, void *) {
  cache::ClearInlineCache();

  // If we are persisting stuff, then we might need to queue the interrupt
  // until such a time where we know that various memory-mapped files are
  // in a consistent state.
  if (!IsInterruptible()) {
    QueueInterrupt(sig);
    return;

  // No idea where we are. We're going to try to just return from wherever
  // we are and hope for the best!
  } else if (!gSigTermStateValid) {
    if (!gSigTermSignal && SIGUSR1 != sig) {
      gSigTermSignal = sig;
    }
    gIsRunning = false;
    return;

  // We're at a point of quiescence, deliver the interrupt.
  } else {
    gSigTermSignal = sig;
    gSigTermStateValid = false;
    siglongjmp(gSigTermState, false);
  }
}

// Handle termination (via an external or keyboard event).
[[noreturn]]
static void CatchNonMaskableInterrupt(int sig, siginfo_t *, void *) {
  cache::ClearInlineCache();
  GRANARY_ASSERT(gSigTermStateValid && "Invalid re-use of `gSigTermState`.");
  if (!gSigTermSignal && SIGUSR1 != sig) gSigTermSignal = sig;
  gSigTermStateValid = false;
  siglongjmp(gSigTermState, false);
}

// Catch a segmentation fault and try to handle it. These can happen for a
// few reasons:
//
//    1)  We are accessing some data in the user process that is protected.
//        This is a recoverable fault.
//    2)  We are writing to some data in the user process that is part of some
//        RWX page, but the current state is RX, so we need to change states
//        to RW. The next change back to RX invalidates the page hash.
//    3)  The emulated user process faults. In this case, we want to catch this
//        fault and break out of the interpreter loop.
//    4)  The emulator itself faults. This is probably a bug.
static void CatchFault(int sig, siginfo_t *si, void *context_) {
  cache::ClearInlineCache();

  auto context = reinterpret_cast<ucontext_t *>(context_);
#ifdef __APPLE__
  auto &pc_ref = context->uc_mcontext->__ss.__rip;
#else
  auto &pc_ref = context->uc_mcontext.gregs[REG_RIP];
#endif

  granary_crash_pc = pc_ref;
  granary_crash_addr = reinterpret_cast<intptr_t>(si->si_addr);

  auto process = os::gProcess;
  GRANARY_ASSERT(process);
  process->signal = sig;

  const auto fault_addr64 = reinterpret_cast<uintptr_t>(si->si_addr);
  const auto fault_rip64 = static_cast<uintptr_t>(pc_ref);

  GRANARY_ASSERT(process->IsProcessAddress(fault_addr64));

  const auto fault_addr32 = process->ConvertAddress(
      reinterpret_cast<Addr64>(fault_addr64));

  // Try to write to a RWX page in an RX state into the RW state, OR try to
  // write to a demand-mapped page.
  if (process->TryLazyMap(fault_addr32) ||
      process->TryMakeWritable(fault_addr32)) {
    return;
  }

  // Try to recover from a `TryWrite` or a `TryRead`.
  //
  // Note: This happens *after* the `TryMakeWritable` because that might
  //       cause us to implicitly recover. For example, if there's RWX data
  //       in the RX state, and we try to write to it from within a syscall,
  //       then we don't want to report an EFAULT when we actually need to
  //       change the page state to RW.
  if (process->RecoverFromTryReadWrite(context)) {
    return;
  }

  // Make sure we faulted within the code cache.
  GRANARY_ASSERT(cache::IsCachePC(fault_rip64));

  // Fault in the emulated code, rather than within a syscall. Pull out the
  // components of the address.
  Addr32 base, index, scale, disp;
  arch::DecomposeFaultAddr(
      process, &base, &index, &scale, &disp, fault_addr32);
  process->fault_addr = fault_addr32;
  process->fault_base_addr = base;
  process->fault_index_addr = index;
  pc_ref = static_cast<decltype(pc_ref + pc_ref)>(
      reinterpret_cast<uintptr_t>(granary_bad_block));

  GRANARY_DEBUG(
      std::cerr
          << "Fault reading [" << std::hex << fault_addr32 << "] = ["
          << std::hex << base
          << " + (" << std::hex << index
          << " * " << std::dec << scale
          << ") + " << std::hex << disp << "]" << std::endl; )
}

// Catch a crash.
static void CatchCrash(int sig, siginfo_t *, void *context_) {
  cache::ClearInlineCache();

  GRANARY_DEBUG( std::cerr << "  CatchCrash " << sig << std::endl; )
  auto process = os::gProcess;
  auto context = reinterpret_cast<ucontext_t *>(context_);
#ifdef __APPLE__
  auto &pc_ref = context->uc_mcontext->__ss.__rip;
#else
  auto &pc_ref = context->uc_mcontext.gregs[REG_RIP];
#endif

  if (!process) {
    granary_crash_pc = pc_ref;
    GRANARY_ASSERT(false && "os::gProcess in CatchCrash is nullptr.");
  }

  process->signal = sig;

  // Try to recover from a `TryWrite` or a `TryRead`.
  if (SIGBUS == sig && process->RecoverFromTryReadWrite(context)) {
    return;
  }

  const auto fault_rip64 = static_cast<uintptr_t>(pc_ref);

  // Make sure we faulted within the code cache.
  GRANARY_ASSERT(cache::IsCachePC(fault_rip64));
  pc_ref = static_cast<decltype(pc_ref + pc_ref)>(
      reinterpret_cast<uintptr_t>(granary_bad_block));
}

static bool gHasSigHandlers = false;

static struct { int sig; SignalHandler *handler; } gSignalHandlers[] = {
  {SIGINT, CatchInterrupt},
  {SIGTERM, CatchInterrupt},
  {SIGALRM, CatchInterrupt},
  {SIGPIPE, CatchInterrupt},
  {SIGUSR1, CatchNonMaskableInterrupt},
  {SIGSEGV, CatchFault},
  {SIGBUS, CatchCrash},
  {SIGFPE, CatchCrash},
#ifdef SIGSTKFLT
  {SIGSTKFLT, CatchCrash},
#endif
  {SIGTRAP, CatchCrash},
  {SIGILL, CatchCrash}
};

// Set up various signal handlers.
static void SetupSignals(void) {
  if (!gHasSigHandlers) {
    gHasSigHandlers = true;
    for (auto &pair : gSignalHandlers) {
      signal(pair.sig, pair.handler);
    }
  }
  sigset_t set;
  sigemptyset(&set);
  sigprocmask(SIG_SETMASK, &set, nullptr);
}

// Perform the actual scheduling of processes.
__attribute__((noinline))
static void Schedule(Process32Group &processes, FileTable &files) {
  Interruptible enable_interrupts;

  // If we were interrupted, then interrupts will be disabled and we'll
  // return from `DoSchedule`.
  if (sigsetjmp(gSigTermState, true)) {
    return;
  }
  gSigTermStateValid = true;

  auto num_syscalls = 0U;
  for (auto made_progress = true; made_progress; ) {
    made_progress = false;

    for (auto process : processes) {

      // The process terminated or crashed.
      if (!process ||
          ProcessStatus::kError == process->status ||
          ProcessStatus::kIgnorableError == process->status ||
          ProcessStatus::kDone == process->status) {
        continue;
      }

      PushProcess32 set_process(process);

      cache::ClearInlineCache();

      // Allows us to repeat system calls that are in-progress. If the
      // status is blocked then we'll try to perform a system call.
      //
      // Note: The initial state of `process_status` is `kSystemCall` even
      //       though it isn't really a system call. So initially we come in
      //       and execute, hopefully up to the first syscall.
      if (ExecStatus::kReady == process->exec_status) {
      continue_execution:
        code::Execute(process);
        made_progress = true;
      }

      // We need to execute a system call.
      if (ProcessStatus::kSystemCall == process->status) {

        // Hard limit on the number of syscalls to avoid OOM conditions.
        if (num_syscalls++ >= kMaxNumSyscalls) {
          made_progress = false;
          break;
        }

        // Disable interrupts; handling system calls modifies global state.
        Uninterruptible disable_interrupts;

        code::MarkCoveredInputLength();

        switch (SystemCall(process, files)) {
          case SystemCallStatus::kTerminated:
            GRANARY_DEBUG( std::cerr << process->Id() << " terminated"
                                     << std::endl; )
            process->status = ProcessStatus::kDone;
            break;

          case SystemCallStatus::kComplete:
            process->exec_status = ExecStatus::kReady;
            made_progress = true;
            if (1 == processes.size() && gIsRunning) {
              goto continue_execution;
            } else {
              break;
            }

          case SystemCallStatus::kInProgress:
            process->exec_status = ExecStatus::kBlocked;
            break;

          case SystemCallStatus::kSleeping:
            process->exec_status = ExecStatus::kBlocked;
            made_progress = true;
            break;
        }

      // Hit some kind of error (e.g. runtime error or decode error) when
      // executing the code.
      } else if (ProcessStatus::kError == process->status) {
        GRANARY_DEBUG( std::cerr << process->Id() << " crashed" << std::endl; )
        return;

      // Unreachable case: this should never happen.
      } else {
        return;
//        GRANARY_ASSERT(false && "Reached ProcessState::kDone directly from "
//                                "execute.");
      }
    }

    // Emulates a `SIGPIPE` if we don't make progress and we haven't already
    // retried.
    if (!made_progress) {
      return;
    }
  }
}

}  // namespace

// The main process scheduler. This is closely tied with the behavior of reads
// ands writes to `File` objects.
bool Run(Process32Group processes) {
  auto files = CreateFiles(processes.size());
  SetupSignals();
  gSigTermStateValid = false;
  memset(gSigTermState, 0, sizeof gSigTermState);
  Schedule(processes, files);
  gSigTermStateValid = false;
  gSigTermSignal = 0;
  os::gProcess = nullptr;
  DeleteFiles(files);
  return 0 != gSigTermSignal;
}

}  // namespace os
}  // namespace granary
