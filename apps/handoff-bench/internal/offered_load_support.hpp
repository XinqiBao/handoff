#pragma once

#include <algorithm>
#include <cstdint>

namespace handoff::bench {

struct OverwriteResynchronization {
  std::uint64_t next_sequence;
  std::uint64_t skipped_sequences;
};

constexpr OverwriteResynchronization resynchronize_after_overwrite(std::uint64_t requested,
                                                                   std::uint64_t oldest_candidate,
                                                                   std::uint64_t last_sequence) {
  const auto next_candidate = std::max(requested + 1, oldest_candidate);
  const auto next = std::min(next_candidate, last_sequence + 1);
  return {.next_sequence = next, .skipped_sequences = next - requested};
}

} // namespace handoff::bench
