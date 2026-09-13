#include "rate.hpp"
#include "workload_support.hpp"
#include "workloads.hpp"

#include "handoff/spsc/basic_bounded_ring.hpp"
#include "handoff/spsc/batch_bounded_ring.hpp"
#include "handoff/spsc/bulk_burst_bounded_ring.hpp"
#include "handoff/spsc/cache_line_bounded_ring.hpp"
#include "handoff/spsc/cached_index_bounded_ring.hpp"
#include "handoff/spsc/staged_bounded_ring.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>

namespace handoff::bench {
namespace {

enum class GroupOperation { scalar, batch, bulk, burst, staged };

template <GroupOperation Operation, std::size_t Bytes, std::size_t BatchSize, typename Queue>
void push_messages(Queue& ring, std::uint64_t count) {
  if constexpr (Operation == GroupOperation::staged) {
    for (std::uint64_t first = 0; first < count; first += BatchSize) {
      auto reservation = ring.try_reserve_push(BatchSize);
      while (!reservation) {
        std::this_thread::yield();
        reservation = ring.try_reserve_push(BatchSize);
      }
      auto token = std::move(reservation).value();
      std::size_t offset = 0;
      for (auto& slot : token.first()) {
        slot = make_payload<Bytes>(first + offset++);
      }
      for (auto& slot : token.second()) {
        slot = make_payload<Bytes>(first + offset++);
      }
      token.finish();
    }
  } else {
    std::array<Payload<Bytes>, BatchSize> payloads{};
    for (std::uint64_t first = 0; first < count; first += BatchSize) {
      for (std::size_t offset = 0; offset < BatchSize; ++offset) {
        payloads[offset] = make_payload<Bytes>(first + offset);
      }
      if constexpr (Operation == GroupOperation::batch) {
        while (!ring.try_push_batch(std::span<const Payload<Bytes>>(payloads))) {
          std::this_thread::yield();
        }
      } else if constexpr (Operation == GroupOperation::bulk) {
        while (!ring.try_push_bulk(std::span<const Payload<Bytes>>(payloads))) {
          std::this_thread::yield();
        }
      } else if constexpr (Operation == GroupOperation::burst) {
        std::size_t completed = 0;
        while (completed < payloads.size()) {
          const auto pushed =
              ring.try_push_burst(std::span<const Payload<Bytes>>(payloads).subspan(completed));
          completed += pushed;
          if (pushed == 0) {
            std::this_thread::yield();
          }
        }
      } else {
        for (const auto& payload : payloads) {
          while (!ring.try_push(payload)) {
            std::this_thread::yield();
          }
        }
      }
    }
  }
}

template <GroupOperation Operation, std::size_t Bytes, std::size_t BatchSize, typename Queue>
void pop_messages(Queue& ring, std::uint64_t count, std::uint64_t& checksum,
                  std::atomic<bool>& valid) {
  if constexpr (Operation == GroupOperation::staged) {
    for (std::uint64_t first = 0; first < count; first += BatchSize) {
      auto reservation = ring.try_reserve_pop(BatchSize);
      while (!reservation) {
        std::this_thread::yield();
        reservation = ring.try_reserve_pop(BatchSize);
      }
      auto token = std::move(reservation).value();
      std::size_t offset = 0;
      for (const auto& payload : token.first()) {
        if (!observe_payload(payload, first + offset++, checksum)) {
          valid.store(false, std::memory_order_relaxed);
        }
      }
      for (const auto& payload : token.second()) {
        if (!observe_payload(payload, first + offset++, checksum)) {
          valid.store(false, std::memory_order_relaxed);
        }
      }
      token.finish();
    }
  } else {
    std::array<Payload<Bytes>, BatchSize> payloads{};
    for (std::uint64_t first = 0; first < count; first += BatchSize) {
      if constexpr (Operation == GroupOperation::batch) {
        while (!ring.try_pop_batch(std::span<Payload<Bytes>>(payloads))) {
          std::this_thread::yield();
        }
      } else if constexpr (Operation == GroupOperation::bulk) {
        while (!ring.try_pop_bulk(std::span<Payload<Bytes>>(payloads))) {
          std::this_thread::yield();
        }
      } else if constexpr (Operation == GroupOperation::burst) {
        std::size_t completed = 0;
        while (completed < payloads.size()) {
          const auto popped =
              ring.try_pop_burst(std::span<Payload<Bytes>>(payloads).subspan(completed));
          completed += popped;
          if (popped == 0) {
            std::this_thread::yield();
          }
        }
      } else {
        for (auto& payload : payloads) {
          while (!ring.try_pop(payload)) {
            std::this_thread::yield();
          }
        }
      }
      for (std::size_t offset = 0; offset < BatchSize; ++offset) {
        if (!observe_payload(payloads[offset], first + offset, checksum)) {
          valid.store(false, std::memory_order_relaxed);
        }
      }
    }
  }
}

template <GroupOperation Operation, template <typename, std::size_t> typename Ring,
          std::size_t Bytes, std::size_t Capacity, std::size_t BatchSize>
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

