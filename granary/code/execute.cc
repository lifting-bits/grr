/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/code/execute.h"

#include "granary/code/block.h"
#include "granary/code/cache.h"
#include "granary/code/index.h"
#include "granary/code/trace.h"

#include "granary/os/process.h"

#include <gflags/gflags.h>

#include <iostream>
#include <iomanip>

DEFINE_bool(disable_tracing, false, "Disable building superblocks.");
DEFINE_bool(disable_inline_cache, false, "Disable the inline cache.");
DEFINE_bool(debug_print_executions, false, "Print all block executions.");
DEFINE_bool(debug_print_pcs, false, "Print PCs executed by the program.");

namespace granary {
namespace code {
namespace {

// Translate a block.
static index::Value Translate(os::Process32 *process, index::Key key) {
  Block block;

  // Safely reads instruction bytes into memory. This only reads bytes that are
  // both executable and readable.
  block.Decode(key.pc32, [=] (Addr32 pc32, uint8_t &byte) -> bool {
    auto pc64 = process->ConvertPC(pc32);
    if (!process->TryRead(pc64, byte)) return false;
    if (!process->CanExecute(pc32)) return false;
    return true;
  });

  index::Value val;
  val.block_pc32 = key.pc32;
  block.Encode(val);
  return val;
}

}  // namespace

// Main interpreter loop. This function handles index lookup, block translation,
// trace building, and dispatching.
//
// Note: We need to be very careful with functions that might modify global
//       state. All such functions *must* be wrapped in code that disables
//       interrupts.
__attribute__((noinline))
void Execute(os::Process32 *process) {
  TraceRecorder trace;
  for (;;) {
    index::Key key;
    index::Value block;

    // Try to find the block to execute. If we don't find it, then we
    // probably haven't translated it for this hash. Given that we likely
    // need to translate something, go and check that the current and
    // potentially the next pages (because a block can cross at most two
    // pages) are both hashed, otherwise invalidate the hash and re-do
    // the lookup.
    for (;;) {
      Uninterruptible disable_interrupts;
      key = index::Key(process, process->PC());  // Might hash page ranges.
      block = index::Find(key);
      if (GRANARY_LIKELY(block)) {
        break;

      } else if (GRANARY_UNLIKELY(process->TryMakeExecutable())) {
        continue;

      } else {
        block = Translate(process, key);
        index::Insert(key, block);
        cache::ClearInlineCache();
        break;
      }
    }

    // TODO(pag): Why does this need to go before trace building, and why
    //            do I need to check that the target block doesn't end with a
    //            system call or error?
    if (!FLAGS_disable_inline_cache &&
        !block.ends_with_error &&
        !block.ends_with_syscall) {
      cache::InsertIntoInlineCache(process, key, block);
    }

    // If we can't extend the trace, then build the trace block.
    if (!FLAGS_disable_tracing && trace.BlockEndsTrace(key, block)) {
      Uninterruptible disable_interrupts;
      trace.Build();
    }

    if (FLAGS_debug_print_executions) {
      std::cerr << process->Id() << std::hex
                << " EIP=" << process->PC()
                << " EAX=" << process->regs.eax
                << " EBX=" << process->regs.ebx
                << " ECX=" << process->regs.ecx
                << " EDX=" << process->regs.edx
                << " EDI=" << process->regs.edi
                << " ESI=" << process->regs.esi
                << " EBP=" << process->regs.ebp
                << " ESP=" << process->regs.esp
                << std::dec << std::endl;
    }

    if (FLAGS_debug_print_pcs) {
      std::cerr << std::hex << process->PC() << std::endl;
    }

    process->signal = 0;

    // Call into the code cache. This returns the `index::Value` of the last
    // block executed. This is important in the case of traces and persistent
    // caches, where the jumps might be hot-patched, thus leading to syscalls.
    process->RestoreFPUState();
    block = cache::Call(process, cache::OffsetToPC(block.cache_offset));
    process->SaveFPUState();

    // At the time of translating the block, we determined that the block
    // ended in either an invalid instruction, or crossed into a non-
    // executable page. We execute all instructions up to that point, then
    // flag the error.
    //
    // The other way of getting here is via a fault that redirected execution
    // within `cache::Call` to go to `granary_bad_block`, which cleanly
    // returns from the code cache.
    if (block.ends_with_error) {
      if (SIGFPE == process->signal) {
        process->status = os::ProcessStatus::kIgnorableError;
      } else {
        process->status = os::ProcessStatus::kError;
      }
      if (!process->signal) {
        process->signal = SIGILL;  // Decode-time deduction.
      }
      process->exec_status = os::ExecStatus::kInvalid;
      return;
    }

    // Go execute a system call.
    if (block.ends_with_syscall) {
      process->status = os::ProcessStatus::kSystemCall;
      process->exec_status = os::ExecStatus::kReady;
      return;
    }
  }

  // Shouldn't be reachable.
  GRANARY_ASSERT(false && "Fell off the end of the interpreter!");
  process->status = os::ProcessStatus::kError;
  process->exec_status = os::ExecStatus::kInvalid;
}

}  // namespace code
}  // namespace granary
