#include "record_support.hpp"
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
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace handoff::bench {
namespace {

enum class QueueOperation { push_pop, byte_record, descriptor_record, fixed_record, sequence };

template <typename T, std::size_t Capacity>
using BenchmarkSequenceRing = sequence::BoundedSequenceRing<T, Capacity>;

struct LatencySummary {
  double median_ns;
  double p95_ns;
  double p99_ns;
};

template <QueueOperation Operation, std::size_t Bytes> auto make_message(std::uint64_t sequence) {
  if constexpr (Operation == QueueOperation::byte_record ||
                Operation == QueueOperation::descriptor_record) {
    return make_record_message<Bytes>(sequence);
  } else if constexpr (Operation == QueueOperation::fixed_record) {
    return make_fixed_record<Bytes>(sequence);
  } else {
    return make_payload<Bytes>(sequence);
  }
}

template <QueueOperation Operation, typename Message>
bool observe_message(const Message& message, std::uint64_t expected_sequence,
                     std::uint64_t& checksum) {
  if constexpr (Operation == QueueOperation::byte_record ||
                Operation == QueueOperation::descriptor_record) {
    return observe_record_message(message, expected_sequence, checksum);
  } else if constexpr (Operation == QueueOperation::fixed_record) {
    return observe_fixed_record(message, expected_sequence, checksum);
  } else {
    return observe_payload(message, expected_sequence, checksum);
  }
}

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

template <QueueOperation Operation, typename Queue, typename Message>
bool try_send(Queue& queue, const Message& value) {
  if constexpr (Operation == QueueOperation::byte_record ||
                Operation == QueueOperation::descriptor_record) {
    using PushResult = typename Queue::PushResult;
    const auto result = queue.try_push(value.header, value.payload.bytes);
    if (result == PushResult::invalid_record) {
      throw std::logic_error("benchmark produced an invalid record message");
    }
    return result == PushResult::success;
  } else if constexpr (Operation == QueueOperation::sequence) {
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

template <QueueOperation Operation, typename Queue, typename Message>
bool try_receive(Queue& queue, Message& value) {
  if constexpr (Operation == QueueOperation::byte_record ||
                Operation == QueueOperation::descriptor_record) {
    using PopResult = typename Queue::PopResult;
    const auto result = queue.try_pop(value.header, value.payload.bytes);
    if (result == PopResult::output_too_small) {
      throw std::logic_error("benchmark record output is too small");
    }
    return result == PopResult::success;
  } else if constexpr (Operation == QueueOperation::sequence) {
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

template <QueueOperation Operation, typename Queue, std::size_t Bytes>
TrialResult run_ping_pong_trial(const Options& options, unsigned int trial,
                                PlacementResult& producer_placement,
                                PlacementResult& consumer_placement, std::ostream* samples_output) {
  Queue requests;
  Queue responses;
  TrialControl control;
  Clock::time_point stop;
  std::uint64_t checksum = 0;
  const auto expected = expected_checksum<Bytes>(options.iterations);
  std::vector<std::int64_t> rtt_samples(static_cast<std::size_t>(options.iterations));

  std::thread producer([&] {
    producer_placement = apply_affinity(options.producer_cpu);
    signal_count(control.ready);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    decltype(make_message<Operation, Bytes>(0)) response;
    std::uint64_t warmup_checksum = 0;
    for (std::uint64_t sequence = 0; sequence < options.warmup; ++sequence) {
      auto request = make_message<Operation, Bytes>(sequence);
      while (!try_send<Operation>(requests, request)) {
      }
      while (!try_receive<Operation>(responses, response)) {
      }
      if (!observe_message<Operation>(response, sequence, warmup_checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
    }
    signal_count(control.warmed);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    for (std::uint64_t sequence = 0; sequence < options.iterations; ++sequence) {
      auto request = make_message<Operation, Bytes>(sequence);
      const auto sample_start = Clock::now();
      while (!try_send<Operation>(requests, request)) {
      }
      while (!try_receive<Operation>(responses, response)) {
      }
      const auto sample_stop = Clock::now();
      rtt_samples[static_cast<std::size_t>(sequence)] =
          std::chrono::duration_cast<std::chrono::nanoseconds>(sample_stop - sample_start).count();
      if (!observe_message<Operation>(response, sequence, checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
    }
    stop = Clock::now();
    signal_done(control.done);
  });

  std::thread consumer([&] {
    consumer_placement = apply_affinity(options.consumer_cpu);
    signal_count(control.ready);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    decltype(make_message<Operation, Bytes>(0)) request;
    std::uint64_t ignored_checksum = 0;
    for (std::uint64_t sequence = 0; sequence < options.warmup; ++sequence) {
      while (!try_receive<Operation>(requests, request)) {
      }
      if (!observe_message<Operation>(request, sequence, ignored_checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
      while (!try_send<Operation>(responses, request)) {
      }
    }
    signal_count(control.warmed);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    for (std::uint64_t sequence = 0; sequence < options.iterations; ++sequence) {
      while (!try_receive<Operation>(requests, request)) {
      }
      if (!observe_message<Operation>(request, sequence, ignored_checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
      while (!try_send<Operation>(responses, request)) {
      }
    }
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
    throw std::runtime_error("ping-pong payload validation failed");
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
  if (samples_output) {
    for (std::size_t index = 0; index < rtt_samples.size(); ++index) {
      *samples_output << trial << ',' << index << ',' << rtt_samples[index] << '\n';
    }
    samples_output->flush();
    if (!*samples_output) {
      throw std::runtime_error("unable to write latency samples");
    }
  }
  const auto latency = summarize_latency(std::move(rtt_samples));
  return {.trial = trial,
          .elapsed_ns = elapsed,
          .messages_per_second = std::nullopt,
          .latency_ns = latency.median_ns,
          .latency_p95_ns = latency.p95_ns,
          .latency_p99_ns = latency.p99_ns,
          .checksum = checksum};
}

template <QueueOperation Operation, typename Queue, std::size_t Bytes>
RunResults run_spsc(const Options& options) {
  std::ofstream samples_output;
  if (options.latency_samples) {
    samples_output.open(*options.latency_samples);
    if (!samples_output) {
      throw std::runtime_error("unable to open latency samples file: " +
                               options.latency_samples->string());
    }
    samples_output << "trial,index,rtt_ns\n";
  }
  RunResults results;
  results.trials.reserve(options.trials);
  for (unsigned int trial = 1; trial <= options.trials; ++trial) {
    PlacementResult producer_placement;
    PlacementResult consumer_placement;
    auto result = run_ping_pong_trial<Operation, Queue, Bytes>(
        options, trial, producer_placement, consumer_placement,
        options.latency_samples ? &samples_output : nullptr);
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
    return run_spsc<Operation, Ring<Payload<Bytes>, 64>, Bytes>(options);
  case 1'024:
    return run_spsc<Operation, Ring<Payload<Bytes>, 1'024>, Bytes>(options);
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

template <std::size_t Bytes> RunResults dispatch_record_capacity(const Options& options) {
  switch (options.capacity_slots) {
  case 64:
    return run_spsc<QueueOperation::fixed_record, record::FixedRecordRing<Bytes, 64>, Bytes>(
        options);
  case 1'024:
    return run_spsc<QueueOperation::fixed_record, record::FixedRecordRing<Bytes, 1'024>, Bytes>(
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
    return run_spsc<QueueOperation::byte_record, record::VariableRecordRing<4'096>, Bytes>(options);
  case 65'536:
    return run_spsc<QueueOperation::byte_record, record::VariableRecordRing<65'536>, Bytes>(
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
    return run_spsc<QueueOperation::descriptor_record, descriptor::DescriptorPayloadRing<64, 4'096>,
                    Bytes>(options);
  }
  if (options.capacity_slots == 1'024 && *capacity == 65'536) {
    return run_spsc<QueueOperation::descriptor_record,
                    descriptor::DescriptorPayloadRing<1'024, 65'536>, Bytes>(options);
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

RunResults run_ping_pong(const Options& options) {
  switch (options.implementation) {
  case Implementation::basic:
    return dispatch_payload<QueueOperation::push_pop, spsc::BasicBoundedRing>(options);
  case Implementation::batch:
    return dispatch_payload<QueueOperation::push_pop, spsc::BatchBoundedRing>(options);
  case Implementation::bulk:
  case Implementation::burst:
    throw std::logic_error("bulk and burst implementations are not ping-pong modes");
  case Implementation::byte_record:
    return dispatch_byte_payload(options);
  case Implementation::cache_line:
    return dispatch_payload<QueueOperation::push_pop, spsc::CacheLineBoundedRing>(options);
  case Implementation::cached_index:
    return dispatch_payload<QueueOperation::push_pop, spsc::CachedIndexBoundedRing>(options);
  case Implementation::descriptor_record:
    return dispatch_descriptor_payload(options);
  case Implementation::fan_out:
    throw std::logic_error("fan-out implementation is not a ping-pong mode");
  case Implementation::fixed_record:
    return dispatch_record_payload(options);
  case Implementation::mpsc_count:
  case Implementation::mpsc_ordered:
  case Implementation::mpsc_serialized:
  case Implementation::mpsc_slot:
    throw std::logic_error("MPSC implementations do not support ping-pong");
  case Implementation::pipeline:
    throw std::logic_error("pipeline implementation is not a ping-pong mode");
  case Implementation::sequence:
    return dispatch_payload<QueueOperation::sequence, BenchmarkSequenceRing>(options);
  case Implementation::sequence_payload:
    throw std::logic_error("sequence-payload implementation is not a ping-pong mode");
  case Implementation::spmc_ordered:
  case Implementation::spmc_serialized:
  case Implementation::spmc_slot:
    throw std::logic_error("SPMC implementations do not support ping-pong");
  case Implementation::staged:
    throw std::logic_error("staged implementation is not a ping-pong mode");
  }
  throw std::logic_error("unknown implementation");
}

} // namespace handoff::bench
