#include "workload_support.hpp"
#include "workloads.hpp"

#include "handoff/sequence/bounded_sequence_ring.hpp"
#include "handoff/spsc/basic_bounded_ring.hpp"
#include "handoff/spsc/batch_bounded_ring.hpp"
#include "handoff/spsc/cache_line_bounded_ring.hpp"
#include "handoff/spsc/cached_index_bounded_ring.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace handoff::bench {
namespace {

enum class QueueOperation { push_pop, sequence };

struct LatencySummary {
  double median_ns;
  double p95_ns;
  double p99_ns;
};

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

template <QueueOperation Operation, typename Queue>
bool try_send(Queue& queue, const typename Queue::value_type& value) {
  if constexpr (Operation == QueueOperation::sequence) {
    auto claim = queue.try_claim();
    if (!claim) {
      return false;
    }
    auto token = std::move(claim).value();
    token.value() = value;
    token.publish();
    return true;
  } else {
    return queue.try_push(value);
  }
}

template <QueueOperation Operation, typename Queue>
bool try_receive(Queue& queue, typename Queue::value_type& value) {
  if constexpr (Operation == QueueOperation::sequence) {
    auto observation = queue.try_observe();
    if (!observation) {
      return false;
    }
    auto token = std::move(observation).value();
    value = token.value();
    token.release();
    return true;
  } else {
    return queue.try_pop(value);
  }
}

template <QueueOperation Operation, template <typename, std::size_t> typename Ring,
          std::size_t Bytes, std::size_t Capacity>
TrialResult run_ping_pong_trial(const Options& options, unsigned int trial,
                                PlacementResult& producer_placement,
                                PlacementResult& consumer_placement) {
  using Queue = Ring<Payload<Bytes>, Capacity>;
  Queue requests;
  Queue responses;
  TrialControl control;
  Clock::time_point stop;
  std::uint64_t checksum = 0;
  const auto expected = expected_checksum<Bytes>(options.iterations);
  std::vector<std::int64_t> rtt_samples(static_cast<std::size_t>(options.iterations));

  std::thread producer([&] {
    producer_placement = apply_affinity(options.producer_cpu);
    control.ready.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    Payload<Bytes> response;
    std::uint64_t warmup_checksum = 0;
    for (std::uint64_t sequence = 0; sequence < options.warmup; ++sequence) {
      auto request = make_payload<Bytes>(sequence);
      while (!try_send<Operation>(requests, request)) {
        std::this_thread::yield();
      }
      while (!try_receive<Operation>(responses, response)) {
        std::this_thread::yield();
      }
      if (!observe_payload(response, sequence, warmup_checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
    }
    control.warmed.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    for (std::uint64_t sequence = 0; sequence < options.iterations; ++sequence) {
      auto request = make_payload<Bytes>(sequence);
      const auto sample_start = Clock::now();
      while (!try_send<Operation>(requests, request)) {
        std::this_thread::yield();
      }
      while (!try_receive<Operation>(responses, response)) {
        std::this_thread::yield();
      }
      const auto sample_stop = Clock::now();
      rtt_samples[static_cast<std::size_t>(sequence)] =
          std::chrono::duration_cast<std::chrono::nanoseconds>(sample_stop - sample_start).count();
      if (!observe_payload(response, sequence, checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
    }
    stop = Clock::now();
    control.done.store(true, std::memory_order_release);
  });

  std::thread consumer([&] {
    consumer_placement = apply_affinity(options.consumer_cpu);
    control.ready.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    Payload<Bytes> request;
    std::uint64_t ignored_checksum = 0;
    for (std::uint64_t sequence = 0; sequence < options.warmup; ++sequence) {
      while (!try_receive<Operation>(requests, request)) {
        std::this_thread::yield();
      }
      if (!observe_payload(request, sequence, ignored_checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
      while (!try_send<Operation>(responses, request)) {
        std::this_thread::yield();
      }
    }
    control.warmed.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    for (std::uint64_t sequence = 0; sequence < options.iterations; ++sequence) {
      while (!try_receive<Operation>(requests, request)) {
        std::this_thread::yield();
      }
      if (!observe_payload(request, sequence, ignored_checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
      while (!try_send<Operation>(responses, request)) {
        std::this_thread::yield();
      }
    }
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
    throw std::runtime_error("ping-pong payload validation failed");
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
  const auto latency = summarize_latency(std::move(rtt_samples));
  return {.trial = trial,
          .elapsed_ns = elapsed,
          .messages_per_second = std::nullopt,
          .latency_ns = latency.median_ns,
          .latency_p95_ns = latency.p95_ns,
          .latency_p99_ns = latency.p99_ns,
          .checksum = checksum};
}

template <QueueOperation Operation, template <typename, std::size_t> typename Ring,
          std::size_t Bytes, std::size_t Capacity>
RunResults run_spsc(const Options& options) {
  RunResults results;
  results.trials.reserve(options.trials);
  for (unsigned int trial = 1; trial <= options.trials; ++trial) {
    PlacementResult producer_placement;
    PlacementResult consumer_placement;
    auto result = run_ping_pong_trial<Operation, Ring, Bytes, Capacity>(
        options, trial, producer_placement, consumer_placement);
    if (trial == 1) {
      results.producer_placement = std::move(producer_placement);
      results.consumer_placement = std::move(consumer_placement);
    }
    results.trials.push_back(result);
  }
  return results;
}

template <QueueOperation Operation, template <typename, std::size_t> typename Ring,
          std::size_t Bytes>
RunResults dispatch_capacity(const Options& options) {
  switch (options.capacity_slots) {
  case 64:
    return run_spsc<Operation, Ring, Bytes, 64>(options);
  case 1'024:
    return run_spsc<Operation, Ring, Bytes, 1'024>(options);
  default:
    throw std::logic_error("validated capacity was not dispatched");
  }
}

template <QueueOperation Operation, template <typename, std::size_t> typename Ring>
RunResults dispatch_payload(const Options& options) {
  switch (options.payload_bytes) {
  case 8:
    return dispatch_capacity<Operation, Ring, 8>(options);
  case 64:
    return dispatch_capacity<Operation, Ring, 64>(options);
  case 256:
    return dispatch_capacity<Operation, Ring, 256>(options);
  default:
    throw std::logic_error("validated payload size was not dispatched");
  }
}

} // namespace

RunResults run_ping_pong(const Options& options) {
  switch (options.implementation) {
  case Implementation::basic:
    return dispatch_payload<QueueOperation::push_pop, spsc::BasicBoundedRing>(options);
  case Implementation::batch:
    return dispatch_payload<QueueOperation::push_pop, spsc::BatchBoundedRing>(options);
  case Implementation::bulk:
  case Implementation::burst:
    throw std::logic_error("bulk and burst implementations are not ping-pong modes");
  case Implementation::cache_line:
    return dispatch_payload<QueueOperation::push_pop, spsc::CacheLineBoundedRing>(options);
  case Implementation::cached_index:
    return dispatch_payload<QueueOperation::push_pop, spsc::CachedIndexBoundedRing>(options);
  case Implementation::sequence:
    return dispatch_payload<QueueOperation::sequence, sequence::BoundedSequenceRing>(options);
  case Implementation::staged:
    throw std::logic_error("staged implementation is not a ping-pong mode");
  }
  throw std::logic_error("unknown implementation");
}

} // namespace handoff::bench
