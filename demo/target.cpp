#include <sys/prctl.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include <vector>

namespace {

constexpr int worker_count = 4;
std::atomic<bool> running{true};

void stop(int) { running = false; }

} // namespace

int main() {
  std::signal(SIGTERM, stop);
  std::signal(SIGINT, stop);
  if (prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY) != 0) {
    std::cerr << "warning: could not relax ptrace policy for demo\n";
  }

  std::vector<std::thread> workers;
  for (int i = 0; i < worker_count; ++i) {
    workers.emplace_back([] {
      while (running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    });
  }

  std::cout << "threaded target pid=" << getpid() << " workers=" << worker_count
            << std::endl;
  while (running.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  for (auto &worker : workers) {
    worker.join();
  }
}
