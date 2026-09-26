#include "rate.hpp"
#include "workload_support.hpp"
#include "workloads.hpp"

#include "handoff/mpsc/ordered_publication_ring.hpp"
#include "handoff/spsc/basic_bounded_ring.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace handoff::bench {
namespace {

template <bool Ordered, std::size_t Bytes, std::size_t Capacity>
using Ring = std::conditional_t<Ordered, mpsc::OrderedPublicationRing<Payload<Bytes>, Capacity>,
                                spsc::BasicBoundedRing<Payload<Bytes>, Capacity>>;

template <bool Ordered, typename Queue, std::size_t Bytes>
void publish_messages(Queue& ring, std::mutex& producer_mutex, std::uint64_t& next_serial_position,
                      std::uint64_t count, std::uint64_t phase_base) {
  for (std::uint64_t message = 0; message < count; ++message) {
    if constexpr (Ordered) {
      auto claim = ring.try_claim();
      while (!claim) {
        std::this_thread::yield();
        claim = ring.try_claim();
      }
      auto token = std::move(claim).value();
      token.value() = make_payload<Bytes>(token.position() - phase_base);
      token.publish();
    } else {
      std::lock_guard lock(producer_mutex);
      const auto payload = make_payload<Bytes>(next_serial_position - phase_base);
      while (!ring.try_push(payload)) {
        std::this_thread::yield();
      }
      ++next_serial_position;
    }
  }
}

template <bool Ordered, typename Queue, std::size_t Bytes>
void observe_messages(Queue& ring, std::uint64_t count, std::uint64_t phase_base,
                      std::uint64_t& checksum, std::atomic<bool>& valid) {
  for (std::uint64_t sequence = 0; sequence < count; ++sequence) {
    if constexpr (Ordered) {
      auto observation = ring.try_observe();
      while (!observation) {
        std::this_thread::yield();
        observation = ring.try_observe();
      }
      auto token = std::move(observation).value();
      if (token.position() != phase_base + sequence ||
          !observe_payload(token.value(), sequence, checksum)) {
        valid.store(false, std::memory_order_relaxed);
      }
      token.release();
    } else {
      Payload<Bytes> payload;
      while (!ring.try_pop(payload)) {
        std::this_thread::yield();
      }
      if (!observe_payload(payload, sequence, checksum)) {
        valid.store(false, std::memory_order_relaxed);
      }
    }
  }
}

void validate_placement_or_cancel(
    TrialControl& control, std::array<std::thread, mpsc_producer_count>& producers,
    std::thread& consumer,
    const std::array<PlacementResult, mpsc_producer_count>& producer_placements,
    const PlacementResult& consumer_placement) {
  const PlacementResult* failed = nullptr;
  std::string role;
  for (std::size_t index = 0; index < producer_placements.size(); ++index) {
    if (affinity_failed(producer_placements[index])) {
      failed = &producer_placements[index];
      role = "producer " + std::to_string(index);
      break;
    }
  }
  if (failed == nullptr && affinity_failed(consumer_placement)) {
    failed = &consumer_placement;
    role = "consumer";
  }
  if (failed == nullptr) {
    return;
  }

  cancel_trial(control);
  for (auto& producer : producers) {
    producer.join();
  }
  consumer.join();
  throw std::runtime_error(role + " affinity failed: " + failed->outcome.message);
}

template <bool Ordered, std::size_t Bytes, std::size_t Capacity>
TrialResult run_trial(const Options& options, unsigned int trial,
                      std::array<PlacementResult, mpsc_producer_count>& producer_placements,
                      PlacementResult& consumer_placement) {
  Ring<Ordered, Bytes, Capacity> ring;
  std::mutex producer_mutex;
  std::uint64_t next_serial_position = 0;
  TrialControl control;
  std::array<std::thread, mpsc_producer_count> producers;
  std::uint64_t warmup_checksum = 0;
  std::uint64_t checksum = 0;
  Clock::time_point stop;

  for (std::size_t index = 0; index < producers.size(); ++index) {
    producers[index] = std::thread([&, index] {
      const auto cpu = options.producer_cpus
                           ? std::optional<unsigned int>{(*options.producer_cpus)[index]}
                           : std::nullopt;
      producer_placements[index] = apply_affinity(cpu);
      signal_count(control.ready);
      if (!wait_for_phase(control.begin_warmup, control.cancel)) {
        return;
      }

      const auto warmup_count =
          options.warmup / mpsc_producer_count +
          static_cast<std::uint64_t>(index < options.warmup % mpsc_producer_count);
      publish_messages<Ordered, Ring<Ordered, Bytes, Capacity>, Bytes>(
          ring, producer_mutex, next_serial_position, warmup_count, 0);
      signal_count(control.warmed);
      if (!wait_for_phase(control.begin_timed, control.cancel)) {
        return;
      }

      const auto timed_count =
          options.iterations / mpsc_producer_count +
          static_cast<std::uint64_t>(index < options.iterations % mpsc_producer_count);
      publish_messages<Ordered, Ring<Ordered, Bytes, Capacity>, Bytes>(
          ring, producer_mutex, next_serial_position, timed_count, options.warmup);
    });
  }

  std::thread consumer([&] {
    consumer_placement = apply_affinity(options.consumer_cpu);
    signal_count(control.ready);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    observe_messages<Ordered, Ring<Ordered, Bytes, Capacity>, Bytes>(
        ring, options.warmup, 0, warmup_checksum, control.valid);
    signal_count(control.warmed);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    observe_messages<Ordered, Ring<Ordered, Bytes, Capacity>, Bytes>(
        ring, options.iterations, options.warmup, checksum, control.valid);
    stop = Clock::now();
    signal_done(control.done);
  });

  wait_for_count(control.ready, mpsc_producer_count + 1);
  validate_placement_or_cancel(control, producers, consumer, producer_placements,
                               consumer_placement);
  release_phase(control.begin_warmup);
  wait_for_count(control.warmed, mpsc_producer_count + 1);
  const auto start = Clock::now();
  release_phase(control.begin_timed);
  wait_for_done(control.done);
  for (auto& producer : producers) {
    producer.join();
  }
  consumer.join();

  if (!control.valid.load(std::memory_order_relaxed) ||
      warmup_checksum != expected_checksum<Bytes>(options.warmup) ||
      checksum != expected_checksum<Bytes>(options.iterations)) {
    throw std::runtime_error("MPSC throughput payload validation failed");
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
  return {.trial = trial,
          .elapsed_ns = elapsed,
          .messages_per_second = rate_per_second(options.iterations, elapsed),
          .latency_ns = std::nullopt,
          .latency_p95_ns = std::nullopt,
          .latency_p99_ns = std::nullopt,
          .checksum = checksum};
}

template <bool Ordered, std::size_t Bytes, std::size_t Capacity>
RunResults run_trials(const Options& options) {
  RunResults results;
  results.trials.reserve(options.trials);
  for (unsigned int trial = 1; trial <= options.trials; ++trial) {
    std::array<PlacementResult, mpsc_producer_count> producer_placements;
    PlacementResult consumer_placement;
    auto result = run_trial<Ordered, Bytes, Capacity>(options, trial, producer_placements,
                                                      consumer_placement);
    if (trial == 1) {
      results.producer_placements = std::move(producer_placements);
      results.consumer_placement = std::move(consumer_placement);
    }
    results.trials.push_back(std::move(result));
  }
  return results;
}

template <bool Ordered, std::size_t Bytes> RunResults dispatch_capacity(const Options& options) {
  switch (options.capacity_slots) {
  case 64:
    return run_trials<Ordered, Bytes, 64>(options);
  case 1'024:
    return run_trials<Ordered, Bytes, 1'024>(options);
  default:
    throw std::logic_error("validated MPSC capacity was not dispatched");
  }
}

template <bool Ordered> RunResults dispatch_payload(const Options& options) {
  switch (options.payload_bytes) {
  case 8:
    return dispatch_capacity<Ordered, 8>(options);
  case 64:
    return dispatch_capacity<Ordered, 64>(options);
  case 256:
    return dispatch_capacity<Ordered, 256>(options);
  default:
    throw std::logic_error("validated MPSC payload size was not dispatched");
  }
}

} // namespace

RunResults run_mpsc_throughput(const Options& options) {
  if (options.implementation == Implementation::mpsc_ordered) {
    return dispatch_payload<true>(options);
  }
  if (options.implementation == Implementation::mpsc_serialized) {
    return dispatch_payload<false>(options);
  }
  throw std::logic_error("unknown MPSC implementation");
}

} // namespace handoff::bench
