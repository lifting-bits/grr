/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_OS_SNAPSHOT_H_
#define GRANARY_OS_SNAPSHOT_H_

#include "granary/os/page.h"
#include "granary/os/user.h"

#include <vector>
#include <string>

#include <sys/types.h>
#include <sys/user.h>

namespace granary {
namespace os {

class Process32;

namespace detail {

// A range of mapped memory.
struct MappedRange32 final {
 public:
  inline size_t Size(void) const {
    return end - begin;
  }

  // Returns the page permissions for this mapped range.
  PagePerms Perms(void) const;

  // Copies some snapshotted memory to `mem`.
  void CopyFromFileIntoMem(int snapshot_fd, void *mem, PageState state) const;

  uint32_t fd_offs;
  uint32_t begin;
  uint32_t end;
  uint32_t lazy_begin;
  bool is_r;
  bool is_w;
  bool is_x;
  uint8_t padding;
} __attribute__((packed));

// On-disk layout of a snapshot file.
struct alignas(kPageSize) Snapshot32File final {
 public:
  struct Meta {
    struct {
      char magic[4];
      int exe_num;
    } __attribute__((packed));
    struct user_regs_struct gregs;
    struct user_fpregs_struct fpregs;
  };

  Meta meta;

  enum {
    kNumPages = 4,
    kNumBytes = kNumPages * os::kPageSize,

    kMaxNumMappedRanges = (kNumBytes - sizeof(Meta)) / sizeof(MappedRange32)
  };

  MappedRange32 ranges[kMaxNumMappedRanges];
};

}  // namespace detail

class Snapshot32 final {
 public:
  ~Snapshot32(void);

  // Revives a previously created snapshot file.
  static Snapshot32 *Revive(int exe_num);

  // Creates the snapshot file.
  static void Create(const char *exe_name, int exe_num);

  // Creates the snapshot from a process.
  static void Create(const Process32 *);

 private:
  friend class Process32;

  int fd;
  int exe_num;
  const detail::Snapshot32File *file;

  explicit Snapshot32(int exe_num_);
  Snapshot32(void) = delete;
};

typedef std::vector<os::Snapshot32 *> SnapshotGroup;

}  // namespace os
}  // namespace granary

#endif  // GRANARY_OS_SNAPSHOT_H_
