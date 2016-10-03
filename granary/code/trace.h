/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_TRACE_H_
#define GRANARY_CODE_TRACE_H_

#include "granary/code/index.h"

namespace granary {

enum : size_t {
  kMaxNumTraceEntries = 32
};

struct TraceEntry {
  index::Key key;
  index::Value val;
};

class TraceRecorder {
 public:
  TraceRecorder(void);

  // Record an entry into a trace.
  bool BlockEndsTrace(index::Key key, index::Value val);

  // Build a trace.
  //
  // Note: This has an architecture-specific implementation.
  //
  // Note: This function is NOT thread-safe.
  void Build(void);

  // Returns true if the trace buffer is empty.
  inline bool IsEmpty(void) const {
    return !trace_length;
  }

 private:
  size_t next_entry;
  size_t trace_length;
  TraceEntry entries[kMaxNumTraceEntries];

  GRANARY_DISALLOW_COPY_AND_ASSIGN(TraceRecorder);
};


}  // namespace granary

#endif  // GRANARY_CODE_TRACE_H_
