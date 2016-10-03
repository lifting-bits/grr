/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_X86_XED_INTEL64_H_
#define GRANARY_ARCH_X86_XED_INTEL64_H_

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wdocumentation"
#pragma clang diagnostic ignored "-Wswitch-enum"
extern "C" {
#define XED_DLL
#include "../../../third_party/xed-intel64/include/xed-interface.h"
}  // extern C

#endif  // GRANARY_ARCH_X86_XED_INTEL64_H_
