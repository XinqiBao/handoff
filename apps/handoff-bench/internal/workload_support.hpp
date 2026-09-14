#pragma once

#include "types.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace handoff::bench {

using Clock = std::chrono::steady_clock;

struct TrialControl {
  std::atomic<unsigned int> ready{0};
  std::atomic<unsigned int> warmed{0};
  std::atomic<bool> begin_warmup{false};
  std::atomic<bool> begin_timed{false};
  std::atomic<bool> cancel{false};
  std::atomic<bool> done{false};
  std::atomic<bool> valid{true};
};

template <std::size_t Bytes> struct Payload {
  static_assert(Bytes >= sizeof(std::uint64_t));
  std::array<std::byte, Bytes> bytes{};
};

template <std::size_t Bytes> Payload<Bytes> make_payload(std::uint64_t sequence) {
  static_assert(sizeof(Payload<Bytes>) == Bytes);
  Payload<Bytes> payload;
  for (std::size_t index = 0; index < sizeof(sequence); ++index) {
    payload.bytes[index] = static_cast<std::byte>((sequence >> (index * 8U)) & 0xffU);
  }
  for (std::size_t index = sizeof(sequence); index < payload.bytes.size(); ++index) {
    payload.bytes[index] = static_cast<std::byte>((sequence + index * 17U) & 0xffU);
  }
  return payload;
}

template <std::size_t Bytes>
bool observe_payload_bytes(const std::array<std::byte, Bytes>& bytes,
                           std::uint64_t expected_sequence, std::uint64_t& checksum) {
  std::uint64_t observed_sequence = 0;
  for (std::size_t index = 0; index < sizeof(observed_sequence); ++index) {
    observed_sequence |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[index]))
                         << (index * 8U);
  }

  bool valid = observed_sequence == expected_sequence;
  checksum ^= observed_sequence + 0x9e3779b97f4a7c15ULL + (checksum << 6U) + (checksum >> 2U);
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const auto expected = index < sizeof(expected_sequence)
                              ? static_cast<std::byte>((expected_sequence >> (index * 8U)) & 0xffU)
                              : static_cast<std::byte>((expected_sequence + index * 17U) & 0xffU);
    valid = valid && bytes[index] == expected;
    checksum += std::to_integer<std::uint8_t>(bytes[index]);
  }
  return valid;
}

template <std::size_t Bytes>
bool observe_payload(const Payload<Bytes>& payload, std::uint64_t expected_sequence,
                     std::uint64_t& checksum) {
  return observe_payload_bytes(payload.bytes, expected_sequence, checksum);
}

template <std::size_t Bytes> std::uint64_t expected_checksum(std::uint64_t iterations) {
  std::uint64_t checksum = 0;
  for (std::uint64_t sequence = 0; sequence < iterations; ++sequence) {
    const auto payload = make_payload<Bytes>(sequence);
    static_cast<void>(observe_payload(payload, sequence, checksum));
  }
  return checksum;
}

inline PlacementResult apply_affinity(std::optional<unsigned int> cpu) {
  if (!cpu) {
    return {.requested = std::nullopt,
            .effective = std::nullopt,
            .outcome = {.status = platform::AffinityStatus::unsupported,
                        .effective_cpu = std::nullopt,
                        .message = "not requested"}};
  }
  auto outcome = platform::pin_current_thread(*cpu);
  const auto effective = outcome.effective_cpu;
  return {.requested = cpu, .effective = effective, .outcome = std::move(outcome)};
}

inline bool affinity_failed(const PlacementResult& placement) {
  if (!placement.requested) {
    return false;
  }
  return placement.outcome.status == platform::AffinityStatus::invalid_cpu ||
         placement.outcome.status == platform::AffinityStatus::system_error;
}

inline void wait_for_count(const std::atomic<unsigned int>& count, unsigned int expected) {
  auto observed = count.load(std::memory_order_acquire);
  while (observed != expected) {
    count.wait(observed, std::memory_order_acquire);
    observed = count.load(std::memory_order_acquire);
  }
}

inline bool wait_for_phase(const std::atomic<bool>& phase, const std::atomic<bool>& cancel) {
  while (!phase.load(std::memory_order_acquire)) {
    if (cancel.load(std::memory_order_acquire)) {
      return false;
    }
    phase.wait(false, std::memory_order_acquire);
  }
  return !cancel.load(std::memory_order_acquire);
}

inline void signal_count(std::atomic<unsigned int>& count) {
  // Chain participant releases so the waiter observes every preceding non-atomic result.
  count.fetch_add(1, std::memory_order_acq_rel);
  count.notify_one();
}

inline void release_phase(std::atomic<bool>& phase) {
  phase.store(true, std::memory_order_release);
  phase.notify_all();
}

inline void signal_done(std::atomic<bool>& done) {
  done.store(true, std::memory_order_release);
  done.notify_one();
}

inline void wait_for_done(const std::atomic<bool>& done) {
  done.wait(false, std::memory_order_acquire);
}

inline void cancel_trial(TrialControl& control) {
  control.cancel.store(true, std::memory_order_release);
  release_phase(control.begin_warmup);
  release_phase(control.begin_timed);
}

inline void validate_affinity_or_cancel(TrialControl& control, std::thread& producer,
                                        std::thread& consumer,
                                        const PlacementResult& producer_placement,
                                        const PlacementResult& consumer_placement) {
  const bool producer_failed = affinity_failed(producer_placement);
  const bool consumer_failed = affinity_failed(consumer_placement);
  if (!producer_failed && !consumer_failed) {
    return;
  }

  cancel_trial(control);
  producer.join();
  consumer.join();
  const auto& failed = producer_failed ? producer_placement : consumer_placement;
  const std::string role = producer_failed ? "producer" : "consumer";
  throw std::runtime_error(role + " affinity failed: " + failed.outcome.message);
}

template <std::size_t ConsumerCount>
void validate_affinity_or_cancel(
    TrialControl& control, std::thread& producer, std::array<std::thread, ConsumerCount>& consumers,
    const PlacementResult& producer_placement,
    const std::array<PlacementResult, ConsumerCount>& consumer_placements,
    const std::array<std::string_view, ConsumerCount>& consumer_roles) {
  const PlacementResult* failed = nullptr;
  std::string_view role = "producer";
  if (affinity_failed(producer_placement)) {
    failed = &producer_placement;
  } else {
    for (std::size_t index = 0; index < ConsumerCount; ++index) {
      if (affinity_failed(consumer_placements[index])) {
        failed = &consumer_placements[index];
        role = consumer_roles[index];
        break;
      }
    }
  }
  if (failed == nullptr) {
    return;
  }

  cancel_trial(control);
  producer.join();
  for (auto& consumer : consumers) {
    consumer.join();
  }
  throw std::runtime_error(std::string(role) + " affinity failed: " + failed->outcome.message);
}

} // namespace handoff::bench
