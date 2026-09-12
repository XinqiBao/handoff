#pragma once

#include <string>

namespace handoff::platform {

enum class AffinityStatus { applied, unsupported, invalid_cpu, system_error };

struct AffinityResult {
  AffinityStatus status;
  std::string message;
};

[[nodiscard]] AffinityResult pin_current_thread(unsigned int cpu);

} // namespace handoff::platform
