#include "rate.hpp"
#include "record_support.hpp"
#include "workload_support.hpp"
#include "workloads.hpp"

#include "handoff/sequence/bounded_sequence_ring.hpp"
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

enum class GroupOperation {
  scalar,
  batch,
  bulk,
  burst,
  byte_record,
  descriptor_record,
  fixed_record,
  sequence,
  staged
};

template <typename T, std::size_t Capacity>
using BenchmarkSequenceRing = sequence::BoundedSequenceRing<T, Capacity>;

template <GroupOperation Operation, std::size_t Bytes, std::size_t BatchSize, typename Queue>
void push_messages(Queue& ring, std::uint64_t count) {
  if constexpr (Operation == GroupOperation::byte_record ||
                Operation == GroupOperation::descriptor_record) {
    using PushResult = typename Queue::PushResult;
    for (std::uint64_t sequence = 0; sequence < count; ++sequence) {
      const auto value = make_record_message<Bytes>(sequence);
      auto result = ring.try_push(value.header, value.payload.bytes);
      while (result == PushResult::full) {
        std::this_thread::yield();
        result = ring.try_push(value.header, value.payload.bytes);
      }
      if (result != PushResult::success) {
        throw std::logic_error("benchmark produced an invalid record message");
      }
    }
  } else if constexpr (Operation == GroupOperation::fixed_record) {
    for (std::uint64_t sequence = 0; sequence < count; ++sequence) {
      const auto record = make_fixed_record<Bytes>(sequence);
      while (!ring.try_push(record)) {
        std::this_thread::yield();
      }
    }
  } else if constexpr (Operation == GroupOperation::sequence) {
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
  } else if constexpr (Operation == GroupOperation::staged) {
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
  if constexpr (Operation == GroupOperation::byte_record ||
                Operation == GroupOperation::descriptor_record) {
    using PopResult = typename Queue::PopResult;
    RecordMessage<Bytes> value;
    for (std::uint64_t sequence = 0; sequence < count; ++sequence) {
      auto result = ring.try_pop(value.header, value.payload.bytes);
      while (result == PopResult::empty) {
        std::this_thread::yield();
        result = ring.try_pop(value.header, value.payload.bytes);
      }
      if (result != PopResult::success) {
        throw std::logic_error("benchmark record output is too small");
      }
      if (!observe_record_message(value, sequence, checksum)) {
        valid.store(false, std::memory_order_relaxed);
      }
    }
  } else if constexpr (Operation == GroupOperation::fixed_record) {
    typename Queue::value_type record;
    for (std::uint64_t sequence = 0; sequence < count; ++sequence) {
      while (!ring.try_pop(record)) {
        std::this_thread::yield();
      }
      if (!observe_fixed_record(record, sequence, checksum)) {
        valid.store(false, std::memory_order_relaxed);
      }
    }
  } else if constexpr (Operation == GroupOperation::sequence) {
    for (std::uint64_t sequence = 0; sequence < count; ++sequence) {
      auto observation = ring.try_observe();
      while (!observation) {
        std::this_thread::yield();
        observation = ring.try_observe();
      }
      auto token = std::move(observation).value();
      if (!observe_payload(token.value(), sequence, checksum)) {
        valid.store(false, std::memory_order_relaxed);
      }
      token.release();
    }
  } else if constexpr (Operation == GroupOperation::staged) {
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

template <GroupOperation Operation, typename Queue, std::size_t Bytes, std::size_t BatchSize>
TrialResult run_throughput_trial(const Options& options, unsigned int trial,
                                 PlacementResult& producer_placement,
                                 PlacementResult& consumer_placement) {
  Queue ring;
  TrialControl control;
  Clock::time_point stop;
  std::uint64_t checksum = 0;
  const auto expected = expected_checksum<Bytes>(options.iterations);

  std::thread producer([&] {
    producer_placement = apply_affinity(options.producer_cpu);
    signal_count(control.ready);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    push_messages<Operation, Bytes, BatchSize>(ring, options.warmup);
    signal_count(control.warmed);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    push_messages<Operation, Bytes, BatchSize>(ring, options.iterations);
  });

  std::thread consumer([&] {
    consumer_placement = apply_affinity(options.consumer_cpu);
    signal_count(control.ready);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    std::uint64_t warmup_checksum = 0;
    pop_messages<Operation, Bytes, BatchSize>(ring, options.warmup, warmup_checksum, control.valid);
    signal_count(control.warmed);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    pop_messages<Operation, Bytes, BatchSize>(ring, options.iterations, checksum, control.valid);
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

template <GroupOperation Operation, typename Queue, std::size_t Bytes, std::size_t BatchSize>
RunResults run_spsc(const Options& options) {
  RunResults results;
  results.trials.reserve(options.trials);
  for (unsigned int trial = 1; trial <= options.trials; ++trial) {
    PlacementResult producer_placement;
    PlacementResult consumer_placement;
    auto result = run_throughput_trial<Operation, Queue, Bytes, BatchSize>(
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
      return run_spsc<Operation, Ring<Payload<Bytes>, 64>, Bytes, 1>(options);
    case 4:
      return run_spsc<Operation, Ring<Payload<Bytes>, 64>, Bytes, 4>(options);
    case 16:
      return run_spsc<Operation, Ring<Payload<Bytes>, 64>, Bytes, 16>(options);
    default:
      throw std::logic_error("validated batch size was not dispatched");
    }
  case 1'024:
    switch (options.batch_size) {
    case 1:
      return run_spsc<Operation, Ring<Payload<Bytes>, 1'024>, Bytes, 1>(options);
    case 4:
      return run_spsc<Operation, Ring<Payload<Bytes>, 1'024>, Bytes, 4>(options);
    case 16:
      return run_spsc<Operation, Ring<Payload<Bytes>, 1'024>, Bytes, 16>(options);
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

template <std::size_t Bytes> RunResults dispatch_record_capacity(const Options& options) {
  switch (options.capacity_slots) {
  case 64:
    return run_spsc<GroupOperation::fixed_record, record::FixedRecordRing<Bytes, 64>, Bytes, 1>(
        options);
  case 1'024:
    return run_spsc<GroupOperation::fixed_record, record::FixedRecordRing<Bytes, 1'024>, Bytes, 1>(
        options);
  default:
    throw std::logic_error("validated capacity was not dispatched");
  }
}

RunResults dispatch_record_payload(const Options& options) {
  switch (options.payload_bytes) {
  case 8:
    return dispatch_record_capacity<8>(options);
  case 64:
    return dispatch_record_capacity<64>(options);
  case 256:
    return dispatch_record_capacity<256>(options);
  default:
    throw std::logic_error("validated payload size was not dispatched");
  }
}

template <std::size_t Bytes> RunResults dispatch_byte_capacity(const Options& options) {
  const auto capacity = options.capacity_bytes;
  if (!capacity) {
    throw std::logic_error("byte-record options require byte capacity");
  }
  switch (*capacity) {
  case 4'096:
    return run_spsc<GroupOperation::byte_record, record::VariableRecordRing<4'096>, Bytes, 1>(
        options);
  case 65'536:
    return run_spsc<GroupOperation::byte_record, record::VariableRecordRing<65'536>, Bytes, 1>(
        options);
  default:
    throw std::logic_error("validated byte capacity was not dispatched");
  }
}

RunResults dispatch_byte_payload(const Options& options) {
  switch (options.payload_bytes) {
  case 8:
    return dispatch_byte_capacity<8>(options);
  case 64:
    return dispatch_byte_capacity<64>(options);
  case 256:
    return dispatch_byte_capacity<256>(options);
  default:
    throw std::logic_error("validated payload size was not dispatched");
  }
}

template <std::size_t Bytes> RunResults dispatch_descriptor_capacity(const Options& options) {
  const auto capacity = options.capacity_bytes;
  if (!capacity) {
    throw std::logic_error("descriptor-record options require byte capacity");
  }
  if (options.capacity_slots == 64 && *capacity == 4'096) {
    return run_spsc<GroupOperation::descriptor_record, descriptor::DescriptorPayloadRing<64, 4'096>,
                    Bytes, 1>(options);
  }
  if (options.capacity_slots == 1'024 && *capacity == 65'536) {
    return run_spsc<GroupOperation::descriptor_record,
                    descriptor::DescriptorPayloadRing<1'024, 65'536>, Bytes, 1>(options);
  }
  throw std::logic_error("validated descriptor capacities were not dispatched");
}

RunResults dispatch_descriptor_payload(const Options& options) {
  switch (options.payload_bytes) {
  case 8:
    return dispatch_descriptor_capacity<8>(options);
  case 64:
    return dispatch_descriptor_capacity<64>(options);
  case 256:
    return dispatch_descriptor_capacity<256>(options);
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
  case Implementation::byte_record:
    return dispatch_byte_payload(options);
  case Implementation::cache_line:
    return dispatch_payload<GroupOperation::scalar, spsc::CacheLineBoundedRing>(options);
  case Implementation::cached_index:
    return dispatch_payload<GroupOperation::scalar, spsc::CachedIndexBoundedRing>(options);
  case Implementation::descriptor_record:
    return dispatch_descriptor_payload(options);
  case Implementation::fan_out:
    return run_fan_out_throughput(options);
  case Implementation::fixed_record:
    return dispatch_record_payload(options);
  case Implementation::pipeline:
    return run_pipeline_throughput(options);
  case Implementation::sequence:
    return dispatch_payload<GroupOperation::sequence, BenchmarkSequenceRing>(options);
  case Implementation::sequence_payload:
    throw std::logic_error("sequence-payload does not support throughput");
  case Implementation::staged:
    return dispatch_payload<GroupOperation::staged, spsc::StagedBoundedRing>(options);
  }
  throw std::logic_error("unknown implementation");
}

} // namespace handoff::bench
