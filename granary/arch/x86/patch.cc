/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/arch/x86/patch.h"

#include "granary/os/process.h"

#include "granary/code/cache.h"
#include "granary/code/index.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include <gflags/gflags.h>

DECLARE_bool(persist);
DECLARE_string(persist_dir);

DEFINE_bool(disable_patching, false,
            "Disable hot-patching of conditional branches?");

namespace granary {
namespace arch {
namespace {

struct PatchPoint {
  CacheOffset patch_offset;
  index::Key target;
};

enum {
  // After how many patch points should we try to patch?
  kPatchInterval = 64,

  // How many patch points should we store?
  kNumPatches = (2 * os::kPageSize) / sizeof(PatchPoint)
};

static_assert(!(kNumPatches % kPatchInterval), "Invalid patch interval.");

// Array of all patch points.
//
// Note: `+ 1` is so that `mmap` won't overflow and corrupt some other memory.
alignas(os::kPageSize) static PatchPoint gPatches[kNumPatches + 1];

// Offset of the next patch point that can be assigned.
static int gNextPatch = 0;

// FD for the patch file.
static int gFd = -1;

// Patch a jump in the code.
//
// Note: `patch_offset` is the offset of an `int32_t` in the code cache that
//       immediately follows the `JMP` opcode.
static void Patch(CacheOffset patch_offset, index::Value target) {
  auto next_pc = cache::OffsetToPC(patch_offset + 4 /* sizeof(CacheOffset) */);
  auto target_pc = cache::OffsetToPC(target.cache_offset);
  auto offset_diff = static_cast<CacheOffset>(target_pc - next_pc);
  auto rel32 = reinterpret_cast<CacheOffset *>(reinterpret_cast<uintptr_t>(
      cache::OffsetToPC(patch_offset)));
  offset_diff = __sync_lock_test_and_set(rel32, offset_diff);
  // It should have had a zero value (e.g. `JMP next_pc`).
  GRANARY_ASSERT(!offset_diff);
}

// Clear all patch points.
static void ClearPatchPoints(void) {
  memset(gPatches, 0, sizeof gPatches);
  gNextPatch = 0;
}


// Type to patch all patch points.
static void PatchCode(void) {
  auto first_free = 0;
  auto last_free = 0;
  auto patched = false;
  for (auto i = 0; i < gNextPatch; ++i) {
    auto &patch = gPatches[i];

    // A patch point that we can patch.
    if (auto val = index::Find(patch.target)) {
      Patch(patch.patch_offset, val);
      memset(&patch, 0, sizeof patch);
      ++last_free;
      patched = true;

    // A patch point that we can't patch yet. Bubble it to a different
    // position.
    } else if (first_free != last_free) {
      memcpy(&(gPatches[first_free]), &patch, sizeof patch);
      memset(&patch, 0, sizeof patch);
      ++first_free;
      ++last_free;

    // A patch point that we can't patch yet. There are free slots available
    // to which we can bubble this
    } else {
      ++first_free;
      ++last_free;
    }
  }
  if (patched) {
    cache::ClearInlineCache();
  }
  gNextPatch = first_free;
}

}  // namespace

// Add a new patch point.
void AddPatchPoint(CachePC rel32, AppPC32 target) {
  if (FLAGS_disable_patching) {
    return;
  }
  if (gNextPatch && !(gNextPatch % kPatchInterval)) {
    PatchCode();
    if (GRANARY_UNLIKELY(kNumPatches == gNextPatch)) ClearPatchPoints();
  }

  auto &patch = gPatches[gNextPatch++];
  patch.patch_offset = cache::PCToOffset(reinterpret_cast<CachePC>(
      reinterpret_cast<uintptr_t>(rel32)));
  patch.target = index::Key(os::gProcess, target);
}

void InitPatcher(void) {
  auto flags = MAP_FIXED | MAP_32BIT;
  if (FLAGS_persist) {

    static char gPath[256] = {'\0'};
    sprintf(gPath, "%s/grr.patch.persist", FLAGS_persist_dir.c_str());

    GRANARY_IF_ASSERT( errno = 0; )
    gFd = open(gPath, O_RDWR | O_CLOEXEC | O_CREAT | O_LARGEFILE, 0666);
    GRANARY_ASSERT(!errno && "Unable to open patch file.");

    ftruncate(gFd, os::kPageSize);
    flags |= MAP_SHARED;

  } else {
    gFd = -1;
    flags |= MAP_PRIVATE | MAP_ANONYMOUS;
  }

  GRANARY_IF_ASSERT( errno = 0; )
  mmap(&gPatches, os::kPageSize, PROT_READ | PROT_WRITE, flags, gFd, 0);
  GRANARY_ASSERT(!errno && "Unable to map patch file.");

  // Advance the patch offset.
  for (; gNextPatch < kNumPatches; ++gNextPatch) {
    if (!gPatches[gNextPatch].target) break;
  }
}

void ExitPatcher(void) {
  if (FLAGS_persist) {
    PatchCode();
    msync(gPatches, os::kPageSize, MS_SYNC | MS_INVALIDATE);
  }

  munmap(gPatches, os::kPageSize);

  if (FLAGS_persist) {
    fsync(gFd);
    close(gFd);
  }
}

}  // namespace arch
}  // namespace granary
