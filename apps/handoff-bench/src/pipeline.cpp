#include "rate.hpp"
#include "workload_support.hpp"
#include "workloads.hpp"

#include "handoff/sequence/bounded_sequence_pipeline.hpp"

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
using PipelineRing = sequence::BoundedSequencePipeline<Payload<Bytes>, Capacity>;

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
void observe_upstream(Ring& ring, std::uint64_t count, std::uint64_t& checksum,
                      std::atomic<bool>& valid) {
  for (std::uint64_t sequence = 0; sequence < count; ++sequence) {
    auto observation = ring.try_observe_upstream();
    while (!observation) {
      std::this_thread::yield();
      observation = ring.try_observe_upstream();
    }
    auto token = std::move(observation).value();
    if (!observe_payload(token.value(), sequence, checksum)) {
      valid.store(false, std::memory_order_relaxed);
    }
    token.release();
  }
}

template <typename Ring, std::size_t Bytes>
void observe_downstream(Ring& ring, std::uint64_t count, std::uint64_t& checksum,
                        std::atomic<bool>& valid) {
  for (std::uint64_t sequence = 0; sequence < count; ++sequence) {
    auto observation = ring.try_observe_downstream();
    while (!observation) {
      std::this_thread::yield();
      observation = ring.try_observe_downstream();
    }
    auto token = std::move(observation).value();
    if (!observe_payload(token.value(), sequence, checksum)) {
      valid.store(false, std::memory_order_relaxed);
    }
    token.release();
  }
}

template <std::size_t Bytes, std::size_t Capacity>
TrialResult
run_pipeline_trial(const Options& options, unsigned int trial, PlacementResult& producer_placement,
                   std::array<PlacementResult, pipeline_consumer_count>& consumer_placements) {
  PipelineRing<Bytes, Capacity> ring;
  TrialControl control;
  Clock::time_point stop;
  std::uint64_t upstream_checksum = 0;
  std::uint64_t downstream_checksum = 0;
  const auto expected = expected_checksum<Bytes>(options.iterations);
  std::array<std::thread, pipeline_consumer_count> consumers;

  std::thread producer([&] {
    producer_placement = apply_affinity(options.producer_cpu);
    signal_count(control.ready);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    publish_messages<PipelineRing<Bytes, Capacity>, Bytes>(ring, options.warmup);
    signal_count(control.warmed);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    publish_messages<PipelineRing<Bytes, Capacity>, Bytes>(ring, options.iterations);
  });

  consumers[0] = std::thread([&] {
    const auto cpu = options.consumer_cpus
                         ? std::optional<unsigned int>{(*options.consumer_cpus)[0]}
                         : std::nullopt;
    consumer_placements[0] = apply_affinity(cpu);
    signal_count(control.ready);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    std::uint64_t warmup_checksum = 0;
    observe_upstream<PipelineRing<Bytes, Capacity>, Bytes>(ring, options.warmup, warmup_checksum,
                                                           control.valid);
    signal_count(control.warmed);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    observe_upstream<PipelineRing<Bytes, Capacity>, Bytes>(ring, options.iterations,
                                                           upstream_checksum, control.valid);
  });

  consumers[1] = std::thread([&] {
    const auto cpu = options.consumer_cpus
                         ? std::optional<unsigned int>{(*options.consumer_cpus)[1]}
                         : std::nullopt;
    consumer_placements[1] = apply_affinity(cpu);
    signal_count(control.ready);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    std::uint64_t warmup_checksum = 0;
    observe_downstream<PipelineRing<Bytes, Capacity>, Bytes>(ring, options.warmup, warmup_checksum,
                                                             control.valid);
    signal_count(control.warmed);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    observe_downstream<PipelineRing<Bytes, Capacity>, Bytes>(ring, options.iterations,
                                                             downstream_checksum, control.valid);
    stop = Clock::now();
    signal_done(control.done);
  });

  wait_for_count(control.ready, 3);
  validate_affinity_or_cancel(control, producer, consumers, producer_placement, consumer_placements,
                              std::array<std::string_view, pipeline_consumer_count>{
                                  "upstream consumer", "downstream consumer"});
  release_phase(control.begin_warmup);
  wait_for_count(control.warmed, 3);
  const auto start = Clock::now();
  release_phase(control.begin_timed);
  wait_for_done(control.done);

  producer.join();
  for (auto& consumer : consumers) {
    consumer.join();
  }

  if (!control.valid.load(std::memory_order_relaxed)) {
    throw std::runtime_error("sequence pipeline payload validation failed");
  }
  if (upstream_checksum != expected || downstream_checksum != expected) {
    throw std::runtime_error("sequence pipeline checksum validation failed");
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
  const auto rate = rate_per_second(options.iterations, elapsed);
  return {.trial = trial,
          .elapsed_ns = elapsed,
          .messages_per_second = rate,
          .latency_ns = std::nullopt,
          .latency_p95_ns = std::nullopt,
          .latency_p99_ns = std::nullopt,
          .checksum = downstream_checksum};
}

template <std::size_t Bytes, std::size_t Capacity> RunResults run_trials(const Options& options) {
  RunResults results;
  results.trials.reserve(options.trials);
  for (unsigned int trial = 1; trial <= options.trials; ++trial) {
    PlacementResult producer_placement;
    std::array<PlacementResult, pipeline_consumer_count> consumer_placements;
    auto result = run_pipeline_trial<Bytes, Capacity>(options, trial, producer_placement,
                                                      consumer_placements);
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

RunResults run_pipeline_throughput(const Options& options) {
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
