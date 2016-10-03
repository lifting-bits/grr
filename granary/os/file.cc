/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/os/file.h"

#include "granary/os/process.h"

#include <iostream>

#include <gflags/gflags.h>

DECLARE_bool(strace);

namespace granary {
namespace os {

File::File(void) {
  memset(this, 0, sizeof *this);
}

// Emulated behavior:
//  - Readers can under read.
//  - Writers block if their write exceeds the max buffer size.
//  - Writers block if the write would case the buffer to fill.

// Read from this file.
FileIOStatus File::Read(Process32 *process, uint32_t *num_bytes,
                        uint8_t *buf, uint32_t count, int fd) {
  GRANARY_UNUSED(fd);
  auto status = FileIOStatus::kCompleted;
  auto completed_count = 0U;
  if (count) {
    if (blocked_reader) {
      // Someone else is at the head of the queue.
      if (process != blocked_reader) {
        return FileIOStatus::kInProgress;

      // We're the blocked reader, and no new data is available.
      } else if (reader_head == writer_head) {
        return FileIOStatus::kInProgress;

      } else {
        blocked_reader = nullptr;
      }

    // We want to read, but no data is available.
    } else if (reader_head == writer_head) {
      blocked_reader = process;
      return FileIOStatus::kInProgress;
    }

    auto max_count = std::min<size_t>(writer_head - reader_head, count);
    for (; completed_count < max_count; ++completed_count) {
      auto read_val = buffer[(reader_head + completed_count) % kBufferSize];
      if (!process->TryWrite(buf + completed_count, read_val)) {
        status = FileIOStatus::kFaulted;
        break;
      }
    }
    reader_head += completed_count;
  }

  GRANARY_STRACE( std::cerr << "length=" << completed_count; )
  if (FileIOStatus::kFaulted == status) {
    GRANARY_STRACE( std::cerr << " EFAULT (buf)"; )
  } else if (num_bytes && !process->TryWrite(num_bytes, completed_count)) {
    GRANARY_STRACE( std::cerr << " EFAULT (rx_bytes)"; )
    status = FileIOStatus::kFaulted;
  }

  return status;
}

// Write to this file.
FileIOStatus File::Write(Process32 *process, uint32_t *num_bytes,
                         uint8_t *buf, uint32_t count, int fd) {
  GRANARY_UNUSED(fd);
  auto status = FileIOStatus::kCompleted;
  auto completed_count = 0U;
  if (count) {
    if (blocked_writer) {
      // Someone else is at the head of the queue.
      if (process != blocked_writer) {
        return FileIOStatus::kInProgress;

      // We're the blocked writer, and the buffer is still too full.
      } else if ((writer_head - reader_head + count) > kBufferSize) {
        return FileIOStatus::kInProgress;

      } else {
        blocked_writer_count = 0;
        blocked_writer = nullptr;
      }

    // We want to write, but the buffer is too full.
    } else if ((writer_head - reader_head + count) > kBufferSize) {
      blocked_writer = process;
      blocked_writer_count = count;
      return FileIOStatus::kInProgress;
    }

    for (; completed_count < count; ++completed_count) {
      auto &write_val = buffer[(writer_head + completed_count) % kBufferSize];
      if (!process->TryRead(buf + completed_count, write_val)) {
        status = FileIOStatus::kFaulted;
        break;
      }
    }
    writer_head += completed_count;
  }

  GRANARY_STRACE( std::cerr << "length=" << completed_count; )
  if (FileIOStatus::kFaulted == status) {
    GRANARY_STRACE( std::cerr << " EFAULT (buf)"; )
  } else if (num_bytes && !process->TryWrite(num_bytes, completed_count)) {
    GRANARY_STRACE( std::cerr << " EFAULT (tx_bytes)"; )
    status = FileIOStatus::kFaulted;
  }

  return status;
}

bool File::ReadWillBlock(const Process32 *process) const {
  return reader_head == writer_head ||
         (blocked_reader && blocked_reader != process);
}

bool File::WriteWillBlock(const Process32 *process) const {
  return ((writer_head - reader_head + blocked_writer_count) > kBufferSize) ||
         (blocked_writer && blocked_writer != process);
}

}  // namespace os
}  // namespace granary
