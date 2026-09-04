#pragma once

#include <sys/types.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dlinject {

struct Map {
  std::uintptr_t start;
  std::uintptr_t end;
  std::uint64_t offset;
  std::string perms;
  std::filesystem::path path;
};

std::vector<Map> read_maps(pid_t pid);
std::vector<pid_t> read_threads(pid_t pid);
std::optional<Map> find_module(pid_t pid, const std::filesystem::path &module);

} // namespace dlinject
