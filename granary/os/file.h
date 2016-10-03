/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_OS_FILE_H_
#define GRANARY_OS_FILE_H_

#include "granary/os/page.h"

#include <vector>

namespace granary {
namespace os {

class Process32;

enum class FileIOStatus {
  kCompleted,  // The I/O operation completed.
  kFaulted,  // The I/O operation failed due to an access violation.

  // The operation is in-progress. The operation must be repeated in order to
  // figure out if it succeeded, failed, or remains in progress.
  kInProgress
};

class File {
 public:
  File(void);
  File(const File &) = default;

  // Read from this file.
  FileIOStatus Read(Process32 *process, uint32_t *num_bytes,
                    uint8_t *buf, uint32_t count, int fd);

  // Write to this file.
  FileIOStatus Write(Process32 *process, uint32_t *num_bytes,
                     uint8_t *buf, uint32_t count, int fd);

  bool ReadWillBlock(const Process32 *process) const;
  bool WriteWillBlock(const Process32 *process) const;

  void Cancel(void);

  enum : size_t {
    kBufferSize = kPageSize * 46UL  // Via trial and error.
  };

 private:
  uint8_t buffer[kBufferSize];
  size_t writer_head;
  size_t reader_head;

  uint32_t blocked_writer_count;
  Process32 *blocked_writer;
  Process32 *blocked_reader;
};

typedef std::vector<File *> FileTable;

}  // namespace os
}  // namespace granary

#endif  // GRANARY_OS_FILE_H_
