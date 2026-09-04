#include "dlinject/dlinject.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

int failures{};

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

std::size_t thread_count(pid_t pid) {
  std::size_t count{};
  for ([[maybe_unused]] const auto &entry : std::filesystem::directory_iterator(
           "/proc/" + std::to_string(pid) + "/task")) {
    count++;
  }
  return count;
}

int run_injector(pid_t target, const std::filesystem::path &library) {
  const pid_t injector = fork();
  if (injector == 0) {
    const auto pid = std::to_string(target);
    execl(DLINJECT_BIN, DLINJECT_BIN, pid.c_str(), library.c_str(), nullptr);
    _exit(127);
  }

  int status{};
  waitpid(injector, &status, 0);
  if (!WIFEXITED(status)) {
    return -1;
  }

  return WEXITSTATUS(status);
}

} // namespace

int main(int argc, char **argv) {
  check(dlinject::version() == "0.2.0", "version");
  check(!dlinject::inject(-1, EXAMPLE_LIB).ok, "reject invalid pid");
  check(!dlinject::inject(getpid(), EXAMPLE_LIB).ok, "reject self injection");
  if (argc == 2 && std::string_view(argv[1]) == "--unit") {
    return failures == 0 ? 0 : 1;
  }

  const auto marker = std::filesystem::temp_directory_path() /
                      ("dlinject-test-" + std::to_string(getpid()));
  std::filesystem::remove(marker);
  setenv("DLINJECT_DEMO_FILE", marker.c_str(), 1);

  const pid_t target = fork();
  if (target == 0) {
    execl(TARGET_BIN, TARGET_BIN, nullptr);
    _exit(127);
  }
  check(target > 0, "start target");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const auto before = thread_count(target);
  check(before >= 5, "target is multithreaded");

  const auto invalid_library = marker.string() + ".so";
  {
    std::ofstream output(invalid_library);
    output << "not an ELF shared library\n";
  }
  check(run_injector(target, invalid_library) == 1,
        "invalid library is rejected");
  check(kill(target, 0) == 0, "target survives failed dlopen");
  check(thread_count(target) == before, "threads survive failed dlopen");
  std::filesystem::remove(invalid_library);

  check(run_injector(target, EXAMPLE_LIB) == 0, "injector exits successfully");
  int status{};
  check(std::filesystem::exists(marker), "library constructor ran");
  check(kill(target, 0) == 0, "target remains alive");
  check(thread_count(target) == before, "all target threads remain alive");

  kill(target, SIGTERM);
  waitpid(target, &status, 0);
  std::filesystem::remove(marker);

  return failures == 0 ? 0 : 1;
}
