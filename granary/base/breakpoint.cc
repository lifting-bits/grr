/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

extern "C" {

extern intptr_t granary_crash_pc = 0;
extern intptr_t granary_crash_addr = 0;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
void granary_unreachable(const char *error, const char *loc) {
#if defined(GRANARY_TARGET_debug)
  granary::Uninterruptible disable_interrupts;
  auto fd = open("/data/grr_crashes", O_CREAT | O_WRONLY | O_APPEND, 0666);
  if (-1 != fd) {
    write(fd, "Assertion failed:\n", 18);
    if (error) {
      write(fd, error, strlen(error));
    }
    write(fd, "\n", 1);
    if (loc) {
      write(fd, loc, strlen(loc));
      write(fd, "\n", 1);
    }

    char buf[128];
    if (granary_crash_pc) {
      write(fd, buf,
            static_cast<size_t>(sprintf(buf, "Granary crash PC = 0x%lx\n",
                                        granary_crash_pc)));
    }

    if (granary_crash_addr) {
      write(fd, buf,
            static_cast<size_t>(sprintf(buf, "Granary crash ADDR = 0x%lx\n",
                                        granary_crash_addr)));

    }

#ifdef GRANARY_TARGET_debug
    write(STDERR_FILENO, buf,
          static_cast<size_t>(sprintf(buf, "PID to attach GDB: %d\n", getpid())));
    read(STDIN_FILENO, buf, 1);
#else
    write(fd, "\n", 1);
    fsync(fd);
#endif
    close(fd);
  }
#else
  GRANARY_UNUSED(error);
  GRANARY_UNUSED(loc);
#endif
  exit(EXIT_FAILURE);
}
#pragma clang diagnostic pop

void granary_curiosity(const char *cond) {
  if (cond) {
    GRANARY_USED(cond);
  }
}

}  // extern C
