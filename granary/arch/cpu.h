/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_CPU_H_
#define GRANARY_ARCH_CPU_H_

namespace granary {
namespace arch {

void Relax(void);

void SerializePipeline(void);

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_ARCH_CPU_H_
