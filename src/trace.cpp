#include "trace.hpp"

#include "maps.hpp"

#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#include <elf.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace dlinject {
namespace {

constexpr std::size_t page_size = 4096;
constexpr std::size_t path_offset = 128;
constexpr int load_flags = RTLD_NOW | RTLD_LOCAL;

class TraceError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void wait_stop(pid_t tid, int signal) {
  int status{};
  if (waitpid(tid, &status, __WALL) < 0) {
    throw TraceError("waitpid failed: " + std::string(std::strerror(errno)));
  }
  if (!WIFSTOPPED(status) || (signal != 0 && WSTOPSIG(status) != signal)) {
    throw TraceError("target stopped unexpectedly");
  }
}

void attach(pid_t tid) {
  if (ptrace(PTRACE_ATTACH, tid, nullptr, nullptr) < 0) {
    throw TraceError("ptrace attach failed for thread " + std::to_string(tid) +
                     ": " + std::strerror(errno));
  }
  wait_stop(tid, SIGSTOP);
}

void detach(pid_t tid) noexcept {
  ptrace(PTRACE_DETACH, tid, nullptr, nullptr);
}

class Group {
public:
  explicit Group(pid_t pid) : tids_(read_threads(pid)) {
    if (tids_.empty()) {
      throw TraceError("target has no threads");
    }

    try {
      for (const auto tid : tids_) {
        attach(tid);
        attached_++;
      }
    } catch (...) {
      release();
      throw;
    }
  }

  ~Group() { release(); }

  Group(const Group &) = delete;
  Group &operator=(const Group &) = delete;

  void release_others(pid_t selected) {
    for (std::size_t i = 0; i < attached_; ++i) {
      if (tids_[i] != selected) {
        detach(tids_[i]);
        tids_[i] = 0;
      }
    }
  }

  void reacquire_others(pid_t selected) {
    const auto current = read_threads(selected);
    for (const auto tid : current) {
      if (tid != selected) {
        attach(tid);
        extra_.push_back(tid);
      }
    }
  }

