/* Copyright 2015 Peter Goodman, all rights reserved. */


#include "granary/input/mutate.h"
#include "granary/os/decree_user/decree.h"

#include <iostream>
#include <cstdlib>
#include <ctime>
#include "../../third_party/radamsa/radamsa.h"
#include "../../third_party/xxhash/xxhash.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-undefined-compare"

namespace granary {
namespace input {
namespace {

static bool IsInput(const IOSystemCall &syscall) {
  return IOKind::kInput == syscall.kind;
}

static size_t NumInputs(const IORecording *record) {
  return record->num_inputs;
}

static size_t MaxInputIndex(const IORecording *record) {
  size_t i = 0;
  size_t j = 0;
  for (const auto &syscall : record->system_calls) {
    ++i;
    if (IOKind::kInput == syscall.kind) {
      ++j;
      if (j == record->num_inputs) {
        break;
      }
    }
  }
  return i;
}

static size_t NextInputIndex(const IORecording *record, size_t start) {
  for (size_t i = start; i < record->system_calls.size(); ++i) {
    if (IOKind::kInput == record->system_calls[i].kind) {
      return i;
    }
  }
  return record->system_calls.size();
}

// Copy a series of system calls (verbatim) within the range of indices.
static void CopyInputs(const IORecording *test,
                       IORecording *output_test,
                       size_t begin,
                       const size_t end) {
  for (; begin < end; ++begin) {
    const auto &syscall = test->system_calls[begin];
    if (IOKind::kInput == syscall.kind) {
      output_test->AddInput(syscall.data);
    }
  }
}

// Chunk-canonicalize a testcase. This canonicalizes adjacent receives.
static IORecording *ChunkedCanonicalizeTest(
    const IORecording *record, IORecording *canonical_test) {

  auto num_syscalls = record->system_calls.size();
  for (size_t i = 0; i < num_syscalls; ) {
    const auto &syscall = record->system_calls[i];
    if (!IsInput(syscall)) {
      ++i;
      continue;
    }

    // Figure out how much data storage we'll need.
    size_t reserve_size = 0;
    for (auto j = i; j < num_syscalls; ++j) {
      const auto &inner_syscall = record->system_calls[j];
      if (!IsInput(inner_syscall)) {
        break;
      }
      reserve_size += inner_syscall.data.size();
    }

    // Allocate the storage.
    std::string canonical_data;
    canonical_data.reserve(reserve_size);

    // Fill the storage.
    for (; i < num_syscalls; ++i) {
      const IOSystemCall &inner_syscall = record->system_calls[i];
      if (!IsInput(inner_syscall)) {
        break;
      }
      canonical_data.insert(
          canonical_data.end(),
          inner_syscall.data.begin(),
          inner_syscall.data.end());
    }

    canonical_test->AddInput(std::move(canonical_data));
  }

  return canonical_test;
}


// Injects one of the inputs from the testcase into an arbitrary spot within
// the testcase.
template <typename MutationEngine>
class SpliceMutator : public Mutator {
 public:
  SpliceMutator(const IORecording *record_)
      : Mutator(record_),
        max_prefix_length(0),
        num_inputs(NumInputs(record_)),
        mutation_index(0),
        max_input_index(MaxInputIndex(record_)),
        mutate() {}

  virtual ~SpliceMutator(void) = default;

  void Reset(void) {
    max_prefix_length = 0;
    mutation_index = 0;
  }

  virtual IORecording *RequestMutationImpl(void) {
    if (max_prefix_length >= num_inputs) {
      return nullptr;
    }

    auto test = new IORecording;

    // Copy some prefix of the syscalls verbatim.
    size_t i = 0;
    for (size_t prefix_length = 0;
         prefix_length < max_prefix_length && i < max_input_index;
         ++i) {
      const auto &orig_syscall = record->system_calls[i];
      if (!IsInput(orig_syscall)) {
        continue;
      }
      mutate(test, orig_syscall);
      ++prefix_length;
    }

    // Go and choose a specific input syscall to move into place somewhere
    // within the testcase. In some cases, this acts as a repeater.
    for (; mutation_index < max_input_index; ) {
      const auto &orig_syscall = record->system_calls[mutation_index++];
      if (!IsInput(orig_syscall)) {
        continue;
      }
      test->AddInput(orig_syscall.data);
      break;
    }

    // Copy the suffix of the syscalls verbatim.
    for (i = NextInputIndex(record, i); i < max_input_index; ++i) {
      const auto &orig_syscall = record->system_calls[i];
      if (!IsInput(orig_syscall)) {
        continue;
      }
      test->AddInput(orig_syscall.data);
    }

    // Set up for the next iteration.
    if (mutation_index >= max_input_index) {
      mutation_index = 0;
      max_prefix_length += 1;
    }

    return test;
  }

