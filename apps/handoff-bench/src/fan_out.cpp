#include "rate.hpp"
#include "workload_support.hpp"
#include "workloads.hpp"

#include "handoff/sequence/bounded_sequence_fan_out.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace handoff::bench {
namespace {

template <std::size_t Bytes, std::size_t Capacity>
using FanOutRing =
    sequence::BoundedSequenceFanOut<Payload<Bytes>, Capacity, fan_out_consumer_count>;

template <typename Ring, std::size_t Bytes> void publish_messages(Ring& ring, std::uint64_t count) {
  for (std::uint64_t sequence = 0; sequence < count; ++sequence) {
    auto claim = ring.try_claim();
    while (!claim) {
      std::this_thread::yield();
      claim = ring.try_claim();
    }
    auto token = std::move(claim).value();
    token.value() = make_payload<Bytes>(sequence);
    token.publish();
  }
}

template <typename Ring, std::size_t Bytes>
void observe_messages(Ring& ring, std::size_t consumer_index, std::uint64_t count,
                      std::uint64_t& checksum, std::atomic<bool>& valid) {
  for (std::uint64_t sequence = 0; sequence < count; ++sequence) {
    auto observation = ring.try_observe(consumer_index);
    while (!observation) {
      std::this_thread::yield();
      observation = ring.try_observe(consumer_index);
    }
    auto token = std::move(observation).value();
    if (token.consumer_index() != consumer_index ||
        !observe_payload(token.value(), sequence, checksum)) {
      valid.store(false, std::memory_order_relaxed);
    }
    token.release();
  }
}

template <std::size_t Bytes, std::size_t Capacity>
TrialResult
run_fan_out_trial(const Options& options, unsigned int trial, PlacementResult& producer_placement,
                  std::array<PlacementResult, fan_out_consumer_count>& consumer_placements) {
  FanOutRing<Bytes, Capacity> ring;
  TrialControl control;
  std::array<std::uint64_t, fan_out_consumer_count> checksums{};
  std::array<std::thread, fan_out_consumer_count> consumers;
  std::atomic<unsigned int> consumers_done{0};
  Clock::time_point stop;
  const auto expected = expected_checksum<Bytes>(options.iterations);

  std::thread producer([&] {
    producer_placement = apply_affinity(options.producer_cpu);
    signal_count(control.ready);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    publish_messages<FanOutRing<Bytes, Capacity>, Bytes>(ring, options.warmup);
    signal_count(control.warmed);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    publish_messages<FanOutRing<Bytes, Capacity>, Bytes>(ring, options.iterations);
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

      std::uint64_t warmup_checksum = 0;
      observe_messages<FanOutRing<Bytes, Capacity>, Bytes>(ring, index, options.warmup,
                                                           warmup_checksum, control.valid);
      signal_count(control.warmed);
      if (!wait_for_phase(control.begin_timed, control.cancel)) {
        return;
      }

      observe_messages<FanOutRing<Bytes, Capacity>, Bytes>(ring, index, options.iterations,
                                                           checksums[index], control.valid);
      if (consumers_done.fetch_add(1, std::memory_order_acq_rel) + 1 == fan_out_consumer_count) {
        stop = Clock::now();
        signal_done(control.done);
      }
    });
  }

  constexpr auto participant_count = static_cast<unsigned int>(fan_out_consumer_count + 1);
  wait_for_count(control.ready, participant_count);
  validate_affinity_or_cancel(
      control, producer, consumers, producer_placement, consumer_placements,
      std::array<std::string_view, fan_out_consumer_count>{"consumer 0", "consumer 1"});
  release_phase(control.begin_warmup);
  wait_for_count(control.warmed, participant_count);
  const auto start = Clock::now();
  release_phase(control.begin_timed);
  wait_for_done(control.done);

  producer.join();
  for (auto& consumer : consumers) {
    consumer.join();
  }

  if (!control.valid.load(std::memory_order_relaxed)) {
    throw std::runtime_error("fan-out throughput payload validation failed");
  }
  for (const auto checksum : checksums) {
    if (checksum != expected) {
      throw std::runtime_error("fan-out throughput checksum validation failed");
    }
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
  const auto rate = rate_per_second(options.iterations, elapsed);
  return {.trial = trial,
          .elapsed_ns = elapsed,
          .messages_per_second = rate,
          .latency_ns = std::nullopt,
          .latency_p95_ns = std::nullopt,
          .latency_p99_ns = std::nullopt,
          .checksum = checksums.front()};
}

template <std::size_t Bytes, std::size_t Capacity> RunResults run_trials(const Options& options) {
  RunResults results;
  results.trials.reserve(options.trials);
  for (unsigned int trial = 1; trial <= options.trials; ++trial) {
    PlacementResult producer_placement;
    std::array<PlacementResult, fan_out_consumer_count> consumer_placements;
    auto result =
        run_fan_out_trial<Bytes, Capacity>(options, trial, producer_placement, consumer_placements);
    if (trial == 1) {
      results.producer_placement = std::move(producer_placement);
      results.consumer_placements = std::move(consumer_placements);
    }
    results.trials.push_back(std::move(result));
  }
  return results;
}

template <std::size_t Bytes> RunResults dispatch_capacity(const Options& options) {
  switch (options.capacity_slots) {
  case 64:
    return run_trials<Bytes, 64>(options);
  case 1'024:
    return run_trials<Bytes, 1'024>(options);
  default:
    throw std::logic_error("validated capacity was not dispatched");
  }
}

} // namespace

RunResults run_fan_out_throughput(const Options& options) {
  switch (options.payload_bytes) {
  case 8:
    return dispatch_capacity<8>(options);
  case 64:
    return dispatch_capacity<64>(options);
  case 256:
    return dispatch_capacity<256>(options);
  default:
    throw std::logic_error("validated payload size was not dispatched");
  }
}

} // namespace handoff::bench
