#pragma once

#include "handoff/platform/thread_affinity.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace handoff::bench {

enum class Benchmark { smoke, throughput, ping_pong };
enum class Implementation { basic, cache_line };

struct Options {
  std::uint64_t iterations{1'000'000};
  std::uint64_t warmup{10'000};
  unsigned int trials{3};
  Implementation implementation{Implementation::basic};
  std::size_t payload_bytes{64};
  std::size_t capacity_slots{1'024};
  std::optional<unsigned int> producer_cpu;
  std::optional<unsigned int> consumer_cpu;
  std::optional<std::filesystem::path> output;
};

struct TrialResult {
  unsigned int trial;
  std::int64_t elapsed_ns;
  double messages_per_second;
  double latency_ns;
  double latency_p95_ns;
  double latency_p99_ns;
  std::uint64_t checksum;
};

struct PlacementResult {
  std::optional<unsigned int> requested;
  platform::AffinityResult outcome;
};

struct RunResults {
  std::vector<TrialResult> trials;
  PlacementResult producer_placement;
  PlacementResult consumer_placement;
};

RunResults run_throughput(const Options& options);
RunResults run_ping_pong(const Options& options);
bool write_csv(const std::filesystem::path& path, Benchmark benchmark, const Options& options,
               const RunResults& results);
void print_results(Benchmark benchmark, const Options& options, const RunResults& results);
int run(int argc, char* argv[]);

} // namespace handoff::bench
