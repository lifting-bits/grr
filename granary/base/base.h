/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_BASE_H_
#define GRANARY_BASE_BASE_H_

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdbool>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
typedef long int ssize_t;

#if defined(GRANARY_TARGET_debug)
# include <iostream>
# define GRANARY_DEBUG(...) do { __VA_ARGS__ } while(false);
# define GRANARY_IF_DEBUG(...) __VA_ARGS__
# define GRANARY_IF_DEBUG_(...) __VA_ARGS__ ,
# define _GRANARY_IF_DEBUG(...) , __VA_ARGS__
# define GRANARY_IF_DEBUG_ELSE(a, b) a
#else
# define GRANARY_DEBUG(...)
# define GRANARY_IF_DEBUG(...)
# define GRANARY_IF_DEBUG_(...)
# define _GRANARY_IF_DEBUG(...)
# define GRANARY_IF_DEBUG_ELSE(a, b) b
#endif  // GRANARY_TARGET_debug

// Serves as documentation of the key entrypoints into Granary.
#define GRANARY_ENTRYPOINT

#define GRANARY_THREAD_LOCAL(...) __VA_ARGS__

#define GRANARY_EARLY_GLOBAL __attribute__((init_priority(102)))
#define GRANARY_GLOBAL __attribute__((init_priority(103)))
#define GRANARY_UNPROTECTED_GLOBAL \
  __attribute__((section(".bss.granary_unprotected")))

// For namespace-based `using` declarations without triggering the linter.
#define GRANARY_USING_NAMESPACE using namespace  // NOLINT

#define GRANARY_UNIQUE_SYMBOL \
  GRANARY_CAT( \
    GRANARY_CAT( \
      GRANARY_CAT(_, __LINE__), \
      GRANARY_CAT(_, __INCLUDE_LEVEL__)), \
    GRANARY_CAT(_, __COUNTER__))

// For use only when editing text with Eclipse CDT (my version doesn't handle
// `decltype` or `alignof` well)
#define IF_ECLIPSE_alignas(...)
#define IF_ECLIPSE_alignof(...) 16
#ifdef GRANARY_ECLIPSE
# define alignas(...)
# define alignof(...) 16
# define GRANARY_ENABLE_IF(...) int
# define GRANARY_IF_NOT_ECLIPSE(...)
#else
# define GRANARY_ENABLE_IF(...) __VA_ARGS__
# define GRANARY_IF_NOT_ECLIPSE(...) __VA_ARGS__
#endif

# define GRANARY_IF_ASSERT(...) __VA_ARGS__
# define GRANARY_ASSERT(...) GRANARY_ASSERT_(__VA_ARGS__)
# define GRANARY_ASSERT_(...) \
  if (!(__VA_ARGS__)) \
    granary_unreachable(#__VA_ARGS__, \
                        __FILE__ ":" GRANARY_TO_STRING(__LINE__))

#ifdef GRANARY_ARCH_INTERNAL
# define GRANARY_ARCH_PUBLIC public
#else
# define GRANARY_ARCH_PUBLIC private
#endif

// Marks some pointers as being internal, and convertible to void for exports.
#define GRANARY_MUTABLE mutable
#define GRANARY_POINTER(type) type
#define GRANARY_UINT32(type) type
#define GRANARY_PROTECTED public
#define GRANARY_PUBLIC public
#define GRANARY_CONST
#define GRANARY_IF_EXTERNAL(...)
#define _GRANARY_IF_EXTERNAL(...)
#define GRANARY_IF_INTERNAL(...) __VA_ARGS__
#define _GRANARY_IF_INTERNAL(...) , __VA_ARGS__
#define GRANARY_IF_INTERNAL_(...) __VA_ARGS__ ,
#define GRANARY_EXTERNAL_DELETE

// Name of the granary binary.
#ifndef GRANARY_NAME
# define GRANARY_NAME granary
#endif
#define GRANARY_NAME_STRING GRANARY_TO_STRING(GRANARY_NAME)

// Static branch prediction hints.
#define GRANARY_LIKELY(x) __builtin_expect((x),1)
#define GRANARY_UNLIKELY(x) __builtin_expect((x),0)

// Inline assembly.
#define GRANARY_INLINE_ASSEMBLY(...) __asm__ __volatile__ ( __VA_ARGS__ )

