#pragma once

#include "handoff/platform/thread_affinity.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace handoff::bench {

enum class Benchmark { smoke, throughput, ping_pong, offered_load };
enum class Implementation {
  basic,
  batch,
  bulk,
  burst,
  byte_record,
  cache_line,
  cached_index,
  descriptor_record,
  fan_out,
  fixed_record,
  pipeline,
  sequence,
  sequence_payload,
  staged
};

inline constexpr std::size_t fan_out_consumer_count = 2;
inline constexpr std::size_t pipeline_consumer_count = 2;

struct Options {
  std::uint64_t iterations{1'000'000};
  std::uint64_t warmup{10'000};
  unsigned int trials{3};
  Implementation implementation{Implementation::basic};
  std::size_t payload_bytes{64};
  std::size_t capacity_slots{1'024};
  std::optional<std::size_t> capacity_bytes;
  std::size_t batch_size{1};
  std::uint64_t producer_interval_ns{0};
  std::uint64_t consumer_stall_every{0};
  std::uint64_t consumer_stall_ns{0};
  std::optional<unsigned int> producer_cpu;
  std::optional<unsigned int> consumer_cpu;
  std::optional<std::filesystem::path> output;
};

struct TrialResult {
  unsigned int trial;
  std::int64_t elapsed_ns;
  std::optional<double> messages_per_second;
  std::optional<double> latency_ns;
  std::optional<double> latency_p95_ns;
  std::optional<double> latency_p99_ns;
  std::uint64_t checksum;
  std::optional<std::uint64_t> offered_messages;
  std::optional<std::uint64_t> observed_messages;
  std::optional<std::uint64_t> overwritten_messages;
  std::optional<std::uint64_t> retry_attempts;
  std::optional<std::uint64_t> observed_payload_bytes;
  std::optional<double> offered_messages_per_second;
  std::optional<double> observed_messages_per_second;
};

struct PlacementResult {
  std::optional<unsigned int> requested;
  std::optional<unsigned int> effective;
  platform::AffinityResult outcome;
};

struct RunResults {
  std::vector<TrialResult> trials;
  PlacementResult producer_placement;
  PlacementResult consumer_placement;
};

} // namespace handoff::bench
