#include "maps.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace dlinject {
namespace {

std::uintptr_t parse_hex(std::string_view value) {
  std::uintptr_t result{};
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result, 16);
  if (parsed.ec != std::errc{}) {
    throw std::runtime_error("invalid address in procfs");
  }

  return result;
}

} // namespace

std::vector<Map> read_maps(pid_t pid) {
  std::ifstream input("/proc/" + std::to_string(pid) + "/maps");
  if (!input) {
    throw std::runtime_error("cannot read target memory map");
  }

  std::vector<Map> maps;
  for (std::string line; std::getline(input, line);) {
    std::istringstream fields(line);
    std::string range;
    std::string perms;
    std::string offset;
    std::string device;
    std::string inode;
    if (!(fields >> range >> perms >> offset >> device >> inode)) {
      continue;
    }

    std::string path;
    std::getline(fields >> std::ws, path);
    const auto dash = range.find('-');
    if (dash == std::string::npos) {
      continue;
    }

    maps.push_back({parse_hex(std::string_view(range).substr(0, dash)),
                    parse_hex(std::string_view(range).substr(dash + 1)),
                    parse_hex(offset), std::move(perms), std::move(path)});
  }

  return maps;
}

std::vector<pid_t> read_threads(pid_t pid) {
  std::vector<pid_t> tids;
  const auto dir =
      std::filesystem::path("/proc") / std::to_string(pid) / "task";
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    const auto name = entry.path().filename().string();
    pid_t tid{};
    const auto parsed =
        std::from_chars(name.data(), name.data() + name.size(), tid);
    if (parsed.ec == std::errc{}) {
      tids.push_back(tid);
    }
  }
  std::ranges::sort(tids);

  return tids;
}

std::optional<Map> find_module(pid_t pid, const std::filesystem::path &module) {
  const auto wanted = module.filename();
  for (const auto &map : read_maps(pid)) {
    if (!map.path.empty() && map.path.filename() == wanted) {
      return map;
    }
  }

  return std::nullopt;
}

} // namespace dlinject
