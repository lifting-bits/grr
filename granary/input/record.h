/* Copyright 2016 Peter Goodman (peter@trailofbits.com), all rights reserved. */

#ifndef GRANARY_OS_RECORD_H_
#define GRANARY_OS_RECORD_H_

#include <string>
#include <vector>

namespace granary {
namespace input {

enum class IOKind {
  kInvalid,
  kInput,
  kOutput
};

struct IOSystemCall {
  inline IOSystemCall(void)
      : kind(IOKind::kInvalid) {}

  std::string data;
  IOKind kind;
};

class IORecording {
 public:
  IORecording(void);

  size_t num_inputs;
  size_t num_input_bytes;

  size_t num_outputs;
  size_t num_output_bytes;

  size_t num_splits;

  std::vector<IOSystemCall> system_calls;

  typedef std::vector<IOSystemCall>::iterator iterator;
  typedef std::vector<IOSystemCall>::const_iterator const_iterator;

  inline iterator begin(void) {
    return system_calls.begin();
  }

  inline iterator end(void) {
    return system_calls.end();
  }

  inline const_iterator begin(void) const {
    return system_calls.begin();
  }

  inline const_iterator end(void) const {
    return system_calls.end();
  }

  void AddInput(const std::string &val);
  void AddInput(std::string &&val);
  void AddOutput(std::string &&val);
  void AddSplit(void);

  std::string ToInput(void) const;
  std::string ToOutput(void) const;
};

extern IORecording *gRecord;

}  // namespace input
}  // namespace granary

#endif  // GRANARY_OS_RECORD_H_
