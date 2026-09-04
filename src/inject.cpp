#include "dlinject/dlinject.hpp"

#include "elf.hpp"
#include "maps.hpp"
#include "trace.hpp"

#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <stdexcept>

namespace dlinject {
namespace {

bool same_file(const std::filesystem::path &left,
               const std::filesystem::path &right) {
  struct stat left_stat{};
  struct stat right_stat{};
  return stat(left.c_str(), &left_stat) == 0 &&
         stat(right.c_str(), &right_stat) == 0 &&
         left_stat.st_dev == right_stat.st_dev &&
         left_stat.st_ino == right_stat.st_ino;
}

std::filesystem::path target_path(pid_t pid,
                                  const std::filesystem::path &path) {
  return "/proc/" + std::to_string(pid) + "/root" + path.string();
}

} // namespace

Result inject(pid_t pid, const std::filesystem::path &library) {
  if (pid <= 1 || pid == getpid()) {
    return {false, "invalid target pid"};
  }
  if (!std::filesystem::is_regular_file(library)) {
    return {false, "library is not a regular file"};
  }
  if (const auto error = check_abi(pid); !error.empty()) {
    return {false, error};
  }

  const auto canonical_library = std::filesystem::canonical(library);
  if (!same_file(canonical_library, target_path(pid, canonical_library))) {
    return {false,
            "library is not the same file in the target mount namespace"};
  }

  Dl_info info{};
  auto *symbol = dlsym(RTLD_DEFAULT, "dlopen");
  if (symbol == nullptr || dladdr(symbol, &info) == 0 ||
      info.dli_fbase == nullptr || info.dli_fname == nullptr) {
    return {false, "cannot resolve local dlopen"};
  }

  const auto local_base = reinterpret_cast<std::uintptr_t>(info.dli_fbase);
  const auto offset = reinterpret_cast<std::uintptr_t>(symbol) - local_base;
  const auto target_module = find_module(pid, info.dli_fname);
  if (!target_module) {
    return {false, "target does not map the local dynamic-loader module"};
  }
  if (!same_file(info.dli_fname, target_path(pid, target_module->path))) {
    return {false, "target uses a different dynamic-loader module"};
  }

  const auto target_base = target_module->start - target_module->offset;
  const auto result = remote_load(pid, target_base + offset, canonical_library);
  return {result.ok, result.message};
}

} // namespace dlinject
