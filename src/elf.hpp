#pragma once

#include <sys/types.h>

#include <filesystem>
#include <string>

namespace dlinject {

std::string check_abi(pid_t pid);
std::filesystem::path proc_exe(pid_t pid);

} // namespace dlinject