    push_messages<Operation, Bytes, BatchSize>(ring, options.warmup);
    control.warmed.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    push_messages<Operation, Bytes, BatchSize>(ring, options.iterations);
  });

  std::thread consumer([&] {
    consumer_placement = apply_affinity(options.consumer_cpu);
    control.ready.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    std::uint64_t warmup_checksum = 0;
    pop_messages<Operation, Bytes, BatchSize>(ring, options.warmup, warmup_checksum, control.valid);
    control.warmed.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    pop_messages<Operation, Bytes, BatchSize>(ring, options.iterations, checksum, control.valid);
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

template <GroupOperation Operation, template <typename, std::size_t> typename Ring,
          std::size_t Bytes, std::size_t Capacity, std::size_t BatchSize>
RunResults run_spsc(const Options& options) {
  RunResults results;
  results.trials.reserve(options.trials);
  for (unsigned int trial = 1; trial <= options.trials; ++trial) {
    PlacementResult producer_placement;
    PlacementResult consumer_placement;
    auto result = run_throughput_trial<Operation, Ring, Bytes, Capacity, BatchSize>(
        options, trial, producer_placement, consumer_placement);
    if (trial == 1) {
      results.producer_placement = std::move(producer_placement);
      results.consumer_placement = std::move(consumer_placement);
    }
    results.trials.push_back(result);
  }
  return results;
}

template <GroupOperation Operation, template <typename, std::size_t> typename Ring,
          std::size_t Bytes>
RunResults dispatch_capacity(const Options& options) {
  switch (options.capacity_slots) {
  case 64:
    switch (options.batch_size) {
    case 1:
      return run_spsc<Operation, Ring, Bytes, 64, 1>(options);
    case 4:
      return run_spsc<Operation, Ring, Bytes, 64, 4>(options);
    case 16:
      return run_spsc<Operation, Ring, Bytes, 64, 16>(options);
    default:
      throw std::logic_error("validated batch size was not dispatched");
    }
  case 1'024:
    switch (options.batch_size) {
    case 1:
      return run_spsc<Operation, Ring, Bytes, 1'024, 1>(options);
    case 4:
      return run_spsc<Operation, Ring, Bytes, 1'024, 4>(options);
    case 16:
      return run_spsc<Operation, Ring, Bytes, 1'024, 16>(options);
    default:
      throw std::logic_error("validated batch size was not dispatched");
    }
  default:
    throw std::logic_error("validated capacity was not dispatched");
  }
}

template <GroupOperation Operation, template <typename, std::size_t> typename Ring>
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

RunResults run_throughput(const Options& options) {
  switch (options.implementation) {
  case Implementation::basic:
    return dispatch_payload<GroupOperation::scalar, spsc::BasicBoundedRing>(options);
  case Implementation::batch:
    return dispatch_payload<GroupOperation::batch, spsc::BatchBoundedRing>(options);
  case Implementation::bulk:
    return dispatch_payload<GroupOperation::bulk, spsc::BulkBurstBoundedRing>(options);
  case Implementation::burst:
    return dispatch_payload<GroupOperation::burst, spsc::BulkBurstBoundedRing>(options);
  case Implementation::cache_line:
    return dispatch_payload<GroupOperation::scalar, spsc::CacheLineBoundedRing>(options);
  case Implementation::cached_index:
    return dispatch_payload<GroupOperation::scalar, spsc::CachedIndexBoundedRing>(options);
  case Implementation::staged:
    return dispatch_payload<GroupOperation::staged, spsc::StagedBoundedRing>(options);
  }
  throw std::logic_error("unknown implementation");
}

} // namespace handoff::bench
