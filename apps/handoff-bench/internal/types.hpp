#pragma once

#include "handoff/platform/thread_affinity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace handoff::bench {

enum class Benchmark { smoke, throughput, ping_pong, offered_load, publication_hole };
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
  mpsc_count,
  mpsc_ordered,
  mpsc_serialized,
  mpsc_slot,
  pipeline,
  sequence,
  sequence_payload,
  staged
};

inline constexpr std::size_t fan_out_consumer_count = 2;
inline constexpr std::size_t mpsc_producer_count = 2;
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
  std::optional<std::array<unsigned int, 2>> producer_cpus;
  std::optional<unsigned int> consumer_cpu;
  std::optional<std::array<unsigned int, 2>> consumer_cpus;
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
  std::optional<std::uint64_t> offered_messages{};
  std::optional<std::uint64_t> observed_messages{};
  std::optional<std::uint64_t> overwritten_messages{};
  std::optional<std::uint64_t> retry_attempts{};
  std::optional<std::uint64_t> observed_payload_bytes{};
  std::optional<double> offered_messages_per_second{};
  std::optional<double> observed_messages_per_second{};
};

struct PlacementResult {
  std::optional<unsigned int> requested;
  std::optional<unsigned int> effective;
  platform::AffinityResult outcome;
};

struct ProgressResult {
  std::size_t claimed_before_release{};
  std::size_t payloads_completed_before_release{};
  std::size_t publication_attempts_rejected_before_release{};
  std::size_t publication_returns_before_release{};
  std::size_t visible_before_release{};
  std::size_t consumer_completions_before_release{};
  bool further_claim_rejected{};
  std::size_t final_consumer_completions{};
  std::uint64_t checksum{};
};

struct RunResults {
  std::vector<TrialResult> trials;
  std::optional<ProgressResult> progress;
  PlacementResult producer_placement;
  std::array<PlacementResult, 2> producer_placements;
  PlacementResult consumer_placement;
  std::array<PlacementResult, 2> consumer_placements;
};

} // namespace handoff::bench
