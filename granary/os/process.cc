/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/os/process.h"
#include "granary/os/snapshot.h"

#include <algorithm>
#include <iostream>

#include <errno.h>

#include <sys/mman.h>
#include "../../third_party/xxhash/xxhash.h"

namespace granary {
namespace os {

// The current thread for this process.
GRANARY_THREAD_LOCAL(Process32 *) gProcess = nullptr;

namespace {

// Return the beginning page state associated with the page permissions.
PageState BeginState(PagePerms perms) {
  switch (perms) {
    case PagePerms::kInvalid: return PageState::kReserved;
    case PagePerms::kRO: return PageState::kRO;
    case PagePerms::kRW: return PageState::kRW;
    case PagePerms::kRWX: return PageState::kRW;
    case PagePerms::kRX: return PageState::kRX;
  }
}

// Convert a `PagePerms` into protection flags for `mmap`.
int PermsToProt(PagePerms perms) {
  switch (perms) {
    case PagePerms::kInvalid: return PROT_NONE;
    case PagePerms::kRO: return PROT_READ;
    case PagePerms::kRW: return PROT_READ | PROT_WRITE;
    case PagePerms::kRWX: return PROT_READ | PROT_WRITE;
    case PagePerms::kRX: return PROT_READ | PROT_EXEC;
  }
  GRANARY_ASSERT(false && "Cannot convert permissions into protection flags");
  return PROT_NONE;
}

#if defined(GRANARY_TARGET_debug)

static void DebugRanges(const std::vector<PageRange32> &pages,
                        uint32_t pc, uint32_t sp) {
  std::cerr << std::endl;
  for (const auto &page : pages) {
    std::cerr << "    0x" << std::hex << page.base
              << "-0x" << page.limit << std::dec
              << " perms=";
    switch (page.perms) {
      case PagePerms::kInvalid: std::cerr << "---"; break;
      case PagePerms::kRO: std::cerr << "r--"; break;
      case PagePerms::kRW: std::cerr << "rw-"; break;
      case PagePerms::kRWX: std::cerr << "rwx"; break;
      case PagePerms::kRX: std::cerr << "r-x"; break;
    }
    std::cerr << " state=";
    switch (page.state) {
      case PageState::kReserved: std::cerr << "---"; break;
      case PageState::kRO: std::cerr << "r--"; break;
      case PageState::kRW: std::cerr << "rw-"; break;
      case PageState::kRX: std::cerr << "r-x"; break;
    }
    if (page.base <= pc && pc < page.limit) {
      std::cerr << " PC=0x" << std::hex << pc << std::dec;
    }
    if (page.base <= sp && sp < page.limit) {
      std::cerr << " SP=0x" << std::hex << sp << std::dec;
    }
    if (page.lazy_base != page.base) {
      std::cerr << " LB=0x" << std::hex << page.lazy_base << std::dec;
    }
    std::cerr << std::endl;
  }
  std::cerr << std::endl;
}

#endif

}  // namespace

Process32::Process32(const Snapshot32 *snapshot)
    : base(mmap64(nullptr, kProcessSize, PROT_NONE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0)),
      pid(snapshot->exe_num),
      text_base(kMaxAddress),
      fault_can_recover(false),
      schedule_delay(-1),
      signal(0),
      status(ProcessStatus::kSystemCall),
      exec_status(ExecStatus::kReady),
      fault_addr(0),
      fault_base_addr(0),
      fault_index_addr(0),
      page_hash(0),
      page_hash_is_valid(false),
      pages() {
  pages.reserve(kReserveNumRanges);

  InitRegs(snapshot);
  InitSnapshotPages(snapshot);
  InitPages();

  TryMakeExecutable();
  HashPageRange();

  GRANARY_IF_DEBUG( DebugRanges(pages, regs.eip, regs.esp); )
}

// Initialize the page table.
void Process32::InitPages(void) {
  pages.push_back({0, kPageSize, 0, PagePerms::kInvalid,
                   PageState::kReserved, false, 0});
  pages.push_back({kMaxAddress, kMaxAddress, kMaxAddress,
                   PagePerms::kInvalid,
                   PageState::kReserved, false, 0});
}

// Copy the data and ranges from the snapshot.
void Process32::InitSnapshotPages(const Snapshot32 *snapshot) {
  GRANARY_IF_DEBUG(std::cerr << "Memory maps:" << std::endl; )

  auto file = snapshot->file;
  for (const auto &range : file->ranges) {
    if (!range.end) break;

    auto perms = range.Perms();
    auto state = BeginState(perms);
    GRANARY_ASSERT(range.begin <= range.end && "Invalid snapshot page range.");
    if (range.is_x) {
      if (range.begin <= PC() && range.end > PC()) {
        text_base = range.begin;
        if (range.is_w) {
          state = PageState::kRX;
        }
      }
    }

    pages.push_back({range.begin, range.end, range.lazy_begin,
                     perms, state, false, 0});

    // Copy the actual range into the process; this might be smaller than the
    // demand-mapped range.
    range.CopyFromFileIntoMem(snapshot->fd, base, state);
  }
}

Process32 *Process32::Revive(const Snapshot32 *snapshot) {
  return new Process32(snapshot);
}

Process32::~Process32(void) {
  GRANARY_IF_ASSERT( errno = 0; )
  munmap(base, kProcessSize);
  GRANARY_ASSERT(!errno && "Unable to unmap process address space.");
}

namespace {

// Hash an a page range by trying to hash each individual page in that range.
// We go one page at a time in order to establish whether or not the page is
// readable (and hence hashable).
static void HashRange(const Process32 *process, const PageRange32 &range) {
  XXH32_state_t digest;
  uint8_t byte;
  XXH32_reset(&digest, range.base);
  auto num_pages = (range.limit - range.base) / kPageSize;
  auto addr32 = range.base;
  for (auto i = 0U; i < num_pages; ++i, addr32 += kPageSize) {
    auto addr64 = process->ConvertAddress(addr32);
    if (process->TryRead(reinterpret_cast<const uint8_t *>(addr64), byte)) {
      XXH32_update(&digest, addr64, kPageSize);
    }
  }
  range.hash_is_valid = true;
  range.hash = XXH32_digest(&digest);
}

// Find a page range that contains an address.
static PageRange32 *FindRange(std::vector<PageRange32> &pages, Addr32 addr) {
  for (auto &range : pages) {
    if (range.base <= addr && addr < range.limit) return &range;
  }
  return nullptr;
}

// Check the consistency of page ranges.
static void CheckConsistency(const std::vector<PageRange32> &pages) {
  for (auto page : pages) {
    GRANARY_ASSERT(page.base <= page.limit && "Invalid page range.");
    GRANARY_ASSERT(page.base == (page.base & kPageMask) &&
                   "Invalid page base address.");
    GRANARY_ASSERT(page.limit == (page.limit & kPageMask) &&
                   "Invalid page limit address.");
    GRANARY_ASSERT(page.base <= page.lazy_base && "Invalid lazy base (1).");
    GRANARY_ASSERT(page.limit >= page.lazy_base && "Invalid lazy base (2).");
  }
}

}  // namespace

// Returns true if the page associated with `pc32` is already executable, or
// can be placed into an executable state.
bool Process32::CanExecute(AppPC32 pc32) {
  if (auto range = FindRange(pages, pc32)) {
    if (PageState::kRX == range->state) {
      return true;
    } else if (PagePerms::kRWX == range->perms) {
      return TryChangeState(pc32, PageState::kRW, PageState::kRX);
    } else {
      return false;
    }
  }
  return false;
}

// Returns true if writing to read-only page should invalidate the page
// hash and make the page read-write (in the case that the page is RWX).
bool Process32::TryMakeWritable(Addr32 addr32) {
  return TryChangeState(addr32, PageState::kRX, PageState::kRW);
}

// Invalidates the page hash if the pages associated with a PC can be
// put into an executable state.
bool Process32::TryMakeExecutable(void) {
  return TryChangeState(PC(), PageState::kRW, PageState::kRX);
}

// Tries to lazily map the address if it is marked as having this capability.
bool Process32::TryLazyMap(Addr32 addr32) {
  Addr32 page32 = addr32 & kPageMask;
  auto range = FindRange(pages, page32);
  if (!range || range->lazy_base == range->base) {
    return false;
  }

  GRANARY_ASSERT(range->lazy_base > range->base);
  if ((range->lazy_base - kPageSize) != page32) {
    return false;
  }

  auto prot = PROT_READ;
  if (PageState::kRW == range->state) prot |= PROT_WRITE;

  GRANARY_IF_ASSERT( errno = 0; )
  mprotect(
      reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(base) + page32),
      kPageSize,
      prot);
  GRANARY_ASSERT(!errno && "Unable to lazy-map page.");

