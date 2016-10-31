/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_OS_PAGE_H_
#define GRANARY_OS_PAGE_H_

#include "granary/base/base.h"

#include <list>

namespace granary {
namespace os {
enum : uintptr_t {
  kPageSize = 4096ULL,
  kPageMask = ~4095ULL
};

enum : uintptr_t {
  k1GiB = 1ULL << 30ULL,
  kProcessSize = k1GiB * 4ULL,
  k1MiB = 1048576,
  kStackSize = 8 * k1MiB,
  kMappedStackSize = 2 * k1MiB,
  kStackLimitPage = 0xbaaaa000U,
  kStackEnd = kStackLimitPage + kPageSize,
  kStackBegin = kStackEnd - kStackSize,
  kMaxAddress = 0xB8000000U,
  kReserveNumRanges = 32UL,
  kTaskSize = 0xFFFFe000U,
  kMagicPageBegin = 0x4347c000U,
  kMagicPageEnd = kMagicPageBegin + kPageSize
};

enum class PageState : uint8_t {
  kReserved,
  kRO,
  kRW,
  kRX
};

enum class PagePerms : uint8_t {
  kInvalid,
  kRO,
  kRW,
  kRWX,
  kRX
};

struct PageRange32 {
  Addr32 base;
  Addr32 limit;
  Addr32 lazy_base;
  PagePerms perms;
  mutable PageState state;
  mutable bool hash_is_valid;
  mutable uint32_t hash;

  inline bool operator<(const PageRange32 &that) const {
    return base > that.base;
  }
};

}  // namespace os
}  // namespace granary

#endif  // GRANARY_OS_PAGE_H_
