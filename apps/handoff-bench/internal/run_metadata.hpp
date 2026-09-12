#pragma once

#include "handoff/platform/system_info.hpp"

#include <optional>
#include <string>

namespace handoff::bench {

struct RunMetadata {
  platform::SystemInfo system;
  std::optional<std::string> git_revision;
  std::optional<bool> git_dirty;
  std::string build_mode;
  std::string waiting_behavior;
};

RunMetadata collect_run_metadata();

} // namespace handoff::bench
