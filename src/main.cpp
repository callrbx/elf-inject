#include "dlinject/dlinject.hpp"

#include <charconv>
#include <iostream>
#include <string_view>

namespace {

void usage(std::string_view program) {
  std::cerr << "Usage: " << program << " <pid> <library.so>\n"
            << "       " << program << " --version\n";
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::cout << "dlinject " << dlinject::version() << '\n';
    return 0;
  }
  if (argc != 3) {
    usage(argv[0]);
    return 2;
  }

  pid_t pid{};
  const std::string_view value(argv[1]);
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), pid);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
      pid <= 1) {
    std::cerr << "dlinject: invalid pid\n";
    return 2;
  }

  const auto result = dlinject::inject(pid, argv[2]);
  if (!result.ok) {
    std::cerr << "dlinject: " << result.message << '\n';
    return 1;
  }

  std::cout << "dlinject: loaded " << argv[2] << " into " << pid << '\n';
  return 0;
}
