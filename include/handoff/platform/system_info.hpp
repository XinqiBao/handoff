#pragma once

#include <string>

namespace handoff::platform {

struct SystemInfo {
  std::string operating_system;
  std::string architecture;
  std::string compiler;
};

[[nodiscard]] SystemInfo current_system_info();

} // namespace handoff::platform
