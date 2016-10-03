/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/code/trace.h"

namespace granary {

TraceRecorder::TraceRecorder(void)
    : next_entry(0)
    , trace_length(0)
    , entries{} {}

bool TraceRecorder::BlockEndsTrace(index::Key key, index::Value block) {
  auto &entry = entries[next_entry++];
  entry.key = key;
  entry.val = block;

  if (block.ends_with_syscall || block.ends_with_error ||
      !block.has_one_successor || block.is_trace_block ||
      kMaxNumTraceEntries == next_entry) {
    trace_length = next_entry;
    next_entry = 0;
    return true;
  } else {
    return false;
  }
}

}  // namespace granary