  range->lazy_base = page32;
  return true;
}

// Tries to change the state of some page ranges from `old_state` to
// `new_state`.
bool Process32::TryChangeState(Addr32 addr32, PageState old_state,
                               PageState new_state) {

  // Make sure the old state matches up with our expectation.

  const Addr32 base32 = addr32 & kPageMask;
  auto range = FindRange(pages, base32);
  if (!range || range->state != old_state) {
    return false;
  }

  // The address is at the beginning of a page range; modify the range in place.
  if (base32 == range->base) {
    range->state = new_state;
    range->hash = 0;
    range->hash_is_valid = false;
    return true;
  }

  // Okay, go split the range if possible.
  std::vector<PageRange32> new_pages;
  new_pages.reserve(std::max<size_t>(kReserveNumRanges, pages.size() + 1));

  for (const auto &page : pages) {
    if (&page != range) {
      new_pages.push_back(page);
    }
  }

  GRANARY_ASSERT(range->base <= range->lazy_base);
  GRANARY_ASSERT(range->base < base32);
  GRANARY_ASSERT(base32 < range->limit);

  PageRange32 low = {
      range->base, base32, range->base, range->perms, old_state, false, 0};

  PageRange32 high = {
      base32, range->limit, base32, range->perms, new_state, false, 0};

  if (range->base != range->lazy_base) {
    low.lazy_base = std::min(base32, range->lazy_base);
    high.lazy_base = std::max(base32, range->lazy_base);
  }

  GRANARY_ASSERT(low.base <= low.lazy_base && low.lazy_base <= low.limit);
  GRANARY_ASSERT(high.base <= high.lazy_base && high.lazy_base <= high.limit);

  new_pages.push_back(low);
  new_pages.push_back(high);

  auto old_range = *range;
  range = nullptr;
  pages.swap(new_pages);

  CheckConsistency(pages);

  // Invalidate the global page hash. The next code cache lookup will
  // trigger new translations.
  page_hash_is_valid = false;
  page_hash = 0;

  auto prot = PROT_READ;
  if (PageState::kRW == new_state) prot |= PROT_WRITE;

  GRANARY_IF_ASSERT( errno = 0; )
  mprotect(ConvertAddress(base32), old_range.limit - base32, prot);
  GRANARY_ASSERT(!errno && "Unable to change protections on RWX pages.");

  return true;
}

// Computes the page hash.
uint32_t Process32::HashPageRange(void) const {
  GRANARY_DEBUG( std::cerr << pid << " Process32::HashPageRange" << std::endl; )
  XXH32_state_t digest;
  XXH32_reset(&digest, static_cast<unsigned>(pid));

  auto num_pages = pages.size();
  XXH32_update(&digest, &num_pages, sizeof num_pages);
  
  for (auto &page : pages) {
    if (PageState::kRX == page.state) {
      if (!page.hash_is_valid) HashRange(this, page);
      XXH32_update(&digest, &(page.hash), sizeof page.hash);
    }
  }

  page_hash = XXH32_digest(&digest) & 0x00FFFFFFU;  // Keep low 24 bits.
  page_hash_is_valid = true;

  return page_hash;
}

// Allocates some memory.
Addr32 Process32::Allocate(size_t num_bytes, PagePerms perms) {
  GRANARY_ASSERT(0 < num_bytes);
  GRANARY_ASSERT(num_bytes == (num_bytes & kPageMask));

  std::sort(pages.begin(), pages.end());

  GRANARY_IF_DEBUG( DebugRanges(pages, regs.eip, regs.esp); )

  // Out of memory.
  auto addr32 = AllocateFromHighMem(num_bytes, perms);
  if (!addr32) return 0;

  CheckConsistency(pages);

  GRANARY_IF_DEBUG( DebugRanges(pages, regs.eip, regs.esp); )

  auto addr64 = ConvertAddress(addr32);
  GRANARY_IF_ASSERT( errno = 0; )
  auto ret = mmap64(addr64, num_bytes, PermsToProt(perms),
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  GRANARY_ASSERT(!errno && "Unable to map newly allocated process32 memory.");
  if (ret != addr64) return 0;

  // Invalidate the page hash because we've potentially allocated new code.
  if (PagePerms::kRX == perms || PagePerms::kRWX == perms) {
    page_hash_is_valid = false;
    page_hash = 0;
  }

  return addr32;
}

// Frees some memory. This builds a new list of page intervals.
//
// Note: This does not alter the sortedness of the page tables.
void Process32::Deallocate(Addr32 addr32, size_t num_bytes) {
  GRANARY_ASSERT(0 < num_bytes);
  GRANARY_ASSERT(addr32 == (addr32 & kPageMask));
  GRANARY_ASSERT(num_bytes == (num_bytes & kPageMask));

  auto addr32_limit = addr32 + static_cast<uint32_t>(num_bytes);
  GRANARY_ASSERT(addr32 < addr32_limit && "Invalid deallocate page range.");

  std::sort(pages.begin(), pages.end());

  auto invalidate_page_hash = false;

  // Start by splitting up the existing pages; this lets us bail out on the
  // process without actually losing info.
  std::vector<PageRange32> new_pages;
  std::vector<PageRange32> removed_pages;
  new_pages.reserve(std::max<size_t>(kReserveNumRanges, pages.size()));
  removed_pages.reserve(4);

  for (auto page : pages) {
    auto deallocated = false;

    // Not allowed to unmap certain pages (e.g. boundary pages).
    if (PagePerms::kInvalid == page.perms ||
        PageState::kReserved == page.state) {
      new_pages.push_back(page);

    // No overlap.
    } else if (addr32 >= page.limit || addr32_limit <= page.base) {
      new_pages.push_back(page);

    // This page is fully contained by the deallocation range.
    } else if (addr32 <= page.base && page.limit <= addr32_limit) {
      deallocated = true;
      removed_pages.push_back(page);

    // Overlap at the beginning.
    } else if (addr32 == page.base) {
      GRANARY_ASSERT(addr32_limit < page.limit &&
                     "Incorrectly identified prefix range.");
      deallocated = true;

      removed_pages.push_back({
          page.base, addr32_limit, page.base, page.perms, page.state, false, 0});

      PageRange32 high = {
          addr32_limit, page.limit, addr32_limit,
          page.perms, page.state, false, 0};

      if (page.base != page.lazy_base) {
        high.lazy_base = std::max(addr32_limit, page.lazy_base);
      }

      new_pages.push_back(high);

    // Overlap at the end.
    } else if (addr32_limit == page.limit) {
      GRANARY_ASSERT(addr32 > page.base &&
                     "Incorrectly identified suffix range.");
      deallocated = true;

      removed_pages.push_back({
          addr32, page.limit, 0, page.perms, page.state, false, 0});

      PageRange32 low = {
          page.base, addr32, page.base, page.perms, page.state, false, 0};

      if (page.base != page.lazy_base) {
        low.lazy_base = std::min(addr32, page.lazy_base);
      }

      new_pages.push_back(low);

    // Deallocation range is fully contained within the current range.
    } else {
      deallocated = true;
      removed_pages.push_back({
          addr32, addr32_limit, 0, page.perms, page.state, false, 0});

      PageRange32 low = {
          page.base, addr32, page.base, page.perms, page.state, false, 0};

      PageRange32 high = {
          addr32_limit, page.limit, addr32_limit,
          page.perms, page.state, false, 0};

      if (page.base != page.lazy_base) {
        low.lazy_base = std::min(addr32, page.lazy_base);
        high.lazy_base = std::max(addr32_limit, page.lazy_base);
      }

      new_pages.push_back(low);
      new_pages.push_back(high);
    }

    if (deallocated && page.hash_is_valid &&
        (PagePerms::kRX == page.perms || PagePerms::kRWX == page.perms)) {
      invalidate_page_hash = true;
    }
  }

  // Unmap the various sub page ranges.
  GRANARY_IF_ASSERT( errno = 0; )
  for (auto page : removed_pages) {
    munmap(ConvertAddress(page.base), page.limit - page.base);
    GRANARY_ASSERT(!errno && "Unable to unmap deallocated process32 memory.");
  }

  // Swap the page lists.
  pages.swap(new_pages);

  CheckConsistency(pages);

  if (invalidate_page_hash && page_hash_is_valid) {
    page_hash_is_valid = false;
    page_hash = 0;
  }
}

// Allocates `num_pages` of memory with permission `perms` at a "variable"
// address. The address at which the pages are allocated is returned.
//
// Performs a linear scan to perform a first-fit of the pages into memory.
// The scan starts from high memory and works its way down.
//
// Note: `num_bytes` must be a multiple of `kPageSize`.
Addr32 Process32::AllocateFromHighMem(size_t num_bytes, PagePerms perms) {
  GRANARY_ASSERT(2 <= pages.size() && "Must have at least two page ranges.");

  const auto end = pages.end();
  for (auto bracket = pages.begin(); bracket != end; ) {
    auto high = *bracket++;
    if (!high.base) break;  // Zero page.
    if (high.base > kMaxAddress) continue;  // Stack.
    if (num_bytes > high.base) break;  // No more space.

    const auto low = *bracket;
    size_t range_base = low.limit;
    size_t range_limit = high.base;

    GRANARY_ASSERT(range_base <= range_limit && "Invalid page range.");

    size_t range_size = range_limit - range_base;
    if (num_bytes > range_size) {
      continue;
    }

    auto alloc_base = high.base - static_cast<uint32_t>(num_bytes);

    GRANARY_ASSERT(alloc_base < high.base && "Invalid allocated range.");
    pages.push_back({alloc_base, high.base, alloc_base, perms,
                     BeginState(perms), false, 0});
    return alloc_base;
  }

  return 0;
}

}  // namespace os
}  // namespace granary
