#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

namespace {

constexpr char fallback_path[] = "/tmp/dlinject-demo.loaded";
constexpr char message[] = "dlinject example library loaded\n";

__attribute__((constructor)) void loaded() {
  const char *path = std::getenv("DLINJECT_DEMO_FILE");
  if (path == nullptr || path[0] == '\0') {
    path = fallback_path;
  }

  const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) {
    return;
  }
  const auto unused = write(fd, message, std::strlen(message));
  (void)unused;
  close(fd);
}

} // namespace
