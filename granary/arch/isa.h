/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_ISA_H_
#define GRANARY_ARCH_ISA_H_

namespace granary {
namespace arch {

enum class ISA {
  x86,
  amd64
};

template <ISA isa>
constexpr unsigned AddressWidth(void);

template <>
constexpr unsigned AddressWidth<ISA::x86>(void) {
  return 32U;
}

template <>
constexpr unsigned AddressWidth<ISA::amd64>(void) {
  return 64U;
}

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_ARCH_ISA_H_
