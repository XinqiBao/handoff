#pragma once

#include <cstdint>
#include <optional>

namespace handoff::bench {

inline std::optional<double> rate_per_second(std::uint64_t count, std::int64_t elapsed_ns) {
  if (elapsed_ns <= 0) {
    return std::nullopt;
  }
  return static_cast<double>(count) * 1'000'000'000.0 / static_cast<double>(elapsed_ns);
}

} // namespace handoff::bench
