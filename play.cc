/* Copyright 2015 Peter Goodman, all rights reserved. */

#include <cstdio>
#include <iostream>
#include <sstream>

#include <set>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <gflags/gflags.h>

#include "granary/arch/base.h"

#include "granary/code/branch_tracer.h"
#include "granary/code/cache.h"
#include "granary/code/index.h"
#include "granary/code/coverage.h"

#include "granary/input/record.h"
#include "granary/input/mutate.h"

#include "granary/os/process.h"
#include "granary/os/snapshot.h"
#include "granary/os/schedule.h"

#include "third_party/md5/md5.h"

DEFINE_bool(about, false, "Show the compile date of grr.");

DEFINE_string(snapshot_dir, "", "Directory where snapshots are stored.");

DEFINE_string(output_snapshot_dir, "", "Directory where a state snapshot will "
                                       "be placed. The state snapshot is taken "
                                       "just before the first system call that "
                                       "isn't a (de)allocate. Only one set of "
                                       "snapshots are produced. If more are "
                                       "needed then you need to 'walk up' to "
                                       "one using repeated invocations.");

DEFINE_int32(snapshot_before_input_byte, 0,
             "Produce a snapshot file as late as possible but before the "
             "Nth input byte is received.");

DEFINE_bool(persist, true, "Should the code cache be persisted?");

DEFINE_string(persist_dir, "", "Directory path to where runtime state should "
                               "be persisted. This should be unique for a "
                               "given set of binaries.");

DEFINE_int32(num_exe, 1, "Number of executables to run.");

DEFINE_string(input, "", "Path to an input testcase to replay.");

DEFINE_string(output_dir, "", "Directory where output testcases "
                                           "are stored.");

DEFINE_string(input_mutator, "", "What input testcase mutator should "
                                 "be used for running the tests?");

DEFINE_int32(num_tests, 0, "The maximum number of testcases to run.");

DEFINE_bool(remutate, false, "Enable remutating of some stuff.");

DEFINE_bool(path_coverage, false, "Enable path code coverage?");

DEFINE_string(coverage_file, "/dev/null",
              "File name in which to save the code coverage data. If "
              "/dev/null is specified then the coverage file is not "
              "mapped, but coverage instrumentation is enabled.");

DEFINE_string(output_coverage_file, "/dev/null",
              "File name in which to save the code coverage data. If "
              "/dev/null is specified then the coverage file is not "
              "mapped, but coverage instrumentation is enabled.");

DEFINE_bool(print_num_mutations, false,
            "Print out the number of mutations evaluated.");

