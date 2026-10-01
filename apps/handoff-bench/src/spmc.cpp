#include "rate.hpp"
#include "workload_support.hpp"
#include "workloads.hpp"

#include "handoff/spmc/ordered_release_ring.hpp"
#include "handoff/spmc/serialized_consumer_ring.hpp"
#include "handoff/spmc/slot_completion_ring.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace handoff::bench {
namespace {

template <Implementation Route, std::size_t Bytes, std::size_t Capacity>
using Ring =
    std::conditional_t<Route == Implementation::spmc_serialized,
                       spmc::SerializedConsumerRing<Payload<Bytes>, Capacity>,
                       std::conditional_t<Route == Implementation::spmc_ordered,
                                          spmc::OrderedReleaseRing<Payload<Bytes>, Capacity>,
                                          spmc::SlotCompletionRing<Payload<Bytes>, Capacity>>>;

template <typename Queue, std::size_t Bytes> void publish_phase(Queue& ring, std::uint64_t count) {
  for (std::uint64_t index = 0; index < count; ++index) {
    auto token = [&]() -> typename Queue::ProducerClaim {
      for (;;) {
        auto attempt = ring.try_claim();
        if (attempt) {
          return std::move(*attempt);
        }
      }
    }();
    token.value() = make_payload<Bytes>(index);
    token.publish();
  }
}

template <typename Queue, std::size_t Bytes>
void consume_phase(Queue& ring, std::uint64_t target, std::uint64_t phase_base,
                   std::atomic<std::uint64_t>& released,
                   std::vector<std::atomic<std::uint8_t>>& seen, std::uint64_t& checksum,
                   std::uint64_t& count, std::atomic<bool>& valid) {
  while (released.load(std::memory_order_acquire) < target) {
    auto item = ring.try_acquire();
    if (!item) {
      continue;
    }
    auto token = std::move(item.value());
    const auto position = static_cast<std::uint64_t>(token.position());
    const auto relative = position - phase_base;
    std::uint64_t contribution = 0;
    if (position < phase_base || position >= target ||
        !observe_payload(token.value(), relative, contribution)) {
      valid.store(false, std::memory_order_relaxed);
    }
    if (position < seen.size()) {
      if (seen[static_cast<std::size_t>(position)].fetch_add(1, std::memory_order_relaxed) != 0) {
        valid.store(false, std::memory_order_relaxed);
      }
    }
    checksum ^= contribution;
    ++count;
    token.release();
    released.fetch_add(1, std::memory_order_acq_rel);
  }
}

template <std::size_t Bytes> std::uint64_t expected_phase_checksum(std::uint64_t count) {
  std::uint64_t checksum = 0;
  for (std::uint64_t position = 0; position < count; ++position) {
    const auto payload = make_payload<Bytes>(position);
    std::uint64_t contribution = 0;
    static_cast<void>(observe_payload(payload, position, contribution));
    checksum ^= contribution;
  }
  return checksum;
}

template <Implementation Route, std::size_t Bytes, std::size_t Capacity>
TrialResult run_trial(const Options& options, unsigned int trial,
                      PlacementResult& producer_placement,
                      std::array<PlacementResult, spmc_consumer_count>& consumer_placements) {
  Ring<Route, Bytes, Capacity> ring;
  TrialControl control;
  const auto total = options.warmup + options.iterations;
  std::vector<std::atomic<std::uint8_t>> seen(static_cast<std::size_t>(total));
  std::atomic<std::uint64_t> released{0};
  std::array<std::uint64_t, spmc_consumer_count> warmup_checksums{};
  std::array<std::uint64_t, spmc_consumer_count> checksums{};
  std::array<std::uint64_t, spmc_consumer_count> warmup_counts{};
  std::array<std::uint64_t, spmc_consumer_count> counts{};
  std::array<std::thread, spmc_consumer_count> consumers;
  Clock::time_point stop;

  std::thread producer([&] {
    producer_placement = apply_affinity(options.producer_cpu);
    signal_count(control.ready);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }
    publish_phase<Ring<Route, Bytes, Capacity>, Bytes>(ring, options.warmup);
    while (released.load(std::memory_order_acquire) != options.warmup ||
           ring.reusable_prefix() != options.warmup) {
    }
    signal_count(control.warmed);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }
    publish_phase<Ring<Route, Bytes, Capacity>, Bytes>(ring, options.iterations);
    while (released.load(std::memory_order_acquire) != total || ring.reusable_prefix() != total) {
    }
    stop = Clock::now();
    signal_done(control.done);
  });

  for (std::size_t index = 0; index < consumers.size(); ++index) {
    consumers[index] = std::thread([&, index] {
      const auto cpu = options.consumer_cpus
                           ? std::optional<unsigned int>{(*options.consumer_cpus)[index]}
                           : std::nullopt;
      consumer_placements[index] = apply_affinity(cpu);
      signal_count(control.ready);
      if (!wait_for_phase(control.begin_warmup, control.cancel)) {
        return;
      }
      consume_phase<Ring<Route, Bytes, Capacity>, Bytes>(ring, options.warmup, 0, released, seen,
                                                         warmup_checksums[index],
                                                         warmup_counts[index], control.valid);
      signal_count(control.warmed);
      if (!wait_for_phase(control.begin_timed, control.cancel)) {
        return;
      }
      consume_phase<Ring<Route, Bytes, Capacity>, Bytes>(ring, total, options.warmup, released,
                                                         seen, checksums[index], counts[index],
                                                         control.valid);
    });
  }

  wait_for_count(control.ready, static_cast<unsigned int>(spmc_consumer_count + 1));
  validate_affinity_or_cancel(
      control, producer, consumers, producer_placement, consumer_placements,
      std::array<std::string_view, spmc_consumer_count>{"consumer 0", "consumer 1"});
  release_phase(control.begin_warmup);
  wait_for_count(control.warmed, static_cast<unsigned int>(spmc_consumer_count + 1));
  const auto start = Clock::now();
  release_phase(control.begin_timed);
  wait_for_done(control.done);
  producer.join();
  for (auto& consumer : consumers) {
    consumer.join();
  }

  const auto warmup_checksum = warmup_checksums[0] ^ warmup_checksums[1];
  const auto checksum = checksums[0] ^ checksums[1];
  bool valid = control.valid.load(std::memory_order_relaxed) &&
               warmup_checksum == expected_phase_checksum<Bytes>(options.warmup) &&
               checksum == expected_phase_checksum<Bytes>(options.iterations) &&
               warmup_counts[0] + warmup_counts[1] == options.warmup &&
               counts[0] + counts[1] == options.iterations && ring.reusable_prefix() == total;
  for (const auto& entry : seen) {
    valid = valid && entry.load(std::memory_order_relaxed) == 1;
  }
  if (!valid) {
    throw std::runtime_error("SPMC complete-handoff validation failed");
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
  return {.trial = trial,
          .elapsed_ns = elapsed,
          .messages_per_second = rate_per_second(options.iterations, elapsed),
          .latency_ns = std::nullopt,
          .latency_p95_ns = std::nullopt,
          .latency_p99_ns = std::nullopt,
          .checksum = checksum,
          .consumer_counts = counts};
}