 private:
  size_t max_prefix_length;
  const size_t num_inputs;
  size_t mutation_index;
  const size_t max_input_index;
  MutationEngine mutate;
};

template <typename MutationEngine>
class ChunkedSpliceMutator : public SpliceMutator<MutationEngine> {
 public:
  ChunkedSpliceMutator(const IORecording *record_)
      : SpliceMutator<MutationEngine>(
            ChunkedCanonicalizeTest(record_, new IORecording)) {}

  virtual ~ChunkedSpliceMutator(void) {
    delete this->record;
  }
};

// Apply a mutator to only one out of all of the input receives.
template <typename MutationEngine>
class SliceMutator : public Mutator {
 public:
  SliceMutator(const IORecording *record_, size_t max_slice_size_=0)
      : Mutator(record_),
        curr_input_index(NextInputIndex(record_, 0)),
        num_inputs(NumInputs(record_)),
        max_input_index(MaxInputIndex(record_)),
        slice_size(1),
        max_slice_size(max_slice_size_ ?: num_inputs),
        mutate() {}

  virtual IORecording *RequestMutationImpl(void) {
    if (curr_input_index >= max_input_index) {
      if (++slice_size > max_slice_size) {
        return nullptr;
      } else {
        curr_input_index = NextInputIndex(record, 0);
      }
    }

    auto test = new IORecording;

    // Verbatim copy of a prefix.
    CopyInputs(record, test, 0, curr_input_index);

    // Mutate a slice of system calls.
    auto input_index = curr_input_index;
    for (size_t i = 0; i < slice_size && input_index < max_input_index; ++i) {
      const auto &curr_syscall = record->system_calls[input_index];
      GRANARY_ASSERT(IsInput(curr_syscall));
      mutate(test, curr_syscall);
      input_index = NextInputIndex(record, input_index + 1);
    }

    // Verbatim copy of a suffix.
    CopyInputs(record, test, input_index, max_input_index);

    // Next iteration moves the slice forward.
    curr_input_index = NextInputIndex(record, curr_input_index + 1);

    return test;
  }

  void Reset(void) {
    curr_input_index = NextInputIndex(record, 0);
    slice_size = 1;
  }

 protected:

  size_t curr_input_index;
  const size_t num_inputs;
  const size_t max_input_index;
  size_t slice_size;
  const size_t max_slice_size;

  MutationEngine mutate;
};

template <typename MutationEngine>
class ChunkedSliceMutator : public SliceMutator<MutationEngine> {
 public:
  ChunkedSliceMutator(const IORecording *record_, size_t max_slice_size_=0)
      : SliceMutator<MutationEngine>(
            ChunkedCanonicalizeTest(record_, new IORecording),
            max_slice_size_) {}

  virtual ~ChunkedSliceMutator(void) {
    delete this->record;
  }
};

// Concatenate all receives in a testcase, then run a mutation engine on it.
template <typename MutationEngine>
class ConcatMutator : public Mutator {
 public:
  ConcatMutator(const IORecording *record_)
      : Mutator(ConcatTestCase(record_)),
        mutate() {}

  virtual ~ConcatMutator(void) {
    delete record;
  }

  virtual IORecording *RequestMutationImpl(void) {
    if (!record->num_input_bytes) {
      return nullptr;
    }
    auto test = new IORecording;
    mutate(test, record->system_calls[0]);
    return test;
  }

 private:
  static IORecording *ConcatTestCase(const IORecording *record_) {
    auto test = new IORecording;
    test->AddInput(std::move(record_->ToInput()));
    return test;
  }

  MutationEngine mutate;
};

// Turns a normal mutator into an infinite mutator.
template <typename BaseMutator>
class InfiniteMutator : public BaseMutator {
 public:
  using BaseMutator::BaseMutator;

  virtual ~InfiniteMutator(void) = default;

  virtual IORecording *RequestMutationImpl(void) {
    auto test = this->BaseMutator::RequestMutationImpl();
    if (!test) {
      this->BaseMutator::Reset();
      test = this->BaseMutator::RequestMutationImpl();
    }
    return test;
  }
};

// Creates a pipeline of mutators.
template <typename BaseMutator1, typename BaseMutator2>
class PipelineMutator : public BaseMutator1 {
 public:
  PipelineMutator(const IORecording *record_)
      : BaseMutator1(record_),
        base2(nullptr),
        base2_seed(nullptr) {}

