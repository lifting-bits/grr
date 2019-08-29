/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/code/index.h"

#include <unordered_map>

#include <gflags/gflags.h>

#include <cerrno>
#include <iostream>

#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "../../third_party/xxhash/xxhash.h"

#ifndef O_LARGEFILE
# define O_LARGEFILE 0
#endif

#ifndef MAP_POPULATE
# define MAP_POPULATE 0
#endif

#ifdef __APPLE__
typedef off_t off64_t;
#endif

DECLARE_bool(persist);
DECLARE_string(persist_dir);

namespace granary {
namespace index {
namespace {

enum : uint64_t {
  kMaxNumProbes = 6,
  kMinNumEntries = 4
};

struct Entry {
  Key key;
  Value val;
};

struct Hash {
  size_t operator()(const Key key) const {
    XXH64_state_t state;
    XXH64_reset(&state, 0xff51afd7ed558ccdULL);
    XXH64_update(&state, &key, sizeof key);
    return XXH64_digest(&state);
  }
};

// Pointer to the currently active index.
static std::unordered_map<Key, Value, Hash> gTable;

// Path to the persisted cache file.
static char gIndexPath[256] = {'\0'};

// Was there an old index file before this one?
static bool gOldIndexExists = false;

// Should the index be synced with the file system?
static bool gSyncIndex = false;

// Opens or re-opens the backing file for the index.
static void ReviveCache(void) {
  if (!FLAGS_persist) return;
  GRANARY_IF_ASSERT( errno = 0; )
  auto fd = open(gIndexPath, O_CREAT | O_RDWR | O_CLOEXEC | O_LARGEFILE, 0666);
  GRANARY_ASSERT(!errno && "Unable to open persisted code cache index file.");

  struct stat info;
  fstat(fd, &info);
  GRANARY_ASSERT(!errno && "Could stat code cache index file.");

  // Existing file is empty; nothing to revive.
  if (!info.st_size) {
    close(fd);
    return;
  }

  GRANARY_DEBUG( std::cerr << "Reviving index file." << std::endl; )

  gOldIndexExists = true;

  auto size = static_cast<size_t>(info.st_size);
  auto num_entries = size / sizeof(Entry);

  auto scaled_size = (size + os::kPageSize - 1) & os::kPageMask;
  if (scaled_size > size) {
    ftruncate(fd, static_cast<off64_t>(scaled_size));
    GRANARY_ASSERT(!errno && "Could not scale code cache index file.");
  }
  auto ret = mmap(nullptr, scaled_size, PROT_READ,
                  MAP_PRIVATE | MAP_POPULATE, fd, 0);
  GRANARY_ASSERT(!errno && "Could not map code cache index file.");

  gTable.reserve(num_entries);
  auto entry = reinterpret_cast<const Entry *>(ret);
  for (auto i = 0UL; i < num_entries; ++i, ++entry) {
    gTable[entry->key] = entry->val;
  }

  munmap(ret, scaled_size);
  close(fd);
}

}  // namespace

// Initialize the code cache index.
void Init(void) {
  if (!FLAGS_persist) return;
  sprintf(gIndexPath, "%s/grr.index.persist", FLAGS_persist_dir.c_str());
  ReviveCache();
}

// Exit the code cache index.
void Exit(void) {
  if (!FLAGS_persist || !gSyncIndex) return;

  GRANARY_IF_ASSERT( errno = 0; )
  auto fd = open(gIndexPath, O_CREAT | O_RDWR | O_CLOEXEC | O_LARGEFILE, 0666);
  GRANARY_ASSERT(!errno && "Unable to open persisted code cache index file.");

  if (gOldIndexExists) {
    ftruncate(fd, 0);
    GRANARY_ASSERT(!errno && "Could not clear stale code cache index file.");
  }

  auto size = sizeof(Entry) * gTable.size();
  auto scaled_size = (size + os::kPageSize - 1) & os::kPageMask;

  ftruncate(fd, static_cast<off64_t>(scaled_size));
  GRANARY_ASSERT(!errno && "Could not scale new code cache index file.");

  auto ret = mmap(nullptr, scaled_size, PROT_READ | PROT_WRITE,
                  MAP_SHARED, fd, 0);
  GRANARY_ASSERT(!errno && "Could not map new code cache index file.");

  // Serialize.
  auto entry = reinterpret_cast<Entry *>(ret);
  for (const auto &key_val : gTable) {
    if (key_val.second) *entry++ = {key_val.first, key_val.second};
  }

  // Persist.
  msync(ret, scaled_size, MS_SYNC | MS_INVALIDATE);
  munmap(ret, scaled_size);

  // Truncate.
  if (size < scaled_size) {
    ftruncate(fd, static_cast<off64_t>(size));
    GRANARY_ASSERT(!errno && "Could resize new code cache index file.");
  }
}

// Print out all entries in the code cache index.
void Dump(void) {
  for (const auto &entry : gTable) {
    auto pc = entry.first.pc32;
    auto pid = entry.first.pid;
    std::cout << std::dec << pid << " " << std::hex << pc << std::endl;
  }
}

// Finds a value in the index given a key.
Value Find(const Key key) {
  return gTable[key];  // If not there, this will create space.
}

// Inserts a (key, value) pair into the index.
void Insert(Key key, Value value) {
  gSyncIndex = true;
  gTable[key] = value;
}

}  // namespace index
}  // namespace granary
