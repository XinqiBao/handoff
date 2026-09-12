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
#include <thread>

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
bool observe_payload(const Payload<Bytes>& payload, std::uint64_t expected_sequence,
                     std::uint64_t& checksum) {
  std::uint64_t observed_sequence = 0;
  for (std::size_t index = 0; index < sizeof(observed_sequence); ++index) {
    observed_sequence |=
        static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(payload.bytes[index]))
        << (index * 8U);
  }

  bool valid = observed_sequence == expected_sequence;
  checksum ^= observed_sequence + 0x9e3779b97f4a7c15ULL + (checksum << 6U) + (checksum >> 2U);
  for (std::size_t index = 0; index < payload.bytes.size(); ++index) {
    const auto expected = index < sizeof(expected_sequence)
                              ? static_cast<std::byte>((expected_sequence >> (index * 8U)) & 0xffU)
                              : static_cast<std::byte>((expected_sequence + index * 17U) & 0xffU);
    valid = valid && payload.bytes[index] == expected;
    checksum += std::to_integer<std::uint8_t>(payload.bytes[index]);
  }
  return valid;
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
    return {
        .requested = std::nullopt,
        .outcome = {.status = platform::AffinityStatus::unsupported, .message = "not requested"}};
  }
  return {.requested = cpu, .outcome = platform::pin_current_thread(*cpu)};
}

inline bool affinity_failed(const PlacementResult& placement) {
  if (!placement.requested) {
    return false;
  }
  return placement.outcome.status == platform::AffinityStatus::invalid_cpu ||
         placement.outcome.status == platform::AffinityStatus::system_error;
}

inline void wait_for_count(const std::atomic<unsigned int>& count, unsigned int expected) {
  while (count.load(std::memory_order_acquire) != expected) {
    std::this_thread::yield();
  }
}

inline bool wait_for_phase(const std::atomic<bool>& phase, const std::atomic<bool>& cancel) {
  while (!phase.load(std::memory_order_acquire)) {
    if (cancel.load(std::memory_order_acquire)) {
      return false;
    }
    std::this_thread::yield();
  }
  return true;
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

  control.cancel.store(true, std::memory_order_release);
  producer.join();
  consumer.join();
  const auto& failed = producer_failed ? producer_placement : consumer_placement;
  const std::string role = producer_failed ? "producer" : "consumer";
  throw std::runtime_error(role + " affinity failed: " + failed.outcome.message);
}

} // namespace handoff::bench
