/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"

#include "granary/input/record.h"

#include "granary/os/syscall.h"
#include "granary/os/file.h"
#include "granary/os/process.h"
#include "granary/os/snapshot.h"
#include "granary/os/decree_user/decree.h"

#include <iostream>

#include <gflags/gflags.h>


DEFINE_bool(strace, false, "Enable printing of system call traces.");

DECLARE_int32(num_exe);
DECLARE_string(output_snapshot_dir);
DECLARE_int32(snapshot_before_input_byte);

namespace granary {

extern std::string gInput;
extern "C" size_t gInputIndex;

namespace os {

namespace {

// Implements undefined system calls.
static SystemCallStatus BadSystemCall(SystemCallABI abi) {
  abi.SetReturn(DECREE_ENOSYS);
  return SystemCallStatus::kComplete;
}

// Implements the terminate system call.
static SystemCallStatus Terminate(const Process32 *process, SystemCallABI abi) {
  if (!FLAGS_output_snapshot_dir.empty()) {
    Snapshot32::Create(process);
    return SystemCallStatus::kTerminated;
  }

  abi.SetReturn(0);
  GRANARY_STRACE( std::cerr << process->pid << " terminate status="
                            << abi.Arg<1>() << std::endl; );
  GRANARY_UNUSED(process);
  return SystemCallStatus::kTerminated;
}

// Converts a `FileIOStatus` into a `SystemCallStatus`.
static SystemCallStatus ConvertStatus(SystemCallABI abi, FileIOStatus status) {
  switch (status) {
    case FileIOStatus::kCompleted:
      GRANARY_STRACE( std::cerr << std::endl; )
      abi.SetReturn(0);
      return SystemCallStatus::kComplete;

    case FileIOStatus::kFaulted:
      GRANARY_STRACE( std::cerr << std::endl; )
      abi.SetReturn(DECREE_EFAULT);
      return SystemCallStatus::kComplete;

    case FileIOStatus::kInProgress:
      GRANARY_STRACE( std::cerr << "(IPR)" << std::endl; )
      return SystemCallStatus::kInProgress;
  }
}


enum : size_t  {
  kMaxTrailingEmptyReceives = 2ULL,
};

// Implement writing to standard output of `data` by `process`.
uint32_t DoTransmit(os::Process32 *process, int fd, const uint8_t *data,
                  uint32_t count) {
  GRANARY_UNUSED(process);
  GRANARY_UNUSED(fd);

  if (count && input::gRecord) {
    std::string syscall_data;
    syscall_data.reserve(count);
    syscall_data.insert(syscall_data.begin(), data, data + count);

    // TODO(pag): Don't record output for now; we unconditionally add a "split"
    //            into `input::gRecord` in the `Transmit` function.
    //input::gRecord->AddOutput(std::move(syscall_data));
  }
  return count;
}

// Implement reading by `process` of standard input to `data`.
static uint32_t DoReceive(os::Process32 *process, int fd, uint8_t *data,
                          uint32_t count, bool &faulted) {
  GRANARY_UNUSED(fd);

  faulted = false;

  // Don't need to log this.
  if (!count) {
    return 0;
  }

  std::string recorded_input;
  recorded_input.reserve(count);

  size_t max_input_len = 0;
  char last_ch = '\0';

  for (auto i = 0ULL; i < count; ++i) {
    auto index = gInputIndex++;
    if (index >= gInput.size()) {

      // Walked too far off the end of the input.
      if ((index - gInput.size()) >= kMaxTrailingEmptyReceives) {
        GRANARY_DEBUG( std::cerr << "; DONE REPLAY" << std::endl; )
        NonMaskableInterrupt();

      } else {

      if (index >= gInput.size()) {
      if (!recorded_input.empty()) {
        break;  // At the end of `gInput`.

      // Walked too far off the end of the input.
      } else if ((index - gInput.size()) >= kMaxTrailingEmptyReceives) {
        GRANARY_DEBUG( std::cerr << "; DONE REPLAY" << std::endl; )
        NonMaskableInterrupt();

      } else {
        return 0;
      }
      }
      }
    }

    auto ch = gInput[index];
    last_ch = ch;

    if (process->TryWrite(data + i, ch)) {
      ++max_input_len;
      recorded_input.append(1, ch);

    // Faulted during the `receive`.
    } else {
      faulted = true;
      break;
    }
  }

  if (faulted) {
    recorded_input.append(1, last_ch);  // To force the fault in a replay.
  }

  if (input::gRecord) {
    input::gRecord->AddInput(std::move(recorded_input));
  }

  return static_cast<uint32_t>(max_input_len);
}


// Implement generation of `count` random bytes of data into `data`.
static void DoRandom(os::Process32 *process, uint8_t *data, uint32_t count) {
  for (auto i = 0U; i < count; i++) {
    if (!process->TryWrite(data, 0)) {
      GRANARY_ASSERT(false && "Should be able to write `count` random bytes.");
    }
  }
}

enum {
  kLinuxArbitraryMinumumEscapeSize = 2048U  // Found by trial and error.. WTF?
};

// Counts the number of readable bytes in a range.
static uint32_t CountChunkedReadableBytes(Process32 *process, uint8_t *buf,
                                          uint32_t length) {
  auto i = 0U;
  for (auto chunk = 0U; chunk < length; ) {
    auto next_chunk = chunk + kLinuxArbitraryMinumumEscapeSize;
    for (; i < length && i < next_chunk; ++i) {
      uint8_t val = 0;
      if (!process->TryRead(&(buf[i]), val)) return chunk;
    }
    chunk = next_chunk;
  }
  return i;
}

// Implements the `transmit` DECREE system call.
static SystemCallStatus Transmit(Process32 *process,
                                 FileTable &files,
                                 SystemCallABI abi) {

  if (!FLAGS_output_snapshot_dir.empty() &&
      !FLAGS_snapshot_before_input_byte) {
    Snapshot32::Create(process);
    return SystemCallStatus::kTerminated;
  }

  if (input::gRecord) {
    input::gRecord->AddSplit();
  }

  const auto fd = abi.Arg<1>();
  const auto buf = abi.Arg<2,uint8_t *>();
  const auto length = abi.Arg<3,uint32_t>();
  const auto tx_bytes = abi.Arg<4,uint32_t *>();
  const auto num_fds = static_cast<int>(files.size());

  GRANARY_STRACE( std::cerr << process->Id() << " transmit length=" << length
                            << " fd=" << fd
                            << " buf=0x" << std::hex << (abi.Arg<2,uint32_t>())
                            << " tx_bytes=0x" << std::hex
                            << (abi.Arg<4,uint32_t>()) << std::dec << " -> "; )

  // Figure out how much to send. Linux seems to have this weird minimum
  // amount of bytes that can escape before it will EFAULT, so we'll respect
  // that.
  auto num_bytes = length;
  if (num_bytes) {
    num_bytes = CountChunkedReadableBytes(process, buf, length);
    if (!num_bytes) {
      GRANARY_STRACE( std::cerr << "EFAULT (buf)" << std::endl; )
      abi.SetReturn(DECREE_EFAULT);
      return SystemCallStatus::kComplete;
    }
  }

  if (DECREE_STDIN == fd || DECREE_STDOUT == fd || DECREE_STDERR == fd) {
    if (num_bytes && DECREE_STDERR != fd) {
      num_bytes = DoTransmit(process, fd, buf, num_bytes);
    }
    if (tx_bytes && !process->TryWrite(tx_bytes, num_bytes)) {
      GRANARY_STRACE( std::cerr << "EFAULT (tx_bytes)" << std::endl; )
      abi.SetReturn(DECREE_EFAULT);
    } else {
      GRANARY_STRACE( std::cerr << "length=" << num_bytes << std::endl; )
      abi.SetReturn(0);
    }
    return SystemCallStatus::kComplete;

  // Invalid FD.
  // TODO(pag): It seems like `num_fds` and `num_fds+1` might actually
  //            alias STDIN/STDOUT in `cb-server`. Double check this.
  } else if (0 > fd || fd >= num_fds) {
    GRANARY_STRACE( std::cerr << "EBADF (fd)" << std::endl; )
    abi.SetReturn(DECREE_EBADF);
    return SystemCallStatus::kComplete;

  // For non-STDIN/STDOUT/STDERR, the chunking behavior doesn't apply.
  } else if (num_bytes < length) {
    GRANARY_STRACE( std::cerr << "EFAULT (buf)" << std::endl; )
    abi.SetReturn(DECREE_EFAULT);
    return SystemCallStatus::kComplete;

  // Okay we can do this.
  } else {
    auto file = files[static_cast<unsigned>(fd)];
    auto status = ConvertStatus(
        abi, file->Write(process, tx_bytes, buf, num_bytes, fd));
    GRANARY_STRACE( std::cerr << std::endl; )
    return status;
  }
}

// Implements the `receive` DECREE system call.
static SystemCallStatus Receive(Process32 *process,
                                FileTable &files,
                                SystemCallABI abi) {


  const auto fd = abi.Arg<1>();
  const auto buf = abi.Arg<2,uint8_t *>();
  const auto length = abi.Arg<3,uint32_t>();
  const auto rx_bytes = abi.Arg<4,uint32_t *>();
  const auto num_fds = static_cast<int>(files.size());

  // If we're not counting down to a specific number of receives, then
  // `FLAGS_snapshot_after_num_receives` will be zero, so we'll snapshot
  // right away. However, if we are, then we don't want to trigger snapshotting
  // in non-`receive` syscalls, so we check against `1` and make sure that
  // the zero check is never triggered.
  if (!FLAGS_output_snapshot_dir.empty()) {
    auto max_byte_index = static_cast<size_t>(FLAGS_snapshot_before_input_byte);
    if ((gInputIndex + length) >= max_byte_index) {
      Snapshot32::Create(process);
      return SystemCallStatus::kTerminated;
    }
  }

  if (FLAGS_snapshot_before_input_byte) {
    FLAGS_snapshot_before_input_byte -= 1;
  }
  GRANARY_STRACE( std::cerr << process->Id() << " receive length=" << length
                           << " fd=" << fd
                           << " buf=0x" << std::hex << abi.Arg<2,Addr32>()
                           << " rx_bytes=0x" << std::hex << abi.Arg<4,Addr32>()
                           << std::dec << " -> "; )

  if (DECREE_STDIN == fd || DECREE_STDOUT == fd || DECREE_STDERR == fd) {
    auto receive_fault = false;
    auto num_bytes = DoReceive(process, fd, buf, length, receive_fault);

    GRANARY_STRACE(std::cerr << "length=" << num_bytes; )

    // We faulted when receiving data, but we got some data, but didn't update
    // the count.
    if (receive_fault) {
      abi.SetReturn(DECREE_EFAULT);
      GRANARY_STRACE( std::cerr << " EFAULT (buf)"; )

    // Try to update the number of bytes read, regardless of if we detected
    // an EFAULT.
    } else if (rx_bytes && !process->TryWrite(rx_bytes, num_bytes)) {
      GRANARY_STRACE( std::cerr << "EFAULT (rx_bytes)"; )
      abi.SetReturn(DECREE_EFAULT);

    } else {
      abi.SetReturn(0);
    }

    GRANARY_STRACE( std::cerr << std::endl; )

    return SystemCallStatus::kComplete;

  // Invalid FD.
  // TODO(pag): It seems like `num_fds` and `num_fds+1` might actually
  //            alias STDIN/STDOUT in `cb-server`. Double check this.
  } else if (0 > fd || fd >= num_fds) {
    GRANARY_STRACE( std::cerr << "EBADF (fd)" << std::endl; )
    abi.SetReturn(DECREE_EBADF);
    return SystemCallStatus::kComplete;

  // Okay we can do this.
  } else {
    auto file = files[static_cast<unsigned>(fd)];
    auto status = ConvertStatus(
        abi, file->Read(process, rx_bytes, buf, length, fd));
    GRANARY_STRACE( std::cerr << std::endl; )
    return status;
  }
}

enum PageCheckType {
  kCheckPageReadable,
  kCheckPageWritable
};

// Checks whether pages are readable/writable.
//
// In the case of `Receive` on stdin: We can't just pass `buf` to a `read`
// system call and depend on its `EFAULT` behavior because we might be reading
// into an RWX page in the RX state (which should convert that page into the
// RW state and invalidate the page hash).
static bool CheckPages(Process32 *process, uint8_t *buf, size_t length,
                       PageCheckType check) {
  auto buf_addr = reinterpret_cast<uintptr_t>(buf);
  auto buf_limit = buf_addr + length;
  auto buf_base = buf_addr & kPageMask;
  if (!buf_base) return false;
  auto buf_str = reinterpret_cast<uint8_t *>(buf_base);
  for (; buf_base < buf_limit; ) {
    uint8_t byte;
    if (!process->TryRead(buf_str, byte)) return false;
    if (kCheckPageWritable == check && !process->TryWrite(buf_str, byte)) {
      return false;
    }
    buf_base += kPageSize;
    buf_str += kPageSize;
  }
  return true;
}

// Checks whether or not a `decree_timeval` struct is valid.
static bool CheckTimeout(Process32 *process, SystemCallABI abi,
                         const decree_timeval *timeout) {
  if (!timeout) return true;

  decree_timeval to = {0, 0};
  if (!process->TryRead(timeout, to)) {
    GRANARY_STRACE( std::cerr << "EFAULT (timeout)" << std::endl; )
    abi.SetReturn(DECREE_EFAULT);
    return false;
  }

  GRANARY_STRACE( std::cerr << "timeout=[s=" << to.tv_sec << " us="
                            << to.tv_usec << "] "; )

  if (0 > to.tv_sec || 0 > to.tv_usec) {
    GRANARY_STRACE( std::cerr << "EINVAL (timeout)" << std::endl; )
    abi.SetReturn(DECREE_EINVAL);
    return false;
  }
  return true;
}

// Updates a FD set. Returns `false` if an invalid descriptor is referenced.
static bool UpdateSet(const FileTable &files,
                      bool (File::*will_block)(const Process32 *) const,
                      const Process32 *proc, decree_fd_set *set, int nfds,
                      int32_t *count, bool is_read) {
  auto max_fds = sizeof(decree_fd_set) * 8;
  auto max_fd = static_cast<size_t>(nfds);
  for (auto fd = 0UL; fd < max_fds; ++fd) {
    if (DECREE_FD_ISSET(fd, set)) {
      if (fd >= max_fd) {
        return false;
      }
      if (3 <= fd && (files[fd]->*will_block)(proc)) {
        DECREE_FD_CLR(fd, set);

      } else {
        if (is_read && 2 > fd &&
            (gInputIndex >= gInput.size() && !gInput.empty())) {
          DECREE_FD_CLR(fd, set);

        } else {
          ++*count;
        }
      }
    }
  }
  return true;
}

// Print out an FD set.
static void PrintSet(const decree_fd_set *set, const char *name, int nfds) {
  GRANARY_STRACE( std::cerr << name << "=["; )
  auto max_fds = sizeof(decree_fd_set) * 8;
  auto max_fd = static_cast<size_t>(nfds);
  const char *sep = "";
  for (auto fd = 0UL; fd < max_fds; ++fd) {
    if (fd >= max_fd) break;
    if (DECREE_FD_ISSET(fd, set)) {
      GRANARY_STRACE( std::cerr << sep << fd; )
      sep = ",";
    }
  }
  GRANARY_STRACE( std::cerr << "]"; )
}

// Implements the `fdwait` DECREE system call.
static SystemCallStatus Fdwait(Process32 *process,
                               FileTable &files,
                               SystemCallABI abi) {
//  if (!FLAGS_output_snapshot_dir.empty() &&
//      !FLAGS_snapshot_before_input_byte) {
//    Snapshot32::Create(process);
//    return SystemCallStatus::kTerminated;
//  }

  auto nfds = abi.Arg<1>();
  auto readfds = abi.Arg<2,decree_fd_set *>();
  auto writefds = abi.Arg<3,decree_fd_set *>();
  auto timeout = abi.Arg<4,const decree_timeval *>();
  auto readyfds = abi.Arg<5,int32_t *>();

  GRANARY_STRACE( std::cerr << process->Id() << " fdwait nfds=" << nfds
                            << " readfds=0x" << std::hex << abi.Arg<2,Addr32>()
                            << " writefds=0x" << std::hex << abi.Arg<3,Addr32>()
                            << " timeout=0x" << std::hex << abi.Arg<4,Addr32>()
                            << " readyfds=0x" << std::hex << abi.Arg<5,Addr32>()
                            << std::dec << " -> "; )

  if (!CheckTimeout(process, abi, timeout)) {
    return SystemCallStatus::kComplete;
  }

  if (0 > nfds) {
    GRANARY_STRACE( std::cerr << "EINVAL (nfds)" << std::endl; )
    abi.SetReturn(DECREE_EINVAL);
    return SystemCallStatus::kComplete;
  }

  if (readfds && !CheckPages(process, abi.Arg<2,uint8_t *>(),
                             sizeof *readfds, kCheckPageWritable)) {
    GRANARY_STRACE( std::cerr << "EFAULT (readfds)" << std::endl; )
    abi.SetReturn(DECREE_EFAULT);
    return SystemCallStatus::kComplete;
  }

  if (writefds && !CheckPages(process, abi.Arg<3,uint8_t *>(),
                              sizeof *writefds, kCheckPageWritable)) {
    GRANARY_STRACE( std::cerr << "EFAULT (writefds)" << std::endl; )
    abi.SetReturn(DECREE_EFAULT);
    return SystemCallStatus::kComplete;
  }

  auto num_bits = 0;
  nfds = std::min(nfds, FLAGS_num_exe * 2 + 1);
  decree_fd_set read_fd_set;
  decree_fd_set write_fd_set;

  if (readfds) {
    memcpy(&read_fd_set, readfds, sizeof(decree_fd_set));
  }

  if (writefds) {
    memcpy(&write_fd_set, writefds, sizeof(decree_fd_set));
  }

  if (readfds &&
      !UpdateSet(files, &File::ReadWillBlock,
                 process, &read_fd_set, nfds, &num_bits, true)) {
    GRANARY_STRACE( std::cerr << "EBADF (readfds)" << std::endl; )
    abi.SetReturn(DECREE_EBADF);
    return SystemCallStatus::kComplete;
  }

  if (writefds &&
      !UpdateSet(files, &File::WriteWillBlock,
                 process, &write_fd_set, nfds, &num_bits, false)) {
    GRANARY_STRACE( std::cerr << "EBADF (writefds)" << std::endl; )
    abi.SetReturn(DECREE_EBADF);
    return SystemCallStatus::kComplete;
  }

  if (readyfds && !process->TryWrite(readyfds, num_bits)) {
    GRANARY_STRACE( std::cerr << "EFAULT (readyfds)" << std::endl; )
    abi.SetReturn(DECREE_EFAULT);
    return SystemCallStatus::kComplete;
  }

  if (!num_bits) {
    if (!timeout) {
      GRANARY_STRACE( std::cerr << "(IPR)" << std::endl; )
      return SystemCallStatus::kInProgress;
    }

    if (timeout && (timeout->tv_sec || timeout->tv_usec)) {
      if (-1 == process->schedule_delay) {
        if (!(process->schedule_delay = timeout->tv_sec)) {
          process->schedule_delay = 1;  // Because `timeout->tv_usec != 0`.
        }
        GRANARY_STRACE( std::cerr << "(IPR)" << std::endl; )
        return SystemCallStatus::kSleeping;
      } else if (process->schedule_delay--) {
        GRANARY_STRACE( std::cerr << "(IPR)" << std::endl; )
        return SystemCallStatus::kSleeping;
      } else {
        process->schedule_delay = -1;
      }
    }
  }

  GRANARY_STRACE( std::cerr << "nfds=" << num_bits;)

  if (readfds) {
    PrintSet(&read_fd_set, " readfds", nfds);
    memcpy(readfds, &read_fd_set, sizeof(decree_fd_set));
  }

  if (writefds) {
    PrintSet(&write_fd_set, " writefds", nfds);
    memcpy(writefds, &write_fd_set, sizeof(decree_fd_set));
  }

  GRANARY_STRACE( std::cerr << std::endl; )

  abi.SetReturn(0);
  return SystemCallStatus::kComplete;
}

enum : uint32_t {
  k1GiB = 1UL << 30
};

// Implements the `allocate` DECREE system call.
static SystemCallStatus Allocate(Process32 *process, SystemCallABI abi) {
  if (input::gRecord) {
    input::gRecord->AddSplit();
  }

  auto length = abi.Arg<1,uint32_t>();
  auto is_executable = 0 != abi.Arg<2>();
  auto addr = abi.Arg<3,Addr32 *>();

  GRANARY_STRACE( std::cerr << process->Id() << " allocate length=0x"
                            << std::hex << length << std::dec << " is_x="
                            << is_executable << " addr=0x" << std::hex
                            << abi.Arg<3,Addr32>() << std::dec
                            << " -> "; )

  if (!length) {
    GRANARY_STRACE( std::cerr << "EINVAL" << std::endl; )
    abi.SetReturn(DECREE_EINVAL);
    return SystemCallStatus::kComplete;
  }

  // Round up to nearest page size.
  auto num_bytes = (length + os::kPageSize - 1UL) & os::kPageMask;
  auto perms = is_executable ? PagePerms::kRWX : PagePerms::kRW;
  auto addr32 = process->Allocate(num_bytes, perms);

  abi.SetReturn(0);

  if (!addr32) {
    GRANARY_STRACE( std::cerr << "ENOMEM" << std::endl; )
    abi.SetReturn(DECREE_ENOMEM);
    return SystemCallStatus::kComplete;

  } else if (addr && !process->TryWrite(addr, addr32)) {
    GRANARY_STRACE( std::cerr << "EFAULT" << std::endl; )
    process->Deallocate(addr32, num_bytes);
    abi.SetReturn(DECREE_EFAULT);

  } else {
    GRANARY_STRACE( std::cerr << "0x" << std::hex << addr32
                              << std::dec << std::endl; )
  }

  return SystemCallStatus::kComplete;
}

// Implements the `allocate` DECREE system call.
static SystemCallStatus Deallocate(Process32 *process, SystemCallABI abi) {
  if (input::gRecord) {
    input::gRecord->AddSplit();
  }

  auto addr = abi.Arg<1,Addr32>();
  auto addr_uint = static_cast<uintptr_t>(addr);
  auto length = abi.Arg<2,uint32_t>();

  GRANARY_STRACE( std::cerr << process->Id() << " deallocate "
                            << "addr=0x" << std::hex << addr
                            << " length=0x" << std::hex << length
                            << std::dec << " -> "; )

  auto aligned_length = (length + os::kPageSize - 1UL) & os::kPageMask;

  if (!aligned_length) {
    abi.SetReturn(DECREE_EINVAL);
    GRANARY_STRACE( std::cerr << "EINVAL (length)" << std::endl; )

  } else if (addr_uint != (addr_uint & kPageMask)) {
    abi.SetReturn(DECREE_EINVAL);
    GRANARY_STRACE( std::cerr << "EINVAL (addr)" << std::endl; )

  } else if (addr_uint >= kTaskSize) {
    abi.SetReturn(DECREE_EINVAL);
    GRANARY_STRACE( std::cerr << "EINVAL (addr > task size)" << std::endl; )

  // Checks for possible overflow.
  } else if (length > (kTaskSize - addr_uint)) {
    abi.SetReturn(DECREE_EINVAL);
    GRANARY_STRACE( std::cerr << "EINVAL (addr > task size)" << std::endl; )

  } else if (!((addr_uint + length) <= kMagicPageBegin ||
                addr_uint >= kMagicPageEnd)) {
    abi.SetReturn(DECREE_EINVAL);
    GRANARY_STRACE( std::cerr << "EINVAL (magic)" << std::endl; )

  } else {
    process->Deallocate(addr, aligned_length);
    abi.SetReturn(0);
    GRANARY_STRACE( std::cerr << "0" << std::endl; )
  }

  return SystemCallStatus::kComplete;
}

// Implements the `random` system call.
static SystemCallStatus Random(Process32 *process, SystemCallABI abi) {
  if (input::gRecord) {
    input::gRecord->AddSplit();
  }
//  if (!FLAGS_output_snapshot_dir.empty() &&
//      !FLAGS_snapshot_before_input_byte) {
//    Snapshot32::Create(process);
//    return SystemCallStatus::kTerminated;
//  }

  auto buf = abi.Arg<1,uint8_t *>();
  auto count = abi.Arg<2,uint32_t>();
  auto rnd_bytes = abi.Arg<3,uint32_t *>();

  GRANARY_STRACE( std::cerr << process->Id() << " random"
                            << " buf=" << std::hex << abi.Arg<1,Addr32>()
                            << " count=" << std::dec << count
                            << " rnd_bytes=" << std::hex << abi.Arg<3,Addr32>()
                            << std::dec; )

  abi.SetReturn(0);

  if (count) {
    if (count > kSignedSizeMax) {
      GRANARY_STRACE( std::cerr << " -> EINVAL (count too big)" << std::endl; )
      abi.SetReturn(DECREE_EINVAL);
      return SystemCallStatus::kComplete;
    }

    if (!buf || !CheckPages(process, buf, count, kCheckPageWritable)) {
      GRANARY_STRACE( std::cerr << " -> EFAULT (buf)" << std::endl; )
      abi.SetReturn(DECREE_EFAULT);
      return SystemCallStatus::kComplete;
    }
  }

  if (count) {
    DoRandom(process, buf, count);
  }
  GRANARY_STRACE( std::cerr << " -> count=" << count << std::endl; )

  if (rnd_bytes && !process->TryWrite(rnd_bytes, count)) {
    GRANARY_STRACE( std::cerr << " EFAULT (rnd_bytes)" << std::endl; )
    abi.SetReturn(DECREE_EFAULT);
  }

  return SystemCallStatus::kComplete;
}

}  // namespace

// Handles system calls.
SystemCallStatus SystemCall(Process32 *process, FileTable &files) {
  SystemCallABI abi{process};
  switch (abi.Number()) {
    case SystemCallSelector::kInvalid: return BadSystemCall(abi);
    case SystemCallSelector::kTerminate: return Terminate(process, abi);
    case SystemCallSelector::kTransmit: return Transmit(process, files, abi);
    case SystemCallSelector::kReceive: return Receive(process, files, abi);
    case SystemCallSelector::kFdwait: return Fdwait(process, files, abi);
    case SystemCallSelector::kAllocate: return Allocate(process, abi);
    case SystemCallSelector::kDeallocate: return Deallocate(process, abi);
    case SystemCallSelector::kRandom: return Random(process, abi);
  }
}

}  // namespace os
}  // namespace granary
