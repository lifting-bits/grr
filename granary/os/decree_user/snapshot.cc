/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/os/snapshot.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <gflags/gflags.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "granary/os/process.h"

DECLARE_string(snapshot_dir);
DECLARE_string(output_snapshot_dir);

namespace granary {
namespace os {
namespace {
static char gSnapshotPath[128] = {'\0'};
static char gMem[kPageSize] = {'\0'};
}  // namespace

// Copy the memory from one process directly into a memory mapped file.
void SnapshotRange(const detail::MappedRange32 &source_range, int source_fd,
                   int dest_fd) {
  auto desired_size = static_cast<ssize_t>(source_range.Size());
  for (ssize_t copied_size = 0; copied_size < desired_size; ) {
    auto size = std::min(static_cast<size_t>(desired_size - copied_size),
                         sizeof gMem);

    GRANARY_IF_ASSERT( errno = 0; )
    lseek64(source_fd, static_cast<off64_t>(source_range.begin) + copied_size,
            SEEK_SET);
    GRANARY_ASSERT(!errno && "Unable to seek to mapped memory range.");

    GRANARY_IF_ASSERT( errno = 0; )
    auto ret_size = read(source_fd, gMem, size);
    if (0 > ret_size) {
      if (EAGAIN == errno || EWOULDBLOCK == errno || EINTR == errno) continue;
      GRANARY_ASSERT(false && "Failed to read memory for snapshotting.");
    } else {
      GRANARY_IF_ASSERT( errno = 0; )
      auto write_ret_size = write(dest_fd, gMem, static_cast<size_t>(ret_size));
      GRANARY_ASSERT(!errno && "Failed to write memory to snapshot.");
      copied_size += write_ret_size;
    }
  }
}

namespace {
enum {
  kMaxNumAttempts = 100
};

// Enable tracing of the target binary.
static void EnableTracing(void) {
  for (auto i = 0; i < kMaxNumAttempts; i++) {
    if (!ptrace(PTRACE_TRACEME, 0, nullptr, nullptr)) {
      raise(SIGSTOP);
      return;
    }
  }
  exit(EXIT_FAILURE);
}

// Wait for system call entry. This will return `false` if we didn't reach the
// expected stopped state. A Failure could mean that the process died or was
// signaled or is looping infinitely. These failure cases should be handled by
// the caller.
static bool WaitForSyscallEntry(pid_t pid) {
  for (int i = kMaxNumAttempts, status = 0; i-- > 0; status = 0) {
    ptrace(PTRACE_SYSCALL, pid, nullptr, nullptr);
    waitpid(pid, &status, 0);
    if (WIFSTOPPED(status)) {
      if ((SIGTRAP | 0x80) == WSTOPSIG(status)) return true;
    } else {
      break;
    }
  }
  return false;
}

// Wait for us to reach the syscall-exit-stop state. This should be called
// after calling `WaitForSyscallEntry`.
static bool WaitForSyscallExit(pid_t pid) {
  return WaitForSyscallEntry(pid);
}

// Attach to the binary and wait for it to raise `SIGSTOP`.
static void TraceBinary(pid_t pid) {
  for (int status = 0; !WIFSTOPPED(status); ) {
    if (-1 == waitpid(pid, &status, 0)) {
      kill(pid, SIGKILL);
      exit(EXIT_FAILURE);
    }
  }
  ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_TRACESYSGOOD);
}

// Copy the register state from a process.
static struct user_regs_struct GetGRegs(pid_t pid) {
  struct user_regs_struct regs;
  ptrace(PTRACE_GETREGS, pid, NULL, &regs);
  regs.rdi = 0;
  regs.rsi = 0;
  regs.rbp = 0;
  regs.rbx = 0;
  regs.rdx = 0;
  regs.rcx = 0;
  regs.rax = 0;
  regs.orig_rax = 0;
  regs.rsp = 0xbaaaaffcU;
  //regs.eip = file->meta.eip;  // This is was we really want.
  regs.eflags = 0x202;

