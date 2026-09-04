#include "elf.hpp"

#include <elf.h>
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace dlinject {

std::filesystem::path proc_exe(pid_t pid) {
  std::array<char, 4096> path{};
  const auto link = "/proc/" + std::to_string(pid) + "/exe";
  const auto size = readlink(link.c_str(), path.data(), path.size() - 1);
  if (size < 0) {
    throw std::runtime_error("cannot resolve target executable");
  }

  return std::string(path.data(), static_cast<std::size_t>(size));
}

std::string check_abi(pid_t pid) {
  const auto path = "/proc/" + std::to_string(pid) + "/exe";
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return "cannot open target executable: " +
           std::string(std::strerror(errno));
  }

  std::array<unsigned char, 20> ident{};
  const auto size = read(fd, ident.data(), ident.size());
  close(fd);
  if (size != static_cast<ssize_t>(ident.size()) || ident[EI_MAG0] != ELFMAG0 ||
      ident[EI_MAG1] != ELFMAG1 || ident[EI_MAG2] != ELFMAG2 ||
      ident[EI_MAG3] != ELFMAG3) {
    return "target is not an ELF executable";
  }

#if INTPTR_MAX == INT64_MAX
  constexpr unsigned char elf_class = ELFCLASS64;
#else
  constexpr unsigned char elf_class = ELFCLASS32;
#endif
  if (ident[EI_CLASS] != elf_class) {
    return "target ABI differs from injector; use a matching build";
  }
  if (ident[EI_DATA] != ELFDATA2LSB) {
    return "target byte order is not supported";
  }

  const auto machine = static_cast<unsigned>(ident[18]) |
                       (static_cast<unsigned>(ident[19]) << 8U);
#if defined(__x86_64__)
  constexpr unsigned expected_machine = EM_X86_64;
#elif defined(__i386__)
  constexpr unsigned expected_machine = EM_386;
#elif defined(__aarch64__)
  constexpr unsigned expected_machine = EM_AARCH64;
#elif defined(__arm__)
  constexpr unsigned expected_machine = EM_ARM;
#else
  constexpr unsigned expected_machine = 0;
#endif
  if (machine != expected_machine) {
    return "target architecture differs from injector; use a matching build";
  }

  return {};
}

} // namespace dlinject
