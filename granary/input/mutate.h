/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef LIB_CGC_MUTATE_H_
#define LIB_CGC_MUTATE_H_

#include <string>

#include "granary/input/record.h"

namespace granary {
namespace input {

// Mutator.
class Mutator {
 public:
  Mutator(const IORecording *record_);
  virtual ~Mutator(void);

  std::string RequestMutation(void);

  static Mutator *Create(const IORecording *record, const std::string &mutator);

 protected:

  virtual IORecording *RequestMutationImpl(void) = 0;

  const IORecording *record;
};

}  // namespace input
}  // namespace granary

#endif  // LIB_CGC_MUTATE_H_
