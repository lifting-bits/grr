/* Copyright 2015 Peter Goodman (peter@trailofbits.com), all rights reserved. */

#include <gflags/gflags.h>

#include <iostream>

#include "granary/code/index.h"

DEFINE_bool(persist, true, "Should the code cache be persisted?");

DEFINE_string(persist_dir, "", "Directory path to where runtime state should "
                               "be persisted. This should be unique for a "
                               "given set of binaries.");

extern "C" int main(int argc, char **argv, char **) {
  using namespace granary;
  GFLAGS_NAMESPACE::SetUsageMessage(std::string(argv[0]) + " [options]");
  GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, false);

  if (FLAGS_persist_dir.empty()) {
    std::cerr << "Must provide a unique path to a directory where the "
              << "runtime state can be persisted." << std::endl;
    return EXIT_FAILURE;
  }

  index::Init();

  index::Dump();

  return EXIT_SUCCESS;
}
