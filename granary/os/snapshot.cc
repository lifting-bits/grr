/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/os/snapshot.h"

#include <sys/mman.h>
#include <sys/unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace granary {
namespace os {
namespace detail {

// Returns the page permissions for this mapped range.
PagePerms MappedRange32::Perms(void) const {
  if (is_w && is_x) {
    return PagePerms::kRWX;
  } else if (is_x) {
    return PagePerms::kRX;
  } else if (is_w) {
    return PagePerms::kRW;
  } else {
    return PagePerms::kRO;
  }
}

// Copies some snapshotted memory (stored in the file `snapshot_fd` to `mem`).
void MappedRange32::CopyFromFileIntoMem(int snapshot_fd, void *mem,
                                        PageState state) const {
  auto mem_base = reinterpret_cast<uintptr_t>(mem);
  mem = reinterpret_cast<void *>(mem_base + begin);

  auto prot = 0;
  switch (state) {
    case PageState::kReserved: prot = PROT_NONE; break;
    case PageState::kRO: prot = PROT_READ; break;
    case PageState::kRW: prot = PROT_READ | PROT_WRITE; break;
    case PageState::kRX: prot = PROT_READ; break;
  }

  GRANARY_IF_ASSERT( errno = 0; )
  mmap64(mem, Size(), prot, MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE,
         snapshot_fd, fd_offs);
  GRANARY_ASSERT(!errno && "Failed to copy snapshotted memory into process.");

  // Deal with lazily allocated pages.
  if (begin < lazy_begin) {
    mprotect(mem, lazy_begin - begin, PROT_NONE);
  }
}

}  // namespace detail

// Tears down the snapshot.
Snapshot32::~Snapshot32(void) {
  munmap(const_cast<detail::Snapshot32File *>(file), sizeof *file);
  close(fd);
}

// Revives a previously created snapshot file.
Snapshot32 *Snapshot32::Revive(int exe_num) {
  return new Snapshot32(exe_num);
}

}  // namespace os
}  // namespace granary