  void release() noexcept {
    for (std::size_t i = 0; i < attached_; ++i) {
      if (tids_[i] != 0) {
        detach(tids_[i]);
      }
    }
    for (const auto tid : extra_) {
      detach(tid);
    }
    attached_ = 0;
    extra_.clear();
  }

private:
  std::vector<pid_t> tids_;
  std::vector<pid_t> extra_;
  std::size_t attached_{};
};

std::vector<std::byte> read_mem(pid_t tid, std::uintptr_t address,
                                std::size_t size) {
  std::vector<std::byte> data(size);
  for (std::size_t offset = 0; offset < size; offset += sizeof(long)) {
    errno = 0;
    const long word = ptrace(PTRACE_PEEKDATA, tid, address + offset, nullptr);
    if (word == -1 && errno != 0) {
      throw TraceError("ptrace read failed: " +
                       std::string(std::strerror(errno)));
    }
    const auto count = std::min(sizeof(word), size - offset);
    std::memcpy(data.data() + offset, &word, count);
  }

  return data;
}

void write_mem(pid_t tid, std::uintptr_t address, const void *source,
               std::size_t size) {
  const auto *data = static_cast<const std::byte *>(source);
  for (std::size_t offset = 0; offset < size; offset += sizeof(long)) {
    const auto count = std::min(sizeof(long), size - offset);
    long word{};
    if (count != sizeof(long)) {
      errno = 0;
      word = ptrace(PTRACE_PEEKDATA, tid, address + offset, nullptr);
      if (word == -1 && errno != 0) {
        throw TraceError("ptrace read failed: " +
                         std::string(std::strerror(errno)));
      }
    }
    std::memcpy(&word, data + offset, count);
    if (ptrace(PTRACE_POKEDATA, tid, address + offset, word) < 0) {
      throw TraceError("ptrace write failed: " +
                       std::string(std::strerror(errno)));
    }
  }
}

#if defined(__x86_64__)
using Regs = user_regs_struct;

Regs get_regs(pid_t tid) {
  Regs regs{};
  if (ptrace(PTRACE_GETREGS, tid, nullptr, &regs) < 0) {
    throw TraceError("cannot read registers");
  }
  return regs;
}

void set_regs(pid_t tid, const Regs &regs) {
  if (ptrace(PTRACE_SETREGS, tid, nullptr, &regs) < 0) {
    throw TraceError("cannot write registers");
  }
}

std::uintptr_t pc(const Regs &regs) { return regs.rip; }

long remote_syscall(pid_t tid, const Regs &saved, long number,
                    std::array<unsigned long, 6> args) {
  constexpr std::array<std::uint8_t, 3> code{0x0f, 0x05, 0xcc};
  const auto backup = read_mem(tid, pc(saved), code.size());
  write_mem(tid, pc(saved), code.data(), code.size());

  auto regs = saved;
  regs.rax = number;
  regs.orig_rax = static_cast<unsigned long>(-1);
  regs.rdi = args[0];
  regs.rsi = args[1];
  regs.rdx = args[2];
  regs.r10 = args[3];
  regs.r8 = args[4];
  regs.r9 = args[5];
  set_regs(tid, regs);
  if (ptrace(PTRACE_CONT, tid, nullptr, nullptr) < 0) {
    throw TraceError("cannot continue target");
  }
  wait_stop(tid, SIGTRAP);
  const auto result = static_cast<long>(get_regs(tid).rax);
  write_mem(tid, pc(saved), backup.data(), backup.size());
  set_regs(tid, saved);
  return result;
}

std::uintptr_t run_dlopen(pid_t tid, const Regs &saved, std::uintptr_t page,
                          std::uintptr_t function, std::uintptr_t path) {
  constexpr std::array<std::uint8_t, 3> code{0xff, 0xd0, 0xcc};
  write_mem(tid, page, code.data(), code.size());
  auto regs = saved;
  regs.rip = page;
  regs.rax = function;
  regs.rdi = path;
  regs.rsi = load_flags;
  regs.rsp = (regs.rsp - 128U) & ~0xfUL;
  set_regs(tid, regs);
  if (ptrace(PTRACE_CONT, tid, nullptr, nullptr) < 0) {
    throw TraceError("cannot run loader");
  }
  wait_stop(tid, SIGTRAP);
  return get_regs(tid).rax;
}

#define DLINJECT_SUPPORTED_ARCH 1
#elif defined(__i386__)
using Regs = user_regs_struct;

Regs get_regs(pid_t tid) {
  Regs regs{};
  if (ptrace(PTRACE_GETREGS, tid, nullptr, &regs) < 0) {
    throw TraceError("cannot read registers");
  }
  return regs;
}
void set_regs(pid_t tid, const Regs &regs) {
  if (ptrace(PTRACE_SETREGS, tid, nullptr, &regs) < 0) {
    throw TraceError("cannot write registers");
  }
}
std::uintptr_t pc(const Regs &regs) { return regs.eip; }
long remote_syscall(pid_t tid, const Regs &saved, long number,
                    std::array<unsigned long, 6> args) {
  constexpr std::array<std::uint8_t, 3> code{0xcd, 0x80, 0xcc};
  const auto backup = read_mem(tid, pc(saved), code.size());
  write_mem(tid, pc(saved), code.data(), code.size());
  auto regs = saved;
  std::vector<std::byte> stack_backup;
  std::uintptr_t stack_address{};
  if (number == 192) {
    std::array<std::uint32_t, 6> mmap_args{};
    std::ranges::transform(args, mmap_args.begin(),
                           [](unsigned long value) { return value; });
    stack_address = (saved.esp - 128U) & ~3U;
    stack_backup = read_mem(tid, stack_address, sizeof(mmap_args));
    write_mem(tid, stack_address, mmap_args.data(), sizeof(mmap_args));
    regs.eax = 90;
    regs.ebx = stack_address;
  } else {
    regs.eax = number;
    regs.ebx = args[0];
    regs.ecx = args[1];
    regs.edx = args[2];
    regs.esi = args[3];
    regs.edi = args[4];
    regs.ebp = args[5];
  }
  regs.orig_eax = static_cast<unsigned long>(-1);
  set_regs(tid, regs);
  ptrace(PTRACE_CONT, tid, nullptr, nullptr);
  wait_stop(tid, SIGTRAP);
  const auto result = static_cast<long>(get_regs(tid).eax);
  if (!stack_backup.empty()) {
    write_mem(tid, stack_address, stack_backup.data(), stack_backup.size());
  }
  write_mem(tid, pc(saved), backup.data(), backup.size());
  set_regs(tid, saved);
  return result;
}
std::uintptr_t run_dlopen(pid_t tid, const Regs &saved, std::uintptr_t page,
                          std::uintptr_t function, std::uintptr_t path) {
  constexpr std::array<std::uint8_t, 9> code{0x6a, 0x02, 0x53, 0xff, 0xd0,
                                             0x83, 0xc4, 0x08, 0xcc};
  write_mem(tid, page, code.data(), code.size());
  auto regs = saved;
  regs.eip = page;
  regs.eax = function;
  regs.ebx = path;
  regs.esp -= 128U;
  set_regs(tid, regs);
  ptrace(PTRACE_CONT, tid, nullptr, nullptr);
  wait_stop(tid, SIGTRAP);
  return get_regs(tid).eax;
}
#define DLINJECT_SUPPORTED_ARCH 1
#elif defined(__aarch64__)
struct Regs {
  std::uint64_t x[31];
  std::uint64_t sp;
  std::uint64_t pc_value;
  std::uint64_t pstate;
};

Regs get_regs(pid_t tid) {
  Regs regs{};
  iovec io{&regs, sizeof(regs)};
  if (ptrace(PTRACE_GETREGSET, tid, NT_PRSTATUS, &io) < 0) {
    throw TraceError("cannot read registers");
  }
  return regs;
}
void set_regs(pid_t tid, const Regs &regs) {
  iovec io{const_cast<Regs *>(&regs), sizeof(regs)};
  if (ptrace(PTRACE_SETREGSET, tid, NT_PRSTATUS, &io) < 0) {
    throw TraceError("cannot write registers");
  }
}
std::uintptr_t pc(const Regs &regs) { return regs.pc_value; }
long remote_syscall(pid_t tid, const Regs &saved, long number,
                    std::array<unsigned long, 6> args) {
  constexpr std::array<std::uint32_t, 2> code{0xd4000001, 0xd4200000};
  const auto backup = read_mem(tid, pc(saved), sizeof(code));
  write_mem(tid, pc(saved), code.data(), sizeof(code));
  auto regs = saved;
  for (std::size_t i = 0; i < args.size(); ++i) {
    regs.x[i] = args[i];
  }
  regs.x[8] = number;
  set_regs(tid, regs);
  if (ptrace(PTRACE_CONT, tid, nullptr, nullptr) < 0) {
    throw TraceError("cannot continue target");
  }
  wait_stop(tid, SIGTRAP);
  const auto result = static_cast<long>(get_regs(tid).x[0]);
  write_mem(tid, pc(saved), backup.data(), backup.size());
  set_regs(tid, saved);
  return result;
}
std::uintptr_t run_dlopen(pid_t tid, const Regs &saved, std::uintptr_t page,
                          std::uintptr_t function, std::uintptr_t path) {
  constexpr std::array<std::uint32_t, 2> code{0xd63f0200, 0xd4200000};
  write_mem(tid, page, code.data(), sizeof(code));
  auto regs = saved;
  regs.pc_value = page;
  regs.x[0] = path;
  regs.x[1] = load_flags;
  regs.x[16] = function;
  regs.sp = (regs.sp - 128U) & ~0xfULL;
  set_regs(tid, regs);
  ptrace(PTRACE_CONT, tid, nullptr, nullptr);
  wait_stop(tid, SIGTRAP);
  return get_regs(tid).x[0];
}
#define DLINJECT_SUPPORTED_ARCH 1
#elif defined(__arm__)
struct Regs {
  std::uint32_t r[18];
};

Regs get_regs(pid_t tid) {
  Regs regs{};
  if (ptrace(PTRACE_GETREGS, tid, nullptr, &regs) < 0) {
    throw TraceError("cannot read registers");
  }
  return regs;
}
void set_regs(pid_t tid, const Regs &regs) {
  if (ptrace(PTRACE_SETREGS, tid, nullptr, &regs) < 0) {
    throw TraceError("cannot write registers");
  }
}
std::uintptr_t pc(const Regs &regs) { return regs.r[15]; }
long remote_syscall(pid_t tid, const Regs &saved, long number,
                    std::array<unsigned long, 6> args) {
  constexpr std::array<std::uint32_t, 2> code{0xef000000, 0xe1200070};
  const auto address = pc(saved) & ~std::uintptr_t{3};
  const auto backup = read_mem(tid, address, sizeof(code));
  write_mem(tid, address, code.data(), sizeof(code));
  auto regs = saved;
  for (std::size_t i = 0; i < args.size(); ++i) {
    regs.r[i] = args[i];
  }
  regs.r[7] = number;
  regs.r[15] = address;
  regs.r[16] &= ~(1U << 5U);
  regs.r[17] = static_cast<std::uint32_t>(-1);
  set_regs(tid, regs);
  ptrace(PTRACE_CONT, tid, nullptr, nullptr);
  wait_stop(tid, SIGTRAP);
  const auto result = static_cast<std::int32_t>(get_regs(tid).r[0]);
  write_mem(tid, address, backup.data(), backup.size());
  set_regs(tid, saved);
  return result;
}
std::uintptr_t run_dlopen(pid_t tid, const Regs &saved, std::uintptr_t page,
                          std::uintptr_t function, std::uintptr_t path) {
  constexpr std::array<std::uint32_t, 2> code{0xe12fff3c, 0xe1200070};
  write_mem(tid, page, code.data(), sizeof(code));
  auto regs = saved;
  regs.r[15] = page;
  regs.r[0] = path;
  regs.r[1] = load_flags;
  regs.r[12] = function;
  regs.r[13] = (regs.r[13] - 128U) & ~7U;
  regs.r[16] &= ~(1U << 5U);
  set_regs(tid, regs);
  ptrace(PTRACE_CONT, tid, nullptr, nullptr);
  wait_stop(tid, SIGTRAP);
  return get_regs(tid).r[0];
}
#define DLINJECT_SUPPORTED_ARCH 1
#endif

} // namespace

