#define _GNU_SOURCE 1

#include "Subprocess.h"
#include <string.h>
#include <sys/wait.h>
#include <vector>

using namespace bracelet;

Result<int> subprocess::call(std::string_view cmd_arg,
                             boost::span<std::string_view> args_arg,
                             const std::filesystem::path *cwd_arg) {
  const char *cwd = cwd_arg ? cwd_arg->c_str() : nullptr;
  std::vector<std::string> argv;
  argv.emplace_back(cmd_arg);
  for (auto &arg : args_arg) {
    argv.emplace_back(arg);
  }
  std::vector<char *> argv_cstr;
  argv_cstr.reserve(argv.size() + 1);
  for (auto &arg : argv) {
    argv_cstr.push_back(arg.data());
  }
  argv_cstr.push_back(NULL);
  std::string error_buffer(4096, '\0');
  auto pid = fork();
  BRACELET_ENSURE(pid >= 0, "Failed to fork: %s", strerror(errno));
  if (pid == 0) {
    // We're in the child.
    if (cwd != nullptr) {
      if (chdir(cwd) < 0) {
        snprintf(error_buffer.data(), error_buffer.size(), "chdir failed: %s\n",
                 strerrordesc_np(errno));
        write(2, error_buffer.data(), strlen(error_buffer.data()));
        _exit(1);
      }
    }
    execvp(argv_cstr[0], argv_cstr.data());
    snprintf(error_buffer.data(), error_buffer.size(),
             "execvp(\"%s\") failed: %s\n", argv_cstr[0],
             strerrordesc_np(errno));
    write(2, error_buffer.data(), strlen(error_buffer.data()));
    _exit(1);
  }
  // We're in the parent process.
  int status;
  while (true) {
    auto rc = waitpid(pid, &status, 0);
    if (rc >= 0)
      break;
    BRACELET_ENSURE(errno == EINTR, "waitpid() failed: %s", strerror(errno));
  }
  return status;
}
