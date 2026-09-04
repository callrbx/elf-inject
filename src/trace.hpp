#pragma once

#include <sys/types.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace dlinject {

struct TraceResult {
  bool ok;
  std::string message;
};

TraceResult remote_load(pid_t pid, std::uintptr_t dlopen_addr,
                        const std::filesystem::path &library);

} // namespace dlinject
