/* Copyright 2016 Peter Goodman (peter@trailofbits.com), all rights reserved. */


#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <gflags/gflags.h>

#include <algorithm>
#include <map>
#include <sstream>

#include "granary/os/page.h"

#include "granary/code/instrument.h"
#include "granary/code/coverage.h"

#include "third_party/md5/md5.h"


DECLARE_bool(path_coverage);
DECLARE_string(coverage_file);
DECLARE_string(output_coverage_file);

DEFINE_bool(count_path_executions, true,
            "Count the order of magnitude of number of times each path "
            "is executed.");

namespace granary {

extern "C" size_t gInputIndex;

namespace code {

struct PathEntry {
  Addr32 block_pc_of_last_branch;
  Addr32 block_pc_of_branch;
  Addr32 target_block_pc_of_branch;

  bool operator<(const PathEntry &other) const {
    if (block_pc_of_last_branch < other.block_pc_of_last_branch) {
      return true;
    } else if (block_pc_of_last_branch > other.block_pc_of_last_branch) {
      return false;
    }

    if (block_pc_of_branch < other.block_pc_of_branch) {
      return true;
    } else if (block_pc_of_branch > other.block_pc_of_branch) {
      return false;
    }

    return target_block_pc_of_branch < other.target_block_pc_of_branch;
  }
} __attribute__((packed));

struct CountedPathEntry : public PathEntry {
  uint32_t count;
} __attribute__((packed));

namespace {

static std::map<PathEntry, uint32_t> gAllPaths;
static std::map<PathEntry, uint32_t> gAllPathsAtInit;
static std::map<PathEntry, uint32_t> gCurrPaths;

static bool gHasNewPathCoverage = false;
static bool gInputLengthMarked = false;
static size_t gMarkedInputLength = 0;

enum : size_t {
  kMaxNumBufferedPathEntries = 4096
};

// log2ish(n) = int(log2(n)) + 1
static inline uint32_t log2ish(uint32_t x) {
  return x ? 32U - static_cast<uint32_t>(__builtin_clz(x)) : 0;
}

}  // namespace

extern "C" {

CountedPathEntry gPathEntries[kMaxNumBufferedPathEntries] = {};
unsigned gNextPathEntry = 0;

// Used for path tracing.
extern void CoverPath(void);

// Invoked by assembly
extern void UpdateCoverageSet(void) {
  if (!gNextPathEntry) {
    return;
  }
  gNextPathEntry = 0;
  for (auto &entry : gPathEntries) {
    if (!entry.count) {
      break;
    }

    PathEntry *uncounted_entry = &entry;

    auto &count = gCurrPaths[*uncounted_entry];
    auto &all_count = gAllPaths[*uncounted_entry];
    if (FLAGS_count_path_executions) {
      count += entry.count;
      const auto log_count = log2ish(count);

      // Executed some path an order of magnitude more than before.
      if (all_count < log_count) {
        gHasNewPathCoverage = true;
        all_count = log_count;
      }
    }

    entry = {};
  }
}

}  // extern C

void InitPathCoverage(void) {
  if (!FLAGS_path_coverage) {
    return;
  }

  // Multi-way branches (jCC, call r/m, jmp r/m, and ret) are the only
  // possible input-dependent branches. Direct jumps and calls are ignored
  // because they contribute no new information.
  granary::code::AddInstrumentationFunction(
      granary::code::InstrumentationPoint::kInstrumentMultiWayBranch,
      CoverPath);

  if (FLAGS_coverage_file.empty() ||
      FLAGS_coverage_file == "/dev/null") {
    return;
  }

  GRANARY_IF_ASSERT( errno = 0; )
  auto fd = open64(FLAGS_coverage_file.c_str(),
                   O_RDONLY | O_CLOEXEC | O_CREAT | O_LARGEFILE, 0666);
  GRANARY_ASSERT(!errno && "Unable to open a coverage file.");

  struct stat file_info;
  fstat(fd, &file_info);
  GRANARY_ASSERT(!errno && "Unable to stat the coverage file.");

  auto size = static_cast<size_t>(file_info.st_size);

  // Likely that we just created the coverage file.
  if (!size) {
    close(fd);
    return;
  }

  // Verify that we have the right number of entries.
  GRANARY_ASSERT(0 == (size % sizeof(CountedPathEntry)));

  auto num_entries = size / sizeof(CountedPathEntry);
  for (size_t i = 0; i < num_entries; ) {
    auto bytes_read_ = read(fd, &(gPathEntries[0]), sizeof(gPathEntries));
    GRANARY_ASSERT(!errno && "Unable to read path entries.");

    // Figure out how many entries we've read.
    auto bytes_read = static_cast<size_t>(bytes_read_);
    auto num_entries_read = bytes_read / sizeof(CountedPathEntry);
    GRANARY_ASSERT(num_entries_read < kMaxNumBufferedPathEntries);

    i += num_entries_read;

    // We didn't read a complete number of entries; back us up.
    if (0 != (bytes_read % sizeof(PathEntry))) {
      lseek(fd, static_cast<off_t>(i * sizeof(CountedPathEntry)), SEEK_SET);
      GRANARY_ASSERT(!errno && "Unable to seek to valid path entry.");
    }

    // Process the entries that we have read.
    for (size_t e = 0; e < num_entries_read; ++e) {
      PathEntry *uncounted_entry = &gPathEntries[e];
      gAllPathsAtInit[*uncounted_entry] = gPathEntries[e].count;
      gPathEntries[e] = {};
    }
  }

  close(fd);
}

void BeginPathCoverage(void) {
  gInputLengthMarked = false;
  gMarkedInputLength = 0;
  gNextPathEntry = 0;
  gHasNewPathCoverage = false;
  gCurrPaths.clear();
  gAllPaths = gAllPathsAtInit;
  memset(&(gPathEntries[0]), 0, sizeof(gPathEntries));
}

void EndPathCoverage(void) {
  UpdateCoverageSet();
  MarkCoveredInputLength();
}

bool CoveredNewPaths(void) {
  return FLAGS_path_coverage && gHasNewPathCoverage;
}

void ExitPathCoverage(void) {
  if (!FLAGS_path_coverage) {
    return;
  }

  if (FLAGS_output_coverage_file.empty() ||
      FLAGS_output_coverage_file == "/dev/null") {
    return;
  }

  std::stringstream ss;
  ss << FLAGS_output_coverage_file << "." << getpid();
  auto cov_file = ss.str();

  GRANARY_IF_ASSERT( errno = 0; )
  auto fd = open64(
      cov_file.c_str(),
      O_RDWR | O_CLOEXEC | O_CREAT | O_LARGEFILE | O_TRUNC,
      0666);
  GRANARY_ASSERT(!errno && "Unable to open a coverage file.");

  for (const auto &entry : gAllPaths) {
    if (entry.second) {
      GRANARY_ASSERT(entry.second >= gAllPathsAtInit[entry.first] &&
                     "Invalid path counting!");

      CountedPathEntry counted_entry = {};
      *reinterpret_cast<PathEntry *>(&counted_entry) = entry.first;
      counted_entry.count = entry.second;
      write(fd, &counted_entry, sizeof(counted_entry));
    }
  }

  close(fd);
  rename(cov_file.c_str(), FLAGS_output_coverage_file.c_str());
}

std::string PathCoverageHash(void) {
  MD5 hash;
  for (const auto &entry : gCurrPaths) {
    if (entry.second) {
      CountedPathEntry counted_entry = {};
      *reinterpret_cast<PathEntry *>(&counted_entry) = entry.first;
      counted_entry.count = log2ish(entry.second);
      hash.update(reinterpret_cast<const char *>(&counted_entry),
                  sizeof(counted_entry));
    }
  }

  hash.finalize();
  return hash.hexdigest();
}

void MarkCoveredInputLength(void) {
  if (FLAGS_path_coverage && gHasNewPathCoverage && !gInputLengthMarked) {
    GRANARY_ASSERT(gInputIndex &&
                   "Cannot cover new code without reading inputs.");
    gMarkedInputLength = gInputIndex;
    gInputLengthMarked = true;
  }
}

size_t GetCoveredInputLength(void) {
  GRANARY_ASSERT(
      gHasNewPathCoverage && gInputLengthMarked && gMarkedInputLength &&
      "Can't get coverage index for no new coverage.");
  return gMarkedInputLength;
}

size_t GetNumCoveredPaths(void) {
  return gCurrPaths.size();
}

}  // namespace code
}  // namespace granary