// Convert a sequence of symbols into a string literal.
#define GRANARY_TO_STRING__(x) #x
#define GRANARY_TO_STRING_(x) GRANARY_TO_STRING__(x)
#define GRANARY_TO_STRING(x) GRANARY_TO_STRING_(x)

// Concatenate two symbols into one.
#define GRANARY_CAT__(x, y) x ## y
#define GRANARY_CAT_(x, y) GRANARY_CAT__(x, y)
#define GRANARY_CAT(x, y) GRANARY_CAT_(x, y)

// Expand out into nothing.
#define GRANARY_NOTHING__
#define GRANARY_NOTHING_ GRANARY_NOTHING__
#define GRANARY_NOTHING GRANARY_NOTHING_

// Determine the number of arguments in a variadic macro argument pack.
// From: http://efesx.com/2010/07/17/variadic-macro-to-count-number-of-\
// arguments/#comment-256
#define GRANARY_NUM_PARAMS_(_0,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,N,...) N
#define GRANARY_NUM_PARAMS(...) \
  GRANARY_NUM_PARAMS_(, ##__VA_ARGS__,12,11,10,9,8,7,6,5,4,3,2,1,0)

// Splats out the arguments passed into the macro function. This assumes one
// is doing something like: `GRANARY_SPLAT((x, y))`, then what you will get
// is `x, y`.
#define GRANARY_SPLAT(params) GRANARY_PARAMS params

// Spits back out the arguments passed into the macro function.
#define GRANARY_PARAMS(...) __VA_ARGS__

// Try to make sure that a function is not optimized.
#define GRANARY_DISABLE_OPTIMIZER __attribute__((used, noinline, \
                                                 visibility ("default")))

// Export some function to instrumentation code. Only exported code can be
// directly invoked by instrumented code.
//
// Note: While these functions can be invoked by instrumented code, their
//       code *is not* instrumented.
#define GRANARY_EXPORT_TO_INSTRUMENTATION \
  __attribute__((noinline, used, section(".text.inst_exports")))

// Granary function that is to be instrumented as part of a test case.
#define GRANARY_TEST_CASE \
  __attribute__((noinline, used, section(".text.test_cases")))

// Determine how much should be added to a value `x` in order to align `x` to
// an `align`-byte boundary.
#define GRANARY_ALIGN_FACTOR(x, align) \
  (((x) % (align)) ? ((align) - ((x) % (align))) : 0)

// Align a value `x` to an `align`-byte boundary.
#define GRANARY_ALIGN_TO(x, align) \
  ((x) + GRANARY_ALIGN_FACTOR(x, align))

// Return the maximum or minimum of two values.
#define GRANARY_MIN(a, b) ((a) < (b) ? (a) : (b))
#define GRANARY_MAX(a, b) ((a) < (b) ? (b) : (a))

// Disallow copying of a specific class.
#define GRANARY_DISALLOW_COPY(cls) \
  cls(const cls &) = delete; \
  cls(const cls &&) = delete

// Disallow assigning of instances of a specific class.
#define GRANARY_DISALLOW_ASSIGN(cls) \
  void operator=(const cls &) = delete; \
  void operator=(const cls &&) = delete

// Disallow copying and assigning of instances of a specific class.
#define GRANARY_DISALLOW_COPY_AND_ASSIGN(cls) \
  GRANARY_DISALLOW_COPY(cls); \
  GRANARY_DISALLOW_ASSIGN(cls)

// Disallow copying of instances of a class generated by a specific
// class template.
#define GRANARY_DISALLOW_COPY_TEMPLATE(cls, params) \
  cls(const cls<GRANARY_PARAMS params> &) = delete; \
  cls(const cls<GRANARY_PARAMS params> &&) = delete

// Disallow assigning of instances of a specific class.
#define GRANARY_DISALLOW_ASSIGN_TEMPLATE(cls, params) \
  void operator=(const cls<GRANARY_PARAMS params> &) = delete; \
  void operator=(const cls<GRANARY_PARAMS params> &&) = delete

// Disallow copying and assigning of instances of a class generated by a
// specific class template.
#define GRANARY_DISALLOW_COPY_AND_ASSIGN_TEMPLATE(cls, params) \
  GRANARY_DISALLOW_COPY_TEMPLATE(cls, params); \
  GRANARY_DISALLOW_ASSIGN_TEMPLATE(cls, params)