  void Reset(void) {
    DestroyBase2();
    this->BaseMutator1::Reset();
  }

  virtual IORecording *RequestMutationImpl(void) {
    InitBase2();
    if (!base2) {
      return nullptr;
    }
    auto test = base2->RequestMutationImpl();
    if (!test) {
      DestroyBase2();
      InitBase2();
      if (!base2) {
        test = base2->RequestMutationImpl();
      }
    }
    return test;
  }

  virtual ~PipelineMutator(void) {
    DestroyBase2();
  }

 private:
  void InitBase2(void) {
    if (!base2_seed) {
      base2_seed = this->BaseMutator1::RequestMutationImpl();
    }
    if (base2_seed && !base2) {
      base2 = new (mem) BaseMutator2(base2_seed);
    }
  }

  void DestroyBase2(void) {
    if (base2) base2->~BaseMutator2();
    if (base2_seed) delete base2_seed;
    base2 = nullptr;
    base2_seed = nullptr;
  }

  alignas(BaseMutator2) uint8_t mem[sizeof(BaseMutator2)];
  BaseMutator2 *base2;
  const IORecording *base2_seed;
};

class IRandomMutator {
 public:
  IRandomMutator(void) {
    XXH32_reset(&state, static_cast<unsigned>(CurrentTSC()));
  }

  uint64_t NextRandom(void) {
    auto tsc = CurrentTSC();
    XXH32_update(&state, &tsc, sizeof tsc);
    return XXH32_digest(&state);
  }

 private:
  static inline uint64_t CurrentTSC(void) {
    uint64_t x;
    __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
    return x;
  }

  XXH32_state_t state;
};

// Replace all bytes with random bytes.
class RandomizeSyscallMutator : public IRandomMutator {
 public:
  void operator()(IORecording *test, const IOSystemCall &input) {
    std::string output_data;
    output_data.reserve(input.data.size());
    for (auto i = 0UL; i < input.data.size(); ++i) {
      output_data.push_back(static_cast<char>(NextRandom()));
    }
    test->AddInput(std::move(output_data));
  }
};

// Replace all bytes with random bytes.
class RandomBitFlipSyscallMutator : public IRandomMutator {
 public:
  void operator()(IORecording *test, const IOSystemCall &input) {
    std::string output_data;
    output_data.reserve(input.data.size());
    for (auto input_char : input.data) {
      auto mask = static_cast<char>(NextRandom());
      output_data.push_back(input_char ^ mask);
    }
    test->AddInput(std::move(output_data));
  }
};

static const size_t kRepeatAmounts[] = {
  0, 1, 2, 10, 100, 1000, 10000
};

class RandomRepeatingSyscallMutator : public IRandomMutator {
 public:
  void operator()(IORecording *test, const IOSystemCall &input) {
    auto repetitions = kRepeatAmounts[NextRandom() % 7UL];
    if (!repetitions) {
      return;
    }

    std::string output_data;
    output_data.reserve(input.data.size() * repetitions);
    for (auto i = 0UL; i < repetitions; ++i) {
      output_data.insert(
          output_data.end(), input.data.begin(), input.data.end());
    }

    test->AddInput(std::move(output_data));
  }
};

// Generic bit flipper.
template <size_t kStartBit, size_t kNumBits>
class BitFlipSyscallMutator {
 public:
  void operator()(IORecording *test, const IOSystemCall &input) {
    std::string output_data;
    output_data.reserve(input.data.size());
    for (auto byte : input.data) {
      output_data.push_back(
          static_cast<char>(FlipBits(static_cast<uint8_t>(byte))));
    }
    test->AddInput(std::move(output_data));
  }

 private:
  static_assert(1 <= kStartBit, "1 <= kStartBit <= 8");
  static_assert(8 >= kStartBit, "1 <= kStartBit <= 8");
  static_assert(1 <= kNumBits, "1 <= kNumBits <= 8");
  static_assert(8 >= kNumBits, "1 <= kNumBits <= 8");

  enum : size_t {
    kMaskHigh = 0xFFUL << kStartBit,
    kMaskLow = 0xFFUL >> (9 - kStartBit),
    kMask = kMaskLow | kMaskHigh,
    kFlippedMask = ~kMask
  };

