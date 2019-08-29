/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/code/cache.h"
#include "granary/code/index.h"

#include "granary/arch/instrument.h"

#include "granary/os/page.h"

#include <gflags/gflags.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef MAP_POPULATE
# define MAP_POPULATE 0
#endif

#ifndef O_LARGEFILE
# define O_LARGEFILE 0
#endif

DECLARE_bool(persist);
DECLARE_string(persist_dir);

namespace granary {
namespace {
enum : size_t {
  k250MiB = 1ULL << 28ULL
};
}  // namespace
namespace cache {
namespace {
enum : unsigned {
  kProbesPerEntry = 4,
  kNumEntries = 2048,
  kNumCacheSlots = kNumEntries + kProbesPerEntry,
};

struct CacheEntry {
  AppPC32 app_pc;
  uint32_t cache_pc_disp_from_inline_cache;
};

extern "C" {

CacheEntry gInlineCache[kNumCacheSlots];

void *gCacheAddr = nullptr;

}  // extern C

static uint8_t gNextInlineCacheEntry[kNumEntries] = {0};

// Beginning of the memory mapping for the code cache.
static void *gBegin = nullptr;
static void *gBeginSync = nullptr;
static void *gEnd = nullptr;

// The beginning of the code cache, as a `CachePC`.
static CachePC gBeginSyncPC = nullptr;

// The next code cache location that can be allocated.
static CachePC gNextBlockPC = nullptr;

// Path to the persisted cache file.
static char gCachePath[256] = {'\0'};

// File descriptor for the code cache.
static int gFd = -1;

// Flags for `mmap`.
static int gMMapFlags = MAP_FIXED | MAP_POPULATE | MAP_SHARED;

// Size (in bytes) of the code cache.
static size_t gCacheSize = 0;

// Should the cache be synchronized with the file system?
static bool gSyncCache = false;

// Is the inline cache empty? This lets us avoid redundant inline cache flushes.
static bool kCacheIsEmpty = true;

// Returns the size of the existing cache.
static size_t ExistingCacheSize(void) {
  if (!FLAGS_persist) return 0;
  struct stat info;
  fstat(gFd, &info);
  return static_cast<size_t>(info.st_size);
}

// Adds a new page to the end of the code cache.
static void ResizeCache(void) {
  off_t offset = 0;
  if (FLAGS_persist) {
    GRANARY_IF_ASSERT( errno = 0; )
    ftruncate(gFd, static_cast<off_t>(gCacheSize + os::kPageSize));
    GRANARY_ASSERT(!errno && "Unable to resize code cache.");

    offset = static_cast<off_t>(gCacheSize);
  }

  GRANARY_IF_ASSERT( errno = 0; )
  mmap(gEnd, os::kPageSize, PROT_READ | PROT_WRITE | PROT_EXEC, gMMapFlags,
       gFd, offset);
  GRANARY_ASSERT(!errno && "Unable to map new end of code cache.");

  gCacheSize += os::kPageSize;
  gEnd = gBeginSyncPC + gCacheSize;
}

static void InitInstrumentation(void) {
  GRANARY_IF_ASSERT( errno = 0; )
  mmap(gBegin, os::kPageSize, PROT_READ | PROT_WRITE,
       MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  GRANARY_ASSERT(!errno && "Unable to map instrumentation page.");

  arch::InitInstrumentationFunctions(reinterpret_cast<CachePC>(gBegin));

  GRANARY_IF_ASSERT( errno = 0; )
  mprotect(gBegin, os::kPageSize, PROT_READ | PROT_EXEC);
  GRANARY_ASSERT(!errno && "Unable to write-protect instrumentation page.");
}

}  // namespace

// Insert into the LRU cache. The cache is accessed from within the assembly
// in `cache.S`.
void InsertIntoInlineCache(const os::Process32 *process, index::Key key,
                           index::Value block) {
  kCacheIsEmpty = false;
  auto offset = process->last_branch_pc % kNumEntries;
  auto probe = (gNextInlineCacheEntry[offset]++) % kProbesPerEntry;
  auto &entry = gInlineCache[offset + probe];
  auto cache_pc = reinterpret_cast<intptr_t>(gBeginSyncPC) + block.cache_offset;
  auto first_entry = reinterpret_cast<intptr_t>(&(gInlineCache[0]));
  entry.app_pc = key.pc32;
  entry.cache_pc_disp_from_inline_cache = static_cast<uint32_t>(
      cache_pc - first_entry);
}

void ClearInlineCache(void) {
  if (!kCacheIsEmpty) {
    memset(gInlineCache, 0, sizeof gInlineCache);
    kCacheIsEmpty = true;
  }
}

// Initialize the code cache.
void Init(void) {
  if (FLAGS_persist) {
    sprintf(gCachePath, "%s/grr.cache.persist", FLAGS_persist_dir.c_str());
    GRANARY_IF_ASSERT( errno = 0; )
    gFd = open(gCachePath, O_RDWR | O_CLOEXEC | O_CREAT | O_LARGEFILE, 0666);
    GRANARY_ASSERT(!errno && "Unable to open persisted code cache file.");
  } else {
    gMMapFlags = MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS;
  }

  GRANARY_IF_ASSERT( errno = 0; )

  const auto begin_loc = (reinterpret_cast<uintptr_t>(&Init) + k250MiB) & ~4095ULL;
  gBegin = mmap(reinterpret_cast<void *>(begin_loc), k250MiB, PROT_NONE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED,
                -1, 0);
  GRANARY_ASSERT(!errno && "Unable to map address space for code cache.");

  // The first page of the code cache is for instrumentation.
  InitInstrumentation();

  gBeginSyncPC = reinterpret_cast<CachePC>(gBegin) + os::kPageSize;
  gBeginSync = gBeginSyncPC;
  gEnd = gBeginSyncPC;
  gNextBlockPC = gBeginSyncPC;

  gCacheAddr = gBeginSync;

  if ((gCacheSize = ExistingCacheSize())) {
    GRANARY_DEBUG( std::cerr << "Reviving cache file." << std::endl; )

    auto scaled_cache_size = (gCacheSize + (os::kPageSize - 1)) & os::kPageMask;

    // Scale the cache file out to a multiple of the page size so that we can
    // mmap it.
    GRANARY_IF_ASSERT( errno = 0; )
    ftruncate(gFd, static_cast<off_t>(scaled_cache_size));
    GRANARY_ASSERT(!errno && "Unable to scale the code cache file.");

    // Bring the old cache into memory, although scale it out. We'll keep as
    // many of the original cache pages non-writable, just in case this helps
    // us to catch spurious bugs.
    GRANARY_IF_ASSERT( errno = 0; )
    mmap(gBeginSync, scaled_cache_size, PROT_READ | PROT_WRITE | PROT_EXEC,
           gMMapFlags, gFd, 0);
    GRANARY_ASSERT(!errno && "Unable to map the scaled code cache file.");

    gNextBlockPC += gCacheSize;
    gCacheSize = scaled_cache_size;
    gEnd = gBeginSyncPC + scaled_cache_size;
  }
}

void Exit(void) {
  if (!FLAGS_persist || !gSyncCache) return;
  auto actual_cache_size = gNextBlockPC - gBeginSyncPC;
  msync(gBeginSync, gCacheSize, MS_SYNC | MS_INVALIDATE);
  munmap(gBegin, k250MiB);
  ftruncate(gFd, actual_cache_size);
  close(gFd);
}

// Allocate `num_bytes` of space from the code cache.
CachePC Allocate(size_t num_bytes) {
  gSyncCache = true;
  auto ret = gNextBlockPC;
  gNextBlockPC += num_bytes;
  if (GRANARY_UNLIKELY(gNextBlockPC > gEnd)) ResizeCache();
  return ret;
}

// Returns true if the PC is inside the code cache.
bool IsCachePC(uintptr_t pc) {
  auto begin = reinterpret_cast<uintptr_t>(gBeginSyncPC);
  return pc >= begin && pc < (begin + gCacheSize);
}

// Returns the offset of some code within the code cache.
CacheOffset PCToOffset(CachePC pc) {
  return static_cast<CacheOffset>(pc - gBeginSyncPC);
}

// Returns the program counter associated with some offset within the
// code cache.
CachePC OffsetToPC(CacheOffset offset) {
  return gBeginSyncPC + offset;
}

}  // namespace cache
}  // namespace granary
