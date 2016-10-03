/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_CACHE_H_
#define GRANARY_CODE_CACHE_H_

#include "granary/code/index.h"

namespace granary {
namespace os {
class Process32;
}  // namespace os

namespace cache {

// Insert into the LRU cache. The cache is accessed from within the assembly
// in `cache.S`.
void InsertIntoInlineCache(const os::Process32 *process, index::Key key,
                           index::Value value);

// Clear the inline cache.
void ClearInlineCache(void);

// Calls into the code cache and returns a continuation.
//
// Note: This function is defined in assembly.
index::Value Call(os::Process32 *process, CachePC cache_pc);

// Initialize the code cache.
void Init(void);

// Tear down the code cache.
void Exit(void);

// Allocate `num_bytes` of space from the code cache.
//
// Note: This function is NOT thread-safe.
CachePC Allocate(size_t num_bytes);

// Returns true if the PC is inside the code cache.
bool IsCachePC(uintptr_t pc);

inline static bool IsCachePC(intptr_t pc) {
  return IsCachePC(static_cast<uintptr_t>(pc));
}

// Returns the offset of some code within the code cache.
CacheOffset PCToOffset(CachePC pc);

// Returns the program counter associated with some offset within the
// code cache.
CachePC OffsetToPC(CacheOffset offset);

}  // namespace cache
}  // namespace granary

#endif  // GRANARY_CODE_CACHE_H_
