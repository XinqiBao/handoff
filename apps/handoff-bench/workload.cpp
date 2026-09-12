#include "workload.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace handoff::bench {

LatencySummary summarize_latency(std::vector<std::int64_t> samples) {
  std::ranges::sort(samples);
  const auto middle = samples.size() / 2;
  const double median_ns =
      samples.size() % 2 == 1
          ? static_cast<double>(samples[middle])
          : (static_cast<double>(samples[middle - 1]) + static_cast<double>(samples[middle])) / 2.0;
  const auto p95_index = (samples.size() - 1) * 95 / 100;
  const auto p99_index = (samples.size() - 1) * 99 / 100;
  return {.median_ns = median_ns,
          .p95_ns = static_cast<double>(samples[p95_index]),
          .p99_ns = static_cast<double>(samples[p99_index])};
}

PlacementResult apply_affinity(std::optional<unsigned int> cpu) {
  if (!cpu) {
    return {
        .requested = std::nullopt,
        .outcome = {.status = platform::AffinityStatus::unsupported, .message = "not requested"}};
  }
  return {.requested = cpu, .outcome = platform::pin_current_thread(*cpu)};
}

namespace {

bool affinity_failed(const PlacementResult& placement) {
  if (!placement.requested) {
    return false;
  }
  return placement.outcome.status == platform::AffinityStatus::invalid_cpu ||
         placement.outcome.status == platform::AffinityStatus::system_error;
}

} // namespace

void wait_for_count(const std::atomic<unsigned int>& count, unsigned int expected) {
  while (count.load(std::memory_order_acquire) != expected) {
    std::this_thread::yield();
  }
}

bool wait_for_phase(const std::atomic<bool>& phase, const std::atomic<bool>& cancel) {
  while (!phase.load(std::memory_order_acquire)) {
    if (cancel.load(std::memory_order_acquire)) {
      return false;
    }
    std::this_thread::yield();
  }
  return true;
}

void validate_affinity_or_cancel(TrialControl& control, std::thread& producer,
                                 std::thread& consumer, const PlacementResult& producer_placement,
                                 const PlacementResult& consumer_placement) {
  const bool producer_failed = affinity_failed(producer_placement);
  const bool consumer_failed = affinity_failed(consumer_placement);
  if (!producer_failed && !consumer_failed) {
    return;
  }

  control.cancel.store(true, std::memory_order_release);
  producer.join();
  consumer.join();
  const auto& failed = producer_failed ? producer_placement : consumer_placement;
  const std::string role = producer_failed ? "producer" : "consumer";
  throw std::runtime_error(role + " affinity failed: " + failed.outcome.message);
}

} // namespace handoff::bench