  return regs;
}

// Copy the floating-point register state from a process.
static struct user_fpregs_struct GetFPRegs(pid_t pid) {
  struct user_fpregs_struct fpregs;
  ptrace(PTRACE_GETFPREGS, pid, NULL, &fpregs);
  return fpregs;
}

static size_t GetLine(std::string::iterator &curr, std::string::iterator end,
                      std::string &line){
  line.clear();
  while (curr != end) {
    auto ch = *curr++;
    if ('\n' == ch) break;
    line.push_back(ch);
  }
  auto len = line.size();
  line.push_back('\0');  // Lets us use `line.data()` as a cstr.
  return len;
}

static std::string ReadMapsFile(pid_t pid) {
  sprintf(gSnapshotPath, "/proc/%d/maps", pid);
  GRANARY_IF_ASSERT( errno = 0; )
  auto fd = open(gSnapshotPath, O_RDONLY);
  GRANARY_ASSERT(!errno && "Unable to open maps file of subprocess.");

  std::string contents;
  contents.reserve(kPageSize);

  for (;;) {
    auto ret = read(fd, gMem, kPageSize);
    if (!ret) break;
    if (0 < ret) {
      contents.insert(contents.end(), gMem, gMem + ret);
    } else {
      GRANARY_ASSERT((EAGAIN == errno || EINTR == errno) &&
                     "Could not read memory maps for snapshotting.");
    }
  }

  return contents;
}

// Copy the memory address ranges from a process.
static std::vector<detail::MappedRange32> GetRanges(pid_t pid, AppPC32 eip) {
  std::vector<detail::MappedRange32> ranges;
  std::string line;
  auto maps_file = ReadMapsFile(pid);
  auto file_curr = maps_file.begin();
  auto file_end = maps_file.end();
  while (auto length = GetLine(file_curr, file_end, line)) {
    if (22 > length) continue;

    // Things like [stack], [vdso], [vvar], etc.
    auto cline = line.data();
    if (strchr(cline, '[') && strchr(cline, ']')) {
      if (!strstr(cline, "[heap]")) continue;
    }

    if ('-' != cline[8] || ' ' != cline[17] || 'p' != cline[21]) continue;
    detail::MappedRange32 range;
    memset(&range, 0, sizeof range);
    range.is_r = ('r' == cline[18]);
    range.is_w = ('w' == cline[19]);
    range.is_x = ('x' == cline[20]);
    if (2 != sscanf(cline, "%x-%x", &(range.begin), &(range.end))) continue;
    range.lazy_begin = range.begin;

    // Auto-split this range in two if it contains the EIP. This is so that we
    // can hopefully "pre-split" the binary which typically combines .text
    // and .data into one RWX region.
    if ((range.begin + kPageSize) <= eip &&  // Middle-ish of range.
        eip < range.end &&
        kPageSize < (range.end - range.begin)) {
      auto old_end = range.end;
      range.end = eip & kPageMask;
      ranges.push_back(range);

      range.begin = range.end;
      range.lazy_begin = range.begin;
      range.end = old_end;
    }

    ranges.push_back(range);
  }
  return ranges;
}

// Open the snapshot file.
static int OpenSnapshotFile(int exe_num, bool create) {
  sprintf(gSnapshotPath, "%s/grr.snapshot.%d.persist",
          FLAGS_snapshot_dir.c_str(), exe_num);

  auto flags = 0;
  auto mode = 0;
  if (create) {
    flags = O_CREAT | O_TRUNC | O_RDWR | O_LARGEFILE;
    mode = 0666;
  } else {
    flags = O_RDONLY | O_LARGEFILE;
  }

  GRANARY_IF_ASSERT( errno = 0; )
  auto fd = open(gSnapshotPath, flags, mode);
  GRANARY_ASSERT(!errno && "Unable to open the snapshot file.");

  return fd;
}

static char * const gArgvEnvp[] = {nullptr};

// Initialize the snapshot file.
static void InitSnapshotFile(const char *exe_name, int exe_num, int fd) {
  if (auto pid = fork()) {
    if (-1 == pid) exit(EXIT_FAILURE);
    TraceBinary(pid);

    // Walk over the `exec`.
    WaitForSyscallEntry(pid);
    WaitForSyscallExit(pid);

    // Align the file handle to the end of the file.
    GRANARY_IF_ASSERT( errno = 0; )
    ftruncate64(fd, static_cast<off64_t>(sizeof(detail::Snapshot32File)));
    GRANARY_ASSERT(!errno && "Unable to scale snapshot file.");

    auto file = new (mmap(nullptr, sizeof(detail::Snapshot32File),
                          PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0))
                    detail::Snapshot32File;
    GRANARY_ASSERT(!errno && "Unable to mmap initial Snapshot32File.");

    // Map the snapshot file into memory.
    memset(file, 0, sizeof *file);

    file->meta.magic[0] = 'G';
    file->meta.magic[1] = 'R';
    file->meta.magic[2] = 'R';
    file->meta.magic[3] = 'S';
    file->meta.exe_num = exe_num;
    file->meta.gregs = GetGRegs(pid);
    file->meta.fpregs = GetFPRegs(pid);

    // Copy the process memory.
    auto ranges = GetRanges(pid, static_cast<AppPC32>(file->meta.gregs.rip));
    auto size = sizeof(detail::Snapshot32File);
    for (const auto &range : ranges) {
      size += range.Size();
    }

    sprintf(gSnapshotPath, "/proc/%d/mem", pid);
    GRANARY_IF_ASSERT( errno = 0; )
    auto mem_fd = open(gSnapshotPath, O_RDONLY | O_LARGEFILE);
    GRANARY_ASSERT(!errno && "Unable to open memory file of subprocess.");

    // For each readable memory range in the process, copy the contents of that
    // range into the snapshot file, and copy the range into the snapshot file
    // as well.
    auto i = 0;
    auto snapshot_offset = static_cast<uint32_t>(sizeof(detail::Snapshot32File));
    for (const auto &range : ranges) {
      if (!range.is_r) {
        continue;
      }
      GRANARY_ASSERT(detail::Snapshot32File::kMaxNumMappedRanges > i);
      GRANARY_ASSERT(range.begin == range.lazy_begin);
      file->ranges[i] = range;
      file->ranges[i].fd_offs = snapshot_offset;
      snapshot_offset += range.Size();
      i += 1;
    }

    // Initialize the stack range.
    GRANARY_ASSERT(detail::Snapshot32File::kMaxNumMappedRanges > i);
    file->ranges[i].begin = kStackBegin;
    file->ranges[i].end = kStackEnd;
    file->ranges[i].lazy_begin = kStackEnd - kMappedStackSize;
    file->ranges[i].fd_offs = snapshot_offset;
    file->ranges[i].is_r = true;
    file->ranges[i].is_w = true;
    file->ranges[i].is_x = true;
    snapshot_offset += kStackSize;
    i += 1;

    // Persist the snapshot file meta-data.
    GRANARY_IF_ASSERT( errno = 0; )
    msync(file, sizeof *file, MS_SYNC | MS_INVALIDATE);
    munmap(file, sizeof *file);
    file = nullptr;
    GRANARY_ASSERT(!errno && "Unable to write snapshot meta-data.");

    // Align the file handle to the end of the file.
    GRANARY_IF_ASSERT( errno = 0; )
    ftruncate64(fd, static_cast<off64_t>(sizeof(detail::Snapshot32File)));
    GRANARY_ASSERT(!errno && "Unable to scale snapshot file (1)");

    lseek64(fd, 0, SEEK_END);
    GRANARY_ASSERT(!errno && "Unable to seek to end of snapshot file (1)");

    // Copy the memory into the snapshot file.
    for (const auto &range : ranges) {
      if (!range.is_r) continue;
      SnapshotRange(range, mem_fd, fd);
    }

    // Add in the stack.
    ftruncate64(fd, static_cast<off64_t>(snapshot_offset));
    GRANARY_ASSERT(!errno && "Unable to scale snapshot file (2)");

    // Add in the magic page.
    lseek64(fd, 0, SEEK_END);
    GRANARY_ASSERT(!errno && "Unable to seek to end of snapshot file (2)");

    // Clean up.
    close(mem_fd);
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, WNOHANG);

  } else {
    EnableTracing();
    execve(exe_name, gArgvEnvp, gArgvEnvp);
    GRANARY_ASSERT(false && "Unable to `exec` process.");
    __builtin_unreachable();
  }
}

static const auto kELFHeader = "\177ELF\x01\x01\x01\x00\x00\x00\x00\x00"
                               "\x00\x00\x00";

enum {
  kELFHeaderLen = 15
};

// Creates a copy of a 32-bit CGC program, then changes the program into am
// ELF binary format.
static std::string CopyExecutableFile(const char *exe_path, int exe_num) {
  GRANARY_IF_ASSERT( errno = 0; )
  auto fd = open(exe_path, O_RDONLY);
  GRANARY_ASSERT(!errno && "Executable does not exist.");

  auto new_path = FLAGS_snapshot_dir;
  if (new_path.length()) {
    new_path += "/";
  }
  new_path += ".grr_exe.";
  new_path += std::to_string(exe_num);

  auto path = new_path.c_str();
  auto new_fd = open64(path, O_CREAT | O_CLOEXEC | O_RDWR | O_TRUNC, 0777);
  GRANARY_ASSERT(!errno && "Could not create copy of the executable.");

  struct stat64 stat;
  fstat64(fd, &stat);
  GRANARY_ASSERT(!errno && "Could not `fstat` the original executable.");

  write(new_fd, kELFHeader, kELFHeaderLen);
  GRANARY_ASSERT(!errno && "Could add header to new executable.");

  off_t after_header_offs = kELFHeaderLen;
  sendfile(new_fd, fd, &after_header_offs,
           static_cast<size_t>(stat.st_size - kELFHeaderLen));
  GRANARY_ASSERT(!errno && "Could not copy the original executable.");

  lseek64(new_fd, 0, SEEK_SET);
  GRANARY_ASSERT(!errno && "Could not seek to beginning of new executable.");

  // Make ELF binary executable.
  fchmod(new_fd, 0777);
  GRANARY_ASSERT(!errno && "Could not make ELF binary executable.");

  // Close the old file, and re-open the new file as read-only so that we can
  // execute it with `execve`.
  close(fd);
  close(new_fd);

  return new_path;
}

}  // namespace

