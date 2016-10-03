/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_INDEX_H_
#define GRANARY_CODE_INDEX_H_

#include "granary/base/base.h"

#include "granary/os/process.h"

namespace granary {
namespace index {

union Key {
  inline Key(void)
      : key(0ULL) {}

  inline Key(os::Process32 *process, AppPC32 pc)
      : pc32(pc),
        pid(process->Id()),
        code_hash(process->PageHash()) {}

  inline operator bool(void) const {
    return 0 != key;
  }

  inline bool operator==(const Key &that) const {
    return key == that.key;
  }

  inline bool operator!=(const Key &that) const {
    return key != that.key;
  }

  uint64_t key;

  struct {
    // The 32-bit program counter in the process associated with `pid` that
    // begins this block.
    AppPC32 pc32;

    // The ID of this binary. We will often put blocks from multiple binaries
    // into a single code cache.
    pid_t pid:8;

    // Hash of all executable pages.
    uint32_t code_hash:24;

  } __attribute__((packed));
};

union Value {
  inline Value(void)
      : value(0ULL) {}

  inline operator bool(void) const {
    return 0 != value;
  }

  inline bool IsTraceHead(void) const {
    return is_trace_head;
  }

  uint64_t value;

  struct {
    // A non-unique block ID that includes part of the AppPC32 and the PID.
    uint32_t block_pc32;

    // Offset of the translated block within the code cache.
    CacheOffset cache_offset:27;

    // Is this the first block in a trace?
    bool is_trace_head:1;

    // Is this block part of a trace?
    bool is_trace_block:1;

    // Does this block have only a single successor?
    bool has_one_successor:1;

    // Does this block end with a system call?
    bool ends_with_syscall:1;

    // Does this block end in an undefined instruction? This signals one of a
    // few conditions:
    //    1)  Failure to decode an instruction.
    //    2)  Instruction crosses from an executable into a non-executable
    //        page.
    //    3)  Instruction is in an unmapped or unreadable page.
    //    4)  Emulated code actually contained UD2 or other undefined
    //        instruction.
    bool ends_with_error:1;
  } __attribute__((packed));
};

static_assert(sizeof(Key) <= sizeof(uint64_t),
              "Invalid structure packing of `IndexKey`.");

static_assert(sizeof(Value) <= sizeof(uint64_t),
              "Invalid structure packing of `IndexKey`.");

// Initialize the code cache index.
void Init(void);

// Exit the code cache index.
void Exit(void);

// Print out all entries in the code cache index.
void Dump(void);

// Finds a value in the index given a key.
Value Find(const Key search_key);

// Inserts a (key, value) pair into the index.
void Insert(const Key key, Value value);

}  // namespace index
}  // namespace granary

#endif  // GRANARY_CODE_INDEX_H_
