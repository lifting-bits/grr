/* Copyright 2016 Peter Goodman (peter@trailofbits.com), all rights reserved. */

#include "granary/input/record.h"

namespace granary {
namespace input {

IORecording *gRecord = nullptr;

IORecording::IORecording(void)
    : num_inputs(0),
      num_input_bytes(0),
      num_outputs(0),
      num_output_bytes(0),
      num_splits(0) {}


void IORecording::AddInput(const std::string &data) {
  num_inputs += 1;
  num_input_bytes += data.size();

  IOSystemCall syscall;
  syscall.kind = IOKind::kInput;
  syscall.data = data;
  system_calls.push_back(std::move(syscall));
}

void IORecording::AddInput(std::string &&data) {
  num_inputs += 1;
  num_input_bytes += data.size();

  IOSystemCall syscall;
  syscall.kind = IOKind::kInput;
  syscall.data = std::move(data);
  system_calls.push_back(std::move(syscall));
}

void IORecording::AddOutput(std::string &&data) {
  num_outputs += 1;
  num_output_bytes += data.size();

  IOSystemCall syscall;
  syscall.kind = IOKind::kOutput;
  syscall.data = std::move(data);
  system_calls.push_back(std::move(syscall));
}

void IORecording::AddSplit(void) {
  ++num_splits;
  IOSystemCall syscall;
  syscall.kind = IOKind::kOutput;
  system_calls.push_back(syscall);
}

std::string IORecording::ToInput(void) const {
  std::string data;
  data.reserve(num_input_bytes);
  for (const auto &record : system_calls) {
    if (IOKind::kInput == record.kind) {
      data.insert(data.end(), record.data.begin(), record.data.end());
    }
  }
  return data;
}

std::string IORecording::ToOutput(void) const {
  std::string data;
  data.reserve(num_output_bytes);
  for (const auto &record : system_calls) {
    if (IOKind::kOutput == record.kind) {
      data.insert(data.end(), record.data.begin(), record.data.end());
    }
  }
  return data;
}

}  // namespace input
}  // namespace granary
