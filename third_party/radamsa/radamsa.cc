/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "../../third_party/radamsa/radamsa.h"

#include <set>

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include "../../third_party/xxhash/xxhash.h"

namespace radamsa {
namespace {

static struct X {
  // Input/output to Radamsa.
  const std::string *input;
  std::string output;

  // Where are we in the process of reading `input`?
  size_t input_index;

  // Set of objects that remain unfreed by Radamsa.
  std::set<void *> allocations;

} gRadamsa;

// Radamsa's `main` function.
extern "C" void *radamsa_boot(int argc, const char *argv[]);

// Pretend to do a `read` system call from within Radamsa.
extern "C" ssize_t radamsa_read(int, uint8_t *buff, size_t buff_size) {
  auto count = std::min(
      buff_size, gRadamsa.input->length() - gRadamsa.input_index);

  memcpy(buff, gRadamsa.input->data() + gRadamsa.input_index, count);
  gRadamsa.input_index += count;
  return static_cast<ssize_t>(count);
}

// Pretend to do a `write` system call from within Radamsa.
extern "C" ssize_t radamsa_write(int, uint8_t *buff, size_t buff_size) {
  gRadamsa.output.insert(gRadamsa.output.end(), buff, buff + buff_size);
  return static_cast<ssize_t>(buff_size);
}

// Interposes on `malloc`s performed by Radamsa.
extern "C" void *radamsa_malloc(size_t size) {
  auto ptr = malloc(size);
  gRadamsa.allocations.insert(ptr);
  return ptr;
}

// Interposes on `free`s performed by Radamsa.
extern "C" void radamsa_free(void *ptr) {
  gRadamsa.allocations.erase(ptr);
  free(ptr);
}

// Interposes on `realloc`s performed by Radamsa. The OWL scheme compiler uses
// `realloc` for heap allocation.
extern "C" void *radamsa_realloc(void *ptr, size_t size) {
  gRadamsa.allocations.erase(ptr);
  ptr = realloc(ptr, size);
  gRadamsa.allocations.insert(ptr);
  return ptr;
}

static inline uint64_t CurrentTSC(void) {
  uint64_t x;
  __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
  return x;
}

static XXH64_state_t gRadamsaRandState;
static auto gRadamsaRandStateInitialized = false;
static char gRadamsaSeed[32] = {'\0'};
}  // namespace

std::string Mutate(const std::string &input) {
  if (!gRadamsaRandStateInitialized) {
    gRadamsaRandStateInitialized = true;
    XXH64_reset(&gRadamsaRandState, CurrentTSC());
  }

  XXH64_update(&gRadamsaRandState, input.data(), input.size());

  gRadamsa.input = &input;
  gRadamsa.input_index = 0;
  gRadamsa.output.clear();
  gRadamsa.output.reserve(input.size());

  sprintf(gRadamsaSeed, "%ld", XXH64_digest(&gRadamsaRandState) & 0xFFFFFFFFU);

  const char *args[] = {
    "radamsa",
    "--seed",
    gRadamsaSeed,
    nullptr
  };
#if 1
  // I am not sure if this is even valid, due to the Radamsa heap being a
  // static char array.
  radamsa_boot(3, args);
#else
  pthread_t radamsa;
  pthread_create(&radamsa, nullptr, radamsa_boot, nullptr);
  pthread_join(radamsa, nullptr);
#endif

  for (auto ptr : gRadamsa.allocations) free(ptr);

  gRadamsa.input = nullptr;
  gRadamsa.allocations.clear();

  std::string output;
  output.swap(gRadamsa.output);
  return output;
}

}  // namespace fuzz