template <Implementation Route, std::size_t Bytes, std::size_t Capacity>
RunResults run_trials(const Options& options) {
  RunResults results;
  results.trials.reserve(options.trials);
  for (unsigned int trial = 1; trial <= options.trials; ++trial) {
    PlacementResult producer_placement;
    std::array<PlacementResult, spmc_consumer_count> consumer_placements;
    auto result =
        run_trial<Route, Bytes, Capacity>(options, trial, producer_placement, consumer_placements);
    if (trial == 1) {
      results.producer_placement = std::move(producer_placement);
      results.consumer_placements = std::move(consumer_placements);
    }
    results.trials.push_back(std::move(result));
  }
  return results;
}

template <Implementation Route, std::size_t Bytes>
RunResults dispatch_capacity(const Options& options) {
  switch (options.capacity_slots) {
  case 64:
    return run_trials<Route, Bytes, 64>(options);
  case 1'024:
    return run_trials<Route, Bytes, 1'024>(options);
  default:
    throw std::logic_error("validated SPMC capacity was not dispatched");
  }
}

template <Implementation Route> RunResults dispatch_payload(const Options& options) {
  switch (options.payload_bytes) {
  case 8:
    return dispatch_capacity<Route, 8>(options);
  case 64:
    return dispatch_capacity<Route, 64>(options);
  case 256:
    return dispatch_capacity<Route, 256>(options);
  default:
    throw std::logic_error("validated SPMC payload size was not dispatched");
  }
}
} // namespace

RunResults run_spmc_throughput(const Options& options) {
  switch (options.implementation) {
  case Implementation::spmc_serialized:
    return dispatch_payload<Implementation::spmc_serialized>(options);
  case Implementation::spmc_ordered:
    return dispatch_payload<Implementation::spmc_ordered>(options);
  case Implementation::spmc_slot:
    return dispatch_payload<Implementation::spmc_slot>(options);
  default:
    throw std::logic_error("unknown SPMC implementation");
  }
}

} // namespace handoff::bench