namespace granary {

std::string gInput = "";
extern "C" size_t gInputIndex = 0;

namespace {

enum {
  kGiveUpAfterEmptyMutations = 5
};

static uintptr_t gNumMutations = 0;
static uintptr_t gTotalInputBytes = 0;
static uintptr_t gTotalInputBytesRead = 0;

static input::IORecording *gRecordToMutate = nullptr;

// Creates and returns a snapshot group, where each snapshot is the initial
// memory and register state of a bunch of related processes.
static os::SnapshotGroup CreateSnapshotGroup(void) {
  os::SnapshotGroup snapshots;
  snapshots.reserve(static_cast<size_t>(FLAGS_num_exe));
  for (auto i = 1; i <= FLAGS_num_exe; ++i) {
    snapshots.push_back(os::Snapshot32::Revive(i));
  }
  return snapshots;
}

// Creates and returns a process group from a snapshot group.
static os::Process32Group CreateProcess32Group(
    const os::SnapshotGroup &snapshots) {
  os::Process32Group processes;
  processes.reserve(snapshots.size());
  for (const auto &snapshot : snapshots) {
    processes.push_back(os::Process32::Revive(snapshot));
  }
  return processes;
}

static bool IsCrash(const granary::os::Process32Group &processes) {
  for (auto process : processes) {
    if (granary::os::ProcessStatus::kError == process->status) {
      return true;
    }
  }
  return false;
}

static void PublishNewInput(std::string &&input,
                            bool is_crash,
                            bool covered_new_code) {
  GRANARY_ASSERT(!FLAGS_output_dir.empty());

  if (!input.size()) {
    return;
  }

  std::stringstream final_path;
  std::stringstream temp_path;
  temp_path << FLAGS_output_dir << "/.tmp";
  final_path << FLAGS_output_dir << "/";

  // Try to create a temporary output directory for this input.
  std::string temp_dir = temp_path.str();
  mkdir(temp_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  errno = 0;  // Suppress failures to make the directory.

  temp_path << "/";
  if (is_crash) {
    temp_path << "crash." << getpid();
    final_path << "crash.";
  } else {
    temp_path << "input." << getpid();
    final_path << "input.";
  }

  auto temp_file = temp_path.str();
  auto fd = open(
      temp_file.c_str(), O_CREAT | O_TRUNC | O_RDWR | O_LARGEFILE, 0666);
  GRANARY_ASSERT(!errno && "Unable to open the temp output file.");

  write(fd, input.c_str(), input.size());
  GRANARY_ASSERT(!errno && "Unable to write the temp output file.");

  close(fd);

  // Name the file in terms of its code coverage, and the input byte index
  // after which new code coverage was first detected.
  if (!is_crash && covered_new_code) {
    final_path << "cov." << code::PathCoverageHash();

    // Number of paths covered.
    final_path << ".size." << code::GetNumCoveredPaths();

    // Append to the name the index of first input character after which
    // new coverage is produced.
    if (covered_new_code) {
      auto ilen = code::GetCoveredInputLength();
      if (ilen < gInput.size()) {
        final_path << ".at." << ilen;
      }
    }

  // Name the file in terms of its data.
  } else {
    final_path << "data.";
    MD5 hash;
    hash.update(input);
    hash.finalize();
    final_path << hash.hexdigest();
  }


  // Atomically make the new file visible. This permits polling of the output
  // directory that will see a consistent view of all the files.
  auto final_file = final_path.str();
  if (0 != rename(temp_file.c_str(), final_file.c_str())) {
    errno = 0;
    unlink(temp_file.c_str());
    GRANARY_ASSERT(!errno && "Couldn't remove temporary testcase.");
    GRANARY_ASSERT(false && "Couldn't publish testcase to output directory.");
  }
}

// Runs a testcase, and return `true` if we should continue running.
static bool RunTestCase(const granary::os::SnapshotGroup &snapshot_group) {
  auto process_group = CreateProcess32Group(snapshot_group);

  // Record the individual syscalls executed.
  auto first_execution = !gRecordToMutate;
  auto mutating = !FLAGS_input_mutator.empty();
  auto publishing = !FLAGS_output_dir.empty();

  input::gRecord = new input::IORecording;
  gInputIndex = 0;

  code::BeginPathCoverage();
  auto got_term_signal = os::Run(process_group);
  code::EndPathCoverage();

  std::string output;

  const auto is_crash = IsCrash(process_group);
  const auto covered_new_code = code::CoveredNewPaths();
  if (publishing) {

    // If we're doing a mutation run, the the first execution is really just
    // a way of setting up an initial recording of the input test for later
    // mutation, so we don't want to publish it because that would be redundant.
    // That is, we expect that what we get as input for mutation has already
    // been processed and therefore cannot contribute new information to a
    // downstream tool.
    if (mutating) {
      if (!first_execution) {
        ++gNumMutations;
        gTotalInputBytes += gInput.size();
        gTotalInputBytesRead += input::gRecord->num_input_bytes;
      }
      if (!first_execution && (is_crash || covered_new_code)) {
        output = input::gRecord->ToInput();
      }

    // We're not mutating, so this is probably a "speculative" replay of some
    // input, where we don't know if the whole input will be processed by
    // the program. In this case, we want to produce an output that represents
    // a "corrected" version of the supplied input.
    //
    // This is useful for handling input mutated by a third-party system. That
    // system produces an input, but we want to preserve only the consumed parts
    // of that information.
    } else {
      output = input::gRecord->ToInput();
    }

    if (!output.empty()) {
      PublishNewInput(std::move(output), is_crash, covered_new_code);
    }
  }

  // We want to mutate code.
  if (mutating) {
    if (!gRecordToMutate) {
      gRecordToMutate = input::gRecord;
      input::gRecord = nullptr;

    // We're running a mutator, and the user has requested that we perform
    // input remutation when an input yields something "new" or interesting.
    // In this case, this is a kind of hill climbing, where when we find
    // new coverage, we set this new input as the one to mutate in the future.
    } else if (FLAGS_remutate && covered_new_code && !first_execution) {
      delete gRecordToMutate;
      gRecordToMutate = input::gRecord;
      input::gRecord = nullptr;
    }
  }

  // No longer need the record if it's still hanging around.
  if (input::gRecord) {
    delete input::gRecord;
    input::gRecord = nullptr;
  }

  // The user wants us to stop, so clean up even if we wanted to continue.
  if (got_term_signal && gRecordToMutate) {
    delete gRecordToMutate;
    gRecordToMutate = nullptr;
  }

  for (auto process : process_group) {
    delete process;
  }

  // If we weren't signaled, then we can keep going.
  return !got_term_signal;
}

}  // namespace
}  // namespace granary

extern "C" int main(int argc, char **argv, char **) {
  using namespace granary;
  GFLAGS_NAMESPACE::SetUsageMessage(std::string(argv[0]) + " [options]");
  GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, false);

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
  }

  if (FLAGS_persist) {
    if (FLAGS_persist_dir.empty()) {
      std::cerr << "Must provide a unique path to a directory where the "
                << "runtime state can be persisted." << std::endl;
      return EXIT_FAILURE;
    }
    mkdir(FLAGS_persist_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    errno = 0;  // Suppress failures to make the directory.
  }

  // Make sure we have a place to output testcases, as well as a special
  // temporary directory for collecting testcases before we move them into
  // the main directory.
  if (!FLAGS_output_dir.empty()) {
    mkdir(FLAGS_output_dir.c_str(),
          S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    errno = 0;  // Suppress failures to make the directory.

    mkdir((FLAGS_output_dir + "/.tmp").c_str(),
          S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    errno = 0;  // Suppress failures to make the directory.
  }

  if (!FLAGS_input_mutator.empty() && FLAGS_output_dir.empty()) {
    std::cerr << "Must specify --output_dir if using --input_mutator."
              << std::endl;
    return EXIT_FAILURE;
  }

  // Make sure we have a place to output snapshots, as well as a special
  // temporary directory for collecting snapshots before we move them into
  // the main directory.
  if (!FLAGS_output_snapshot_dir.empty()) {
    mkdir(FLAGS_output_snapshot_dir.c_str(),
          S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    errno = 0;  // Suppress failures to make the directory.

    mkdir((FLAGS_output_snapshot_dir + "/.tmp").c_str(),
          S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    errno = 0;  // Suppress failures to make the directory.
  } else {
    FLAGS_snapshot_before_input_byte = 0;
  }

  // Read in the input file.
  if (!FLAGS_input.empty()) {
    errno = 0;
    auto fd = open(FLAGS_input.c_str(), O_RDONLY | O_CLOEXEC | O_LARGEFILE);
    if (errno) {
      std::cerr << "Cannot open or parse file: " << FLAGS_input << std::endl;
      return EXIT_FAILURE;
    }

    struct stat file_info;
    fstat(fd, &file_info);
    GRANARY_ASSERT(!errno && "Unable to stat the input file.");

    auto size = static_cast<size_t>(file_info.st_size);
    if (size) {
      gInput.resize(size);
      read(fd, const_cast<char *>(gInput.data()), size);  // Super sketchy.
      GRANARY_ASSERT(!errno && "Unable to read input file data.");
    }
  }

  // We'll only allow ourselves to be interrupted by specific locations in
  // `os::Schedule`.
  Uninterruptible disable_interrupts;

  auto snapshot_group = CreateSnapshotGroup();

  code::InitBranchTracer();
  code::InitPathCoverage();
  arch::Init();
  index::Init();
  cache::Init();

  // Start by running the individual testcase. This acts as the normal replayer.
  // If branch coverage is enabled, then this also establishes the "base case"
  // for coverage that will determine if other tests are published.
  if (RunTestCase(snapshot_group) &&
      !FLAGS_input_mutator.empty()) {

    // Now try to mutate the input testcase using the mutator specified in the
    // command-line arguments.
    input::gRecord = new input::IORecording;

    while (gRecordToMutate) {
      auto old_record_to_mutate = gRecordToMutate;
      auto mutator = input::Mutator::Create(
          gRecordToMutate, FLAGS_input_mutator);

      gInput.clear();
      while (mutator && gRecordToMutate) {

        for (auto empty = 0;
             gInput.empty() && empty < kGiveUpAfterEmptyMutations;
             ++empty) {
          gInput = mutator->RequestMutation();
        }

        // Done mutating.
        if (gInput.empty()) {
          delete gRecordToMutate;
          gRecordToMutate = nullptr;
          break;
        }

        if (HasPendingInterrupt() ||
            !RunTestCase(snapshot_group) ||
            HasPendingInterrupt()) {

          if (gRecordToMutate) {
            delete gRecordToMutate;
            gRecordToMutate = nullptr;
            break;
          }

        } else if (old_record_to_mutate != gRecordToMutate) {
          delete mutator;
          mutator = input::Mutator::Create(
              gRecordToMutate, FLAGS_input_mutator);
        }
      }
      delete mutator;
    }
  }

  MutePendingInterrupt();

  if (gRecordToMutate) {
    delete gRecordToMutate;
    gRecordToMutate = nullptr;
  }

  if (input::gRecord) {
    delete input::gRecord;
    input::gRecord = nullptr;
  }

  for (auto snapshot : snapshot_group) {
    delete snapshot;
  }

  arch::Exit();
  index::Exit();
  cache::Exit();
  code::ExitPathCoverage();
  code::ExitBranchTracer();

  if (FLAGS_print_num_mutations) {
    std::cout << gNumMutations << " " << gTotalInputBytes << " "
              << gTotalInputBytesRead;
  }

  return EXIT_SUCCESS;
}
