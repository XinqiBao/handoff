#include "rate.hpp"
#include "workload_support.hpp"
#include "workloads.hpp"

#include "handoff/spsc/basic_bounded_ring.hpp"
#include "handoff/spsc/cache_line_bounded_ring.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace handoff::bench {
namespace {

template <template <typename, std::size_t> typename Ring, std::size_t Bytes, std::size_t Capacity>
TrialResult run_throughput_trial(const Options& options, unsigned int trial,
                                 PlacementResult& producer_placement,
                                 PlacementResult& consumer_placement) {
  using Queue = Ring<Payload<Bytes>, Capacity>;
  Queue ring;
  TrialControl control;
  Clock::time_point stop;
  std::uint64_t checksum = 0;
  const auto expected = expected_checksum<Bytes>(options.iterations);

  std::thread producer([&] {
    producer_placement = apply_affinity(options.producer_cpu);
    control.ready.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    for (std::uint64_t sequence = 0; sequence < options.warmup; ++sequence) {
      auto payload = make_payload<Bytes>(sequence);
      while (!ring.try_push(payload)) {
        std::this_thread::yield();
      }
    }
    control.warmed.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    for (std::uint64_t sequence = 0; sequence < options.iterations; ++sequence) {
      auto payload = make_payload<Bytes>(sequence);
      while (!ring.try_push(payload)) {
        std::this_thread::yield();
      }
    }
  });

  std::thread consumer([&] {
    consumer_placement = apply_affinity(options.consumer_cpu);
    control.ready.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    Payload<Bytes> payload;
    std::uint64_t warmup_checksum = 0;
    for (std::uint64_t sequence = 0; sequence < options.warmup; ++sequence) {
      while (!ring.try_pop(payload)) {
        std::this_thread::yield();
      }
      if (!observe_payload(payload, sequence, warmup_checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
    }
    control.warmed.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    for (std::uint64_t sequence = 0; sequence < options.iterations; ++sequence) {
      while (!ring.try_pop(payload)) {
        std::this_thread::yield();
      }
      if (!observe_payload(payload, sequence, checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
    }
    stop = Clock::now();
    control.done.store(true, std::memory_order_release);
  });

  wait_for_count(control.ready, 2);
  validate_affinity_or_cancel(control, producer, consumer, producer_placement, consumer_placement);
  control.begin_warmup.store(true, std::memory_order_release);
  wait_for_count(control.warmed, 2);
  const auto start = Clock::now();
  control.begin_timed.store(true, std::memory_order_release);
  while (!control.done.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  producer.join();
  consumer.join();

  if (!control.valid.load(std::memory_order_relaxed) || checksum != expected) {
    throw std::runtime_error("throughput payload validation failed");
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
  const auto rate = rate_per_second(options.iterations, elapsed);
  return {.trial = trial,
          .elapsed_ns = elapsed,
          .messages_per_second = rate,
          .latency_ns = std::nullopt,
          .latency_p95_ns = std::nullopt,
          .latency_p99_ns = std::nullopt,
          .checksum = checksum};
}

template <template <typename, std::size_t> typename Ring, std::size_t Bytes, std::size_t Capacity>
RunResults run_spsc(const Options& options) {
  RunResults results;
  results.trials.reserve(options.trials);
  for (unsigned int trial = 1; trial <= options.trials; ++trial) {
    PlacementResult producer_placement;
    PlacementResult consumer_placement;
    auto result = run_throughput_trial<Ring, Bytes, Capacity>(options, trial, producer_placement,
                                                              consumer_placement);
    if (trial == 1) {
      results.producer_placement = std::move(producer_placement);
      results.consumer_placement = std::move(consumer_placement);
    }
    results.trials.push_back(result);
  }
  return results;
}

template <template <typename, std::size_t> typename Ring, std::size_t Bytes>
RunResults dispatch_capacity(const Options& options) {
  switch (options.capacity_slots) {
  case 64:
    return run_spsc<Ring, Bytes, 64>(options);
  case 1'024:
    return run_spsc<Ring, Bytes, 1'024>(options);
  default:
    throw std::logic_error("validated capacity was not dispatched");
  }
}

template <template <typename, std::size_t> typename Ring>
RunResults dispatch_payload(const Options& options) {
  switch (options.payload_bytes) {
  case 8:
    return dispatch_capacity<Ring, 8>(options);
  case 64:
    return dispatch_capacity<Ring, 64>(options);
  case 256:
    return dispatch_capacity<Ring, 256>(options);
  default:
    throw std::logic_error("validated payload size was not dispatched");
  }
}

} // namespace

RunResults run_throughput(const Options& options) {
  switch (options.implementation) {
  case Implementation::basic:
    return dispatch_payload<spsc::BasicBoundedRing>(options);
  case Implementation::cache_line:
    return dispatch_payload<spsc::CacheLineBoundedRing>(options);
  }
  throw std::logic_error("unknown implementation");
}

} // namespace handoff::bench