  static uint8_t FlipBits(uint8_t byte) {
    return static_cast<uint8_t>((byte & kMask) | (~byte & kFlippedMask));
  }
};

// Drop the syscalls.
class DeletingSyscallMutator {
 public:
  void operator()(IORecording *, const IOSystemCall &) {}
};

// Returns the identity of the system call.
class IdentitySyscallMutator {
 public:
  void operator()(IORecording *test, const IOSystemCall &input) {
    test->AddInput(input.data);
  }
};

// Use Radamsa to mutate a system call.
class RadamsaSyscallMutator {
 public:
  void operator()(IORecording *test, const IOSystemCall &input) {
    test->AddInput(radamsa::Mutate(input.data));
  }
};

}  // namespace


Mutator::Mutator(const IORecording *record_)
    : record(record_) {}

Mutator::~Mutator(void) {}

std::string Mutator::RequestMutation(void) {
  if (auto req_record = this->RequestMutationImpl()) {
    auto ret = req_record->ToInput();
    delete req_record;
    return ret;
  } else {
    return "";
  }
}

Mutator *Mutator::Create(const IORecording *test,
                        const std::string &mutator) {
  if (mutator == "splice") {
    return new SpliceMutator<IdentitySyscallMutator>(test);

  } else if (mutator == "splice_chunked") {
    return new ChunkedSpliceMutator<IdentitySyscallMutator>(test);

  } else if (mutator == "random") {
    return new SliceMutator<RandomizeSyscallMutator>(test);

  } else if (mutator == "dropper") {
    return new SliceMutator<DeletingSyscallMutator>(test);

  } else if (mutator == "bitflip1") {
    return new SliceMutator<BitFlipSyscallMutator<1, 1>>(test);
  } else if (mutator == "bitflip2") {
    return new SliceMutator<BitFlipSyscallMutator<2, 1>>(test);
  } else if (mutator == "bitflip3") {
    return new SliceMutator<BitFlipSyscallMutator<3, 1>>(test);
  } else if (mutator == "bitflip4") {
    return new SliceMutator<BitFlipSyscallMutator<4, 1>>(test);
  } else if (mutator == "bitflip5") {
    return new SliceMutator<BitFlipSyscallMutator<5, 1>>(test);
  } else if (mutator == "bitflip6") {
    return new SliceMutator<BitFlipSyscallMutator<6, 1>>(test);
  } else if (mutator == "bitflip7") {
    return new SliceMutator<BitFlipSyscallMutator<7, 1>>(test);
  } else if (mutator == "bitflip8") {
    return new SliceMutator<BitFlipSyscallMutator<8, 1>>(test);

  } else if (mutator == "bitflip2_2") {
    return new SliceMutator<BitFlipSyscallMutator<2, 2>>(test);
  } else if (mutator == "bitflip3_2") {
    return new SliceMutator<BitFlipSyscallMutator<3, 2>>(test);
  } else if (mutator == "bitflip4_2") {
    return new SliceMutator<BitFlipSyscallMutator<4, 2>>(test);
  } else if (mutator == "bitflip5_2") {
    return new SliceMutator<BitFlipSyscallMutator<5, 2>>(test);
  } else if (mutator == "bitflip6_2") {
    return new SliceMutator<BitFlipSyscallMutator<6, 2>>(test);
  } else if (mutator == "bitflip7_2") {
    return new SliceMutator<BitFlipSyscallMutator<7, 2>>(test);
  } else if (mutator == "bitflip8_2") {
    return new SliceMutator<BitFlipSyscallMutator<8, 2>>(test);

  } else if (mutator == "bitflip4_4") {
    return new SliceMutator<BitFlipSyscallMutator<4, 4>>(test);
  } else if (mutator == "bitflip6_4") {
    return new SliceMutator<BitFlipSyscallMutator<6, 4>>(test);
  } else if (mutator == "bitflip8_4") {
    return new SliceMutator<BitFlipSyscallMutator<8, 4>>(test);

  } else if (mutator == "bitflip8_8") {
    return new SliceMutator<BitFlipSyscallMutator<8, 8>>(test);

  } else if (mutator == "inf_bitflip_random") {
    return new InfiniteMutator<SliceMutator<RandomBitFlipSyscallMutator>>(test);

  } else if (mutator == "inf_radamsa_chunked") {
      return new InfiniteMutator<
          ChunkedSliceMutator<RadamsaSyscallMutator>>(test);

  } else if (mutator == "inf_radamsa_spliced") {
      return new InfiniteMutator<
          ChunkedSpliceMutator<RadamsaSyscallMutator>>(test);

  } else if (mutator == "inf_radamsa_concat") {
    return new ConcatMutator<RadamsaSyscallMutator>(test);

  } else if (mutator == "inf_chunked_repeat") {
    return new InfiniteMutator<
        ChunkedSliceMutator<RandomRepeatingSyscallMutator>>(test, 1);

  } else {
    return nullptr;
  }
}

#pragma clang diagnostic pop

}  // namespace input
}  // namespace granary
