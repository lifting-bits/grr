/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_INTERRUPT_H_
#define GRANARY_BASE_INTERRUPT_H_

namespace granary {
namespace detail {

class InterruptState {
 public:
  explicit InterruptState(bool is_interruptible_, bool is_ignorable_);
  ~InterruptState(void);

  bool is_interruptible;
  bool is_ignorable;
  bool is_done;
  int pending_signal;
  InterruptState *prev_state;

 private:
  InterruptState(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InterruptState);
};

}  // namespace

bool IsInterruptible(void);
void QueueInterrupt(int signal);
bool HasPendingInterrupt(void);
void MutePendingInterrupt(void);
[[noreturn]] void NonMaskableInterrupt(void);

class Interruptible : public detail::InterruptState {
 public:
  inline Interruptible(void)
      : detail::InterruptState(true, false) {}

  ~Interruptible(void) = default;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(Interruptible);
};

class Uninterruptible : public detail::InterruptState {
 public:
  inline Uninterruptible(void)
      : detail::InterruptState(false, false) {}

  ~Uninterruptible(void) = default;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Uninterruptible);
};

class Ignorable : public detail::InterruptState {
 public:
  inline Ignorable(void)
      : detail::InterruptState(false, true) {}

  ~Ignorable(void) = default;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(Ignorable);
};
}  // namespace granary

#endif  // GRANARY_BASE_INTERRUPT_H_
