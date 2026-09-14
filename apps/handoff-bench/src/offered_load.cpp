#include "workloads.hpp"

#include "offered_load_support.hpp"
#include "rate.hpp"
#include "workload_support.hpp"

#include "handoff/metadata/sequence_payload_ring.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>

namespace handoff::bench {
namespace {

struct PhaseResult {
  std::uint64_t observed{};
  std::uint64_t overwritten{};
  std::uint64_t retries{};
  std::uint64_t payload_bytes{};
  std::uint64_t checksum{};
};

template <typename Ring, std::size_t Bytes>
void publish_phase(Ring& ring, std::uint64_t count, std::uint64_t interval_ns,
                   std::atomic<bool>& producer_done, std::atomic<bool>& valid) {
  auto deadline = Clock::now();
  const auto interval = std::chrono::nanoseconds(static_cast<std::int64_t>(interval_ns));
  for (std::uint64_t logical_sequence = 0; logical_sequence < count; ++logical_sequence) {
    if (logical_sequence != 0 && interval_ns != 0) {
      deadline += interval;
      std::this_thread::sleep_until(deadline);
    }

    const auto payload = make_payload<Bytes>(logical_sequence);
    const auto result =
        ring.try_publish(logical_sequence, static_cast<std::uint32_t>(logical_sequence),
                         std::span<const std::byte>(payload.bytes));
    if (result != Ring::PublishResult::success) {
      valid.store(false, std::memory_order_relaxed);
      break;
    }
  }
  producer_done.store(true, std::memory_order_release);
}

template <typename Ring, std::size_t Bytes>
PhaseResult observe_phase(const Ring& ring, std::uint64_t first_sequence, std::uint64_t count,
                          const Options& options, const std::atomic<bool>& producer_done,
                          std::atomic<bool>& valid) {
  PhaseResult result;
  if (count == 0) {
    return result;
  }

  const auto last_sequence = first_sequence + count - 1;
  auto requested = first_sequence;
  while (requested <= last_sequence) {
    metadata::Metadata observed_metadata;
    Payload<Bytes> payload;
    switch (ring.try_read(requested, observed_metadata, std::span<std::byte>(payload.bytes))) {
    case Ring::ReadResult::success: {
      const auto logical_sequence = requested - first_sequence;
      const auto expected_chunk = (requested - Ring::first_sequence()) & (Ring::capacity() - 1);
      const bool metadata_valid =
          observed_metadata.signature == logical_sequence &&
          observed_metadata.chunk == expected_chunk && observed_metadata.length == Bytes &&
          observed_metadata.control == static_cast<std::uint32_t>(logical_sequence);
      if (!metadata_valid || !observe_payload(payload, logical_sequence, result.checksum) ||
          result.payload_bytes >
              std::numeric_limits<std::uint64_t>::max() - observed_metadata.length) {
        valid.store(false, std::memory_order_relaxed);
        return result;
      }
      result.payload_bytes += observed_metadata.length;
      ++result.observed;
      ++requested;
      if (options.consumer_stall_every != 0 &&
          result.observed % options.consumer_stall_every == 0 && requested <= last_sequence) {
        std::this_thread::sleep_for(
            std::chrono::nanoseconds(static_cast<std::int64_t>(options.consumer_stall_ns)));
      }
      break;
    }
    case Ring::ReadResult::overwritten: {
      const auto range = ring.available_range();
      const auto resynchronization =
          resynchronize_after_overwrite(requested, range.oldest, last_sequence);
      result.overwritten += resynchronization.skipped_sequences;
      requested = resynchronization.next_sequence;
      break;
    }
    case Ring::ReadResult::retry:
      ++result.retries;
      break;
    case Ring::ReadResult::not_yet_published:
      if (producer_done.load(std::memory_order_acquire) && ring.published_sequence() < requested) {
        valid.store(false, std::memory_order_relaxed);
        return result;
      }
      std::this_thread::yield();
      break;
    case Ring::ReadResult::invalid_sequence:
    case Ring::ReadResult::output_too_small:
      valid.store(false, std::memory_order_relaxed);
      return result;
    }
  }
  return result;
}

template <std::size_t Capacity, std::size_t Bytes>
TrialResult run_offered_load_trial(const Options& options, unsigned int trial,
                                   PlacementResult& producer_placement,
                                   PlacementResult& consumer_placement) {
  using Ring = metadata::SequencePayloadRing<Capacity, Bytes>;

  Ring ring;
  TrialControl control;
  std::atomic<bool> warmup_producer_done{false};
  std::atomic<bool> timed_producer_done{false};
  Clock::time_point stop;
  PhaseResult timed_result;

  std::thread producer([&] {
    producer_placement = apply_affinity(options.producer_cpu);
    signal_count(control.ready);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    publish_phase<Ring, Bytes>(ring, options.warmup, options.producer_interval_ns,
                               warmup_producer_done, control.valid);
    signal_count(control.warmed);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    publish_phase<Ring, Bytes>(ring, options.iterations, options.producer_interval_ns,
                               timed_producer_done, control.valid);
  });

  std::thread consumer([&] {
    consumer_placement = apply_affinity(options.consumer_cpu);
    signal_count(control.ready);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    static_cast<void>(observe_phase<Ring, Bytes>(ring, Ring::first_sequence(), options.warmup,
                                                 options, warmup_producer_done, control.valid));
    signal_count(control.warmed);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    const auto first_timed_sequence = Ring::first_sequence() + options.warmup;
    timed_result = observe_phase<Ring, Bytes>(ring, first_timed_sequence, options.iterations,
                                              options, timed_producer_done, control.valid);
    stop = Clock::now();
    signal_done(control.done);
  });

  wait_for_count(control.ready, 2);
  validate_affinity_or_cancel(control, producer, consumer, producer_placement, consumer_placement);
  release_phase(control.begin_warmup);
  wait_for_count(control.warmed, 2);
  const auto start = Clock::now();
  release_phase(control.begin_timed);
  wait_for_done(control.done);
  producer.join();
  consumer.join();

  if (!control.valid.load(std::memory_order_relaxed) ||
      timed_result.observed + timed_result.overwritten != options.iterations) {
    throw std::runtime_error("offered-load accounting or payload validation failed");
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
  return {.trial = trial,
          .elapsed_ns = elapsed,
          .messages_per_second = std::nullopt,
          .latency_ns = std::nullopt,
          .latency_p95_ns = std::nullopt,
          .latency_p99_ns = std::nullopt,
          .checksum = timed_result.checksum,
          .offered_messages = options.iterations,
          .observed_messages = timed_result.observed,
          .overwritten_messages = timed_result.overwritten,
          .retry_attempts = timed_result.retries,
          .observed_payload_bytes = timed_result.payload_bytes,
          .offered_messages_per_second = rate_per_second(options.iterations, elapsed),
          .observed_messages_per_second = rate_per_second(timed_result.observed, elapsed)};
}

template <std::size_t Capacity, std::size_t Bytes>
RunResults run_offered_load_configuration(const Options& options) {
  RunResults results;
  results.trials.reserve(options.trials);
  for (unsigned int trial = 1; trial <= options.trials; ++trial) {
    PlacementResult producer_placement;
    PlacementResult consumer_placement;
    auto result = run_offered_load_trial<Capacity, Bytes>(options, trial, producer_placement,
                                                          consumer_placement);
    if (trial == 1) {
      results.producer_placement = std::move(producer_placement);
      results.consumer_placement = std::move(consumer_placement);
    }
    results.trials.push_back(result);
  }
  return results;
}

template <std::size_t Bytes> RunResults dispatch_capacity(const Options& options) {
  switch (options.capacity_slots) {
  case 64:
    return run_offered_load_configuration<64, Bytes>(options);
  case 1'024:
    return run_offered_load_configuration<1'024, Bytes>(options);
  default:
    throw std::logic_error("validated capacity was not dispatched");
  }
}

} // namespace

RunResults run_offered_load(const Options& options) {
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