// Revives a snapshot file.
Snapshot32::Snapshot32(int exe_num_)
    : fd(OpenSnapshotFile(exe_num_, false)),
      exe_num(exe_num_) {
  GRANARY_IF_ASSERT( errno = 0; )
  file = reinterpret_cast<detail::Snapshot32File *>(
      mmap64(nullptr, sizeof(detail::Snapshot32File),
             PROT_READ, MAP_POPULATE | MAP_PRIVATE, fd, 0));
  GRANARY_ASSERT(!errno && "Unable to map snapshot file meta-data.");
  GRANARY_ASSERT(file->meta.exe_num == exe_num &&
                 "Unexpected executable number in snapshot file meta-data.");
}

// Creates a snapshot file.
void Snapshot32::Create(const char *exe_name, int exe_num) {
  auto exe_path = CopyExecutableFile(exe_name, exe_num);
  auto snapshot_fd = OpenSnapshotFile(exe_num, true);
  auto exe_path_str = exe_path.c_str();
  InitSnapshotFile(exe_path_str, exe_num, snapshot_fd);
  unlink(exe_path_str);
}

// Creates the snapshot from a process.
void Snapshot32::Create(const Process32 *process) {

  // Size of the snapshot file.
  auto size = sizeof(detail::Snapshot32File);
  for (const auto &page : process->pages) {
    if (PagePerms::kInvalid != page.perms &&
        PageState::kReserved != page.state) {
      size += (page.limit - page.base);
    }
  }

  std::stringstream temp_snapshot_path_ss;
  temp_snapshot_path_ss << FLAGS_output_snapshot_dir << "/.tmp/"
                        << getpid() << "." << process->Id();
  auto temp_path =  temp_snapshot_path_ss.str();
  auto temp_snapshot_path = temp_path.c_str();

  GRANARY_IF_ASSERT( errno = 0; )
  auto fd = open64(temp_snapshot_path,
                   O_CREAT | O_CLOEXEC | O_RDWR | O_TRUNC, 0666);
  GRANARY_ASSERT(!errno && "Unable to create snapshot file.");

  ftruncate64(fd, static_cast<off64_t>(size));
  GRANARY_ASSERT(!errno && "Unable to scale snapshot file.");

  auto file = reinterpret_cast<detail::Snapshot32File *>(
      mmap64(nullptr, size,
             PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  GRANARY_ASSERT(!errno && "Unable to map snapshot file meta-data.");

  file->meta.magic[0] = 'G';
  file->meta.magic[1] = 'R';
  file->meta.magic[2] = 'R';
  file->meta.magic[3] = 'S';
  file->meta.exe_num = process->Id();
  file->meta.gregs.rdi = process->regs.edi;
  file->meta.gregs.rsi = process->regs.esi;
  file->meta.gregs.rbp = process->regs.ebp;
  file->meta.gregs.rbx = process->regs.ebx;
  file->meta.gregs.rdx = process->regs.edx;
  file->meta.gregs.rcx = process->regs.ecx;
  file->meta.gregs.rax = process->regs.eax;
  file->meta.gregs.rsp = process->regs.esp;
  file->meta.gregs.rip = process->regs.eip - 2;  /* Size of INT 0x80 */
  file->meta.gregs.eflags = process->regs.eflags;
  file->meta.fpregs = process->fpregs;

  auto i = 0;
  auto offset = static_cast<uint32_t>(sizeof(detail::Snapshot32File));
  auto data = reinterpret_cast<uint8_t *>(file);

  for (const auto &page : process->pages) {
    if (PagePerms::kInvalid == page.perms ||
        PageState::kReserved == page.state ||
        !(page.limit - page.base)) {
      continue;
    }

    auto &range = file->ranges[i++];
    auto range_size = page.limit - page.base;

    range.begin = page.base;
    range.end = page.limit;
    range.lazy_begin = page.lazy_base;
    range.fd_offs = offset;

    switch (page.perms) {
      case PagePerms::kInvalid: break;
      case PagePerms::kRO:
        range.is_r = true;
        break;
      case PagePerms::kRW:
        range.is_r = true;
        range.is_w = true;
        break;
      case PagePerms::kRWX:
        range.is_r = true;
        range.is_w = true;
        range.is_x = true;
        break;
      case PagePerms::kRX:
        range.is_r = true;
        range.is_x = true;
        break;
    }

    GRANARY_ASSERT(static_cast<size_t>(offset) < size &&
                   "Invalid snapshot file size.");

    auto lazy_offset = page.lazy_base - page.base;

    for (Addr32 j = 0; j < lazy_offset; ++j) {
      data[offset + j] = 0;
    }

    for (Addr32 a = page.lazy_base, o = 0; a < page.limit; ++a, ++o) {
      auto b = reinterpret_cast<uint8_t *>(process->base) + a;
      data[offset + lazy_offset + o] = *b;
    }

    offset += range_size;
  }

  GRANARY_ASSERT(static_cast<size_t>(offset) == size &&
                 "Invalid snapshot file size.");

  msync(data, size, MS_SYNC | MS_INVALIDATE);
  GRANARY_ASSERT(!errno && "Couldn't sync snapshot file (1).");
  fsync(fd);
  GRANARY_ASSERT(!errno && "Couldn't sync snapshot file (2).");
  close(fd);
  GRANARY_ASSERT(!errno && "Couldn't close snapshot file.");

  std::stringstream snapshot_path_ss;
  snapshot_path_ss << FLAGS_output_snapshot_dir << "/grr.snapshot."
                   << process->Id() << ".persist";

  auto path = snapshot_path_ss.str();
  rename(temp_path.c_str(), path.c_str());
  GRANARY_ASSERT(!errno && "Couldn't commit to snapshot file.");
}

}  // namespace os
}  // namespace granary
