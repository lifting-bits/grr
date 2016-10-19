/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"

#include <signal.h>
#include <unistd.h>

#include <sys/time.h>

namespace granary {
namespace {

static detail::InterruptState *gInterruptState = nullptr;

}  // namespace
namespace detail {

InterruptState::InterruptState(bool is_interruptible_, bool is_ignorable_)
    : is_interruptible(is_interruptible_),
      is_ignorable(is_ignorable_),
      is_done(false),
      pending_signal(0),
      prev_state(gInterruptState) {

  // Down-propagate if we've entered an interruptible region, but only deliver
  // the signal at the end.
  if (is_interruptible && prev_state && prev_state->pending_signal) {
    pending_signal = prev_state->pending_signal;
    prev_state->pending_signal = 0;
  }

  gInterruptState = this;
}

InterruptState::~InterruptState(void) {

  gInterruptState = prev_state;

  // We've got a pending signal that we haven't handled yet. Either handle it
  // or up-propagate it.
  if (pending_signal && !is_done) {
    if (!is_interruptible) {
      if (prev_state && !prev_state->pending_signal) {
        prev_state->pending_signal = pending_signal;
      }
      pending_signal = 0;
      is_done = true;
    } else {
      is_done = true;
      raise(pending_signal);
    }
  }
}

}  // namespace detail

bool IsInterruptible(void) {
  return !gInterruptState || gInterruptState->is_interruptible;
}

void QueueInterrupt(int signal) {
  GRANARY_ASSERT(gInterruptState && "Can't queue interrupt.");
  if (!gInterruptState->is_ignorable) {
    gInterruptState->pending_signal = signal;
  }
}

bool HasPendingInterrupt(void) {
  for (auto state = gInterruptState; state; state = state->prev_state) {
    if (state->pending_signal) {
      return true;
    }
  }
  return false;
}

void MutePendingInterrupt(void) {
  for (auto state = gInterruptState; state; state = state->prev_state) {
    if (state->pending_signal) {
      state->pending_signal = 0;
    }
  }
}

void NonMaskableInterrupt(void) {
  auto sig = SIGUSR1;

  // If there's a pending signal, then deliver it; otherwise, deliver the NMI.
  for (auto state = gInterruptState; state; state = state->prev_state) {
    if (state->pending_signal) {
      sig = state->pending_signal;
      state->pending_signal = 0;
      gInterruptState->is_interruptible = true;
    }
  }
  raise(sig);

  GRANARY_ASSERT(false && "Unable to raise non-maskable interrupt.");
  __builtin_unreachable();
}

}  // namespace granary
