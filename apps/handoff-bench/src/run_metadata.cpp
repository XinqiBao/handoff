#include "run_metadata.hpp"

#include "build_config.hpp"
#include "build_identity.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#ifndef HANDOFF_BUILD_MODE
#define HANDOFF_BUILD_MODE "unknown"
#endif

#ifndef HANDOFF_SOURCE_DIR
#define HANDOFF_SOURCE_DIR ""
#endif

extern char** environ;

namespace handoff::bench {
namespace {

std::optional<std::string> run_git(std::initializer_list<std::string_view> arguments) {
  std::vector<std::string> storage;
  storage.reserve(arguments.size() + 1);
  storage.emplace_back("git");
  for (const auto argument : arguments) {
    storage.emplace_back(argument);
  }

  std::vector<char*> argv;
  argv.reserve(storage.size() + 1);
  for (auto& argument : storage) {
    argv.push_back(argument.data());
  }
  argv.push_back(nullptr);

  std::array<int, 2> output_pipe{};
  if (pipe(output_pipe.data()) != 0) {
    return std::nullopt;
  }

  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    close(output_pipe[0]);
    close(output_pipe[1]);
    return std::nullopt;
  }

  const bool actions_ready =
      posix_spawn_file_actions_addclose(&actions, output_pipe[0]) == 0 &&
      posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO) == 0 &&
      posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) == 0 &&
      posix_spawn_file_actions_addclose(&actions, output_pipe[1]) == 0;
  if (!actions_ready) {
    posix_spawn_file_actions_destroy(&actions);
    close(output_pipe[0]);
    close(output_pipe[1]);
    return std::nullopt;
  }

  pid_t child = 0;
  const int spawn_error =
      posix_spawnp(&child, argv.front(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(output_pipe[1]);
  if (spawn_error != 0) {
    close(output_pipe[0]);
    return std::nullopt;
  }

  std::string output;
  std::array<char, 256> buffer{};
  while (true) {
    const auto bytes_read = read(output_pipe[0], buffer.data(), buffer.size());
    if (bytes_read > 0) {
      output.append(buffer.data(), static_cast<std::size_t>(bytes_read));
      continue;
    }
    if (bytes_read < 0 && errno == EINTR) {
      continue;
    }
    break;
  }
  close(output_pipe[0]);

  int status = 0;
  while (waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) {
      return std::nullopt;
    }
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    return std::nullopt;
  }

  while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
    output.pop_back();
  }
  return output;
}

} // namespace

RunMetadata collect_run_metadata() {
  RunMetadata metadata{.system = platform::current_system_info(),
                       .build_git_revision = HANDOFF_BUILD_GIT_REVISION,
                       .build_git_dirty = HANDOFF_BUILD_GIT_DIRTY,
                       .build_source_sha256 = HANDOFF_BUILD_SOURCE_SHA256,
                       .checkout_git_revision = std::nullopt,
                       .checkout_git_dirty = std::nullopt,
                       .build_mode = HANDOFF_BUILD_MODE,
                       .build_flags = HANDOFF_BUILD_FLAGS,
                       .waiting_behavior = "spin",
                       .control_waiting_behavior = "atomic-wait"};

  if (std::string_view(HANDOFF_SOURCE_DIR).empty() ||
      !std::filesystem::exists(std::filesystem::path(HANDOFF_SOURCE_DIR) / ".git")) {
    return metadata;
  }

  metadata.checkout_git_revision =
      run_git({"-C", HANDOFF_SOURCE_DIR, "rev-parse", "--verify", "HEAD"});
  const auto status =
      run_git({"-C", HANDOFF_SOURCE_DIR, "status", "--porcelain=v1", "--untracked-files=normal"});
  if (status) {
    metadata.checkout_git_dirty = !status->empty();
  }
  return metadata;
}

} // namespace handoff::bench
