#pragma once

#include <sys/types.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace dlinject {

struct Result {
  bool ok;
  std::string message;
};

Result inject(pid_t pid, const std::filesystem::path &library);
std::string_view version() noexcept;

} // namespace dlinject