// Mark a result / variable as being used.
#define GRANARY_UNUSED(...) (void) __VA_ARGS__
#define GRANARY_USED(var) \
  do { \
    GRANARY_INLINE_ASSEMBLY("" :: "m"(var)); \
    GRANARY_UNUSED(var); \
  } while (0)

#define GRANARY_COMMA() ,

// Apply a macro `pp` to each of a variable number of arguments. Separate the
// results of the macro application with `sep`.
#define GRANARY_APPLY_EACH(pp, sep, ...) \
  GRANARY_CAT(GRANARY_APPLY_EACH_, GRANARY_NUM_PARAMS(__VA_ARGS__))( \
      pp, sep, ##__VA_ARGS__)

#define GRANARY_APPLY_EACH_1(pp, sep, a0) \
  pp(a0)

#define GRANARY_APPLY_EACH_2(pp, sep, a0, a1) \
  pp(a0) sep() \
  pp(a1)

#define GRANARY_APPLY_EACH_3(pp, sep, a0, a1, a2) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2)

#define GRANARY_APPLY_EACH_4(pp, sep, a0, a1, a2, a3) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2) sep() \
  pp(a3)

#define GRANARY_APPLY_EACH_5(pp, sep, a0, a1, a2, a3, a4) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2) sep() \
  pp(a3) sep() \
  pp(a4)

#define GRANARY_APPLY_EACH_6(pp, sep, a0, a1, a2, a3, a4, a5) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2) sep() \
  pp(a3) sep() \
  pp(a4) sep() \
  pp(a5)

#define GRANARY_APPLY_EACH_7(pp, sep, a0, a1, a2, a3, a4, a5, a6) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2) sep() \
  pp(a3) sep() \
  pp(a4) sep() \
  pp(a5) sep() \
  pp(a6)

#define GRANARY_APPLY_EACH_8(pp, sep, a0, a1, a2, a3, a4, a5, a6, a7) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2) sep() \
  pp(a3) sep() \
  pp(a4) sep() \
  pp(a5) sep() \
  pp(a6) sep() \
  pp(a7)

#define GRANARY_APPLY_EACH_9(pp, sep, a0, a1, a2, a3, a4, a5, a6, a7, a8) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2) sep() \
  pp(a3) sep() \
  pp(a4) sep() \
  pp(a5) sep() \
  pp(a6) sep() \
  pp(a7) sep() \
  pp(a8)

#define GRANARY_APPLY_EACH_10(pp, sep, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2) sep() \
  pp(a3) sep() \
  pp(a4) sep() \
  pp(a5) sep() \
  pp(a6) sep() \
  pp(a7) sep() \
  pp(a8) sep() \
  pp(a9)

#define GRANARY_APPLY_EACH_11(pp, sep, a0, a1, a2, a3, a4, a5,a6,a7,a8,a9,a10) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2) sep() \
  pp(a3) sep() \
  pp(a4) sep() \
  pp(a5) sep() \
  pp(a6) sep() \
  pp(a7) sep() \
  pp(a8) sep() \
  pp(a9) sep() \
  pp(a10)

#define GRANARY_APPLY_EACH_12(pp, sep, a0, a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2) sep() \
  pp(a3) sep() \
  pp(a4) sep() \
  pp(a5) sep() \
  pp(a6) sep() \
  pp(a7) sep() \
  pp(a8) sep() \
  pp(a9) sep() \
  pp(a10) sep() \
  pp(a11)

// Mark a symbol as exported.
#define GRANARY_EXPORT __attribute__((visibility("default")))

#if !defined(__x86_64__) && !defined(__x86_64)
# error "Granary must be compiled as a 64-bit program."
#endif

#define GRANARY_STRACE(...) \
  if (FLAGS_strace) { __VA_ARGS__ }

namespace granary {

typedef const uint8_t *PC;
typedef uint8_t *CachePC;
typedef int32_t CacheOffset;
typedef void *Addr64;
typedef uint8_t *AppPC64;
typedef uint32_t Addr32;
typedef Addr32 AppPC32;

}  // namespace granary

#include "granary/base/breakpoint.h"
#include "granary/base/interrupt.h"

#endif  // GRANARY_BASE_BASE_H_