TraceResult remote_load(pid_t pid, std::uintptr_t dlopen_addr,
                        const std::filesystem::path &library) {
#ifndef DLINJECT_SUPPORTED_ARCH
  (void)pid;
  (void)dlopen_addr;
  (void)library;
  return {false, "this build architecture is not supported"};
#else
  try {
    const auto path = std::filesystem::canonical(library).string();
    if (path.size() + 1 > page_size - path_offset) {
      return {false, "library path is too long"};
    }

    Group group(pid);
    const auto saved = get_regs(pid);
    const std::array<unsigned long, 6> mmap_args{0,
                                                 page_size,
                                                 PROT_READ | PROT_WRITE |
                                                     PROT_EXEC,
                                                 MAP_PRIVATE | MAP_ANONYMOUS,
                                                 static_cast<unsigned long>(-1),
                                                 0};
#if defined(__x86_64__)
    constexpr long mmap_number = 9;
    constexpr long munmap_number = 11;
#elif defined(__i386__) || defined(__arm__)
    constexpr long mmap_number = 192;
    constexpr long munmap_number = 91;
#else
    constexpr long mmap_number = 222;
    constexpr long munmap_number = 215;
#endif
    const long mapped = remote_syscall(pid, saved, mmap_number, mmap_args);
    if (mapped < 0 && mapped >= -4095) {
      return {false, "remote mmap failed: errno " + std::to_string(-mapped)};
    }

    const auto page = static_cast<std::uintptr_t>(mapped);
    write_mem(pid, page + path_offset, path.c_str(), path.size() + 1);

    group.release_others(pid);
    std::uintptr_t handle{};
    try {
      handle = run_dlopen(pid, saved, page, dlopen_addr, page + path_offset);
      set_regs(pid, saved);
    } catch (...) {
      try {
        set_regs(pid, saved);
      } catch (...) {
      }
      throw;
    }
    group.reacquire_others(pid);
    const std::array<unsigned long, 6> munmap_args{page, page_size, 0, 0, 0, 0};
    const auto unmapped =
        remote_syscall(pid, saved, munmap_number, munmap_args);
    group.release();
    if (unmapped != 0) {
      return {false, "library loaded but remote memory cleanup failed"};
    }
    if (handle == 0) {
      return {false, "target dlopen returned null"};
    }

    return {true, "library loaded"};
  } catch (const std::exception &error) {
    return {false, error.what()};
  }
#endif
}

} // namespace dlinject
