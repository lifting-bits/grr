/* Copyright 2015 Peter Goodman, all rights reserved. */

#include <cstdio>
#include <iostream>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <gflags/gflags.h>

#include "granary/os/snapshot.h"

DEFINE_bool(about, false, "Show the compile date of grr.");

DEFINE_string(snapshot_dir, "", "Directory where snapshots are be stored.");

DEFINE_string(output_snapshot_dir, "", "Directory where a state snapshot will "
                                       "be placed. The state snapshot is taken "
                                       "just before the first system call that "
                                       "isn't a (de)allocate. Only one set of "
                                       "snapshots are produced. If more are "
                                       "needed then you need to 'walk up' to "
                                       "one using repeated invocations.");

DEFINE_string(exe_dir, "", "Path to the directory containing executables "
                           "1 through N, where N is `--num_exe`.");

DEFINE_string(exe_prefix, "", "Prefix of each executable name.");

DEFINE_int32(num_exe, 1, "Number of executables to run.");

namespace granary {
namespace {

// Name of the executable to snapshot.
static char gExeName[256] = {'\0'};

// Fail if `gExeName` is not an executable file.
static void AssertExecutableExists(void) {
  struct stat file_info;
  if (stat(gExeName, &file_info) || !(file_info.st_mode & S_IXUSR)) {
    std::cerr << "File `" << gExeName << "` does not exist or is not "
              << "executable" << std::endl;
    exit(EXIT_FAILURE);
  }
}

}  // namespace
}  // namespace granary

extern "C" int main(int argc, char **argv, char **) {
  using namespace granary;
  GFLAGS_NAMESPACE::SetUsageMessage("grr_snapshot [options]");
  GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, false);

  FLAGS_output_snapshot_dir = FLAGS_snapshot_dir;

  if (FLAGS_about) {
    std::cerr << __DATE__ ", " __TIME__ << std::endl;
    return EXIT_SUCCESS;
  }

  if (0 >= FLAGS_num_exe) {
    std::cerr << "One or more executables must be available." << std::endl;
    return EXIT_FAILURE;
  }

  if (FLAGS_snapshot_dir.empty()) {
    std::cerr << "Must provide a unique path to a directory where the "
              << "snapshots are located persisted." << std::endl;
    return EXIT_FAILURE;
  } else {
    mkdir(FLAGS_snapshot_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  }

  signal(SIGCHLD, SIG_IGN);

  // Create the various snapshot files.
  for (auto i = 1; i <= FLAGS_num_exe; ++i) {
    sprintf(gExeName, "%s/%s%04x", FLAGS_exe_dir.c_str(),
            FLAGS_exe_prefix.c_str(), i);
    AssertExecutableExists();
    os::Snapshot32::Create(gExeName, i);
  }

  return 0;
}

