/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/code/trace.h"

#include "granary/arch/cpu.h"

#include "granary/code/cache.h"
#include "granary/code/instruction.h"

namespace granary {
namespace arch {
extern const xed_state_t kXEDState64;
}  // namespace arch
namespace {
enum : size_t {
  kRelCallJmpSize = 5
};

static void AddInstr(arch::InstructionStack &stack, const TraceEntry &entry,
                     CachePC next_pc, xed_iclass_enum_t iclass) {
  auto instr = stack.Add();
  auto pc = cache::OffsetToPC(entry.val.cache_offset);
  xed_inst1(instr, arch::kXEDState64, iclass, 64, xed_relbr(pc - next_pc, 32));
}

}  // namespace

void TraceRecorder::Build(void) {
  if (1 >= trace_length) {
    trace_length = 0;
    return;
  }

  cache::ClearInlineCache();

  auto trace_size = trace_length * kRelCallJmpSize;  // 5 bytes per CALL/JMP.
  auto trace_begin = cache::Allocate(trace_size);
  auto trace_end = trace_begin + trace_size;

  arch::InstructionStack stack;
  auto i = trace_length - 1;

  // Create the intermediate trace instructions.
  AddInstr(stack, entries[i], trace_end, XED_ICLASS_JMP);
  for (; i-- > 0; ) {
    trace_end -= kRelCallJmpSize;
    AddInstr(stack, entries[i], trace_end, XED_ICLASS_CALL_NEAR);
  }

  // Encode the trace instructions.
  trace_end = trace_begin;
  for (auto &einstr : stack) {
    einstr.Encode(trace_end);
    trace_end += kRelCallJmpSize;
  }

  arch::SerializePipeline();

  // Update the index with the new values.
  auto last_val = entries[trace_length - 1].val;
  for (i = 0; i < trace_length - 1; ++i) {
    auto key = entries[i].key;
    auto val = entries[i].val;
    val.is_trace_head = (0 == i);
    val.is_trace_block = true;
    val.ends_with_syscall = last_val.ends_with_syscall;
    val.has_one_successor = last_val.has_one_successor;
    val.block_pc32 = last_val.block_pc32;
    val.cache_offset = cache::PCToOffset(trace_begin);
    trace_begin += kRelCallJmpSize;
    index::Insert(key, val);
  }

  trace_length = 0;
}

}  // namespace granary
