#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace handoff::record {

struct FixedRecordHeader {
  std::uint64_t sequence{};
  std::uint32_t type_tag{};
  std::uint32_t payload_length{};
};

static_assert(std::is_standard_layout_v<FixedRecordHeader>);
static_assert(sizeof(FixedRecordHeader) == 16);

template <std::size_t PayloadCapacity> struct FixedRecord {
  static_assert(PayloadCapacity > 0, "a fixed record needs payload storage");
  static_assert(PayloadCapacity <= std::numeric_limits<std::uint32_t>::max(),
                "payload capacity must fit in the header length field");

  [[nodiscard]] static constexpr std::size_t payload_capacity() noexcept { return PayloadCapacity; }

  [[nodiscard]] constexpr bool has_valid_payload_length() const noexcept {
    return header.payload_length <= PayloadCapacity;
  }

  FixedRecordHeader header{};
  std::array<std::byte, PayloadCapacity> payload{};
};

template <std::size_t PayloadCapacity, std::size_t Capacity> class FixedRecordRing {
  static_assert(Capacity > 0, "a fixed-record SPSC ring needs at least one slot");
  static_assert(Capacity <= std::numeric_limits<std::size_t>::max() / 2,
                "capacity must fit unambiguously in the counter distance");

public:
  using value_type = FixedRecord<PayloadCapacity>;

  static constexpr std::size_t capacity() noexcept { return Capacity; }
  static constexpr std::size_t payload_capacity() noexcept { return PayloadCapacity; }

  FixedRecordRing() = default;
  FixedRecordRing(const FixedRecordRing&) = delete;
  FixedRecordRing& operator=(const FixedRecordRing&) = delete;
  FixedRecordRing(FixedRecordRing&&) = delete;
  FixedRecordRing& operator=(FixedRecordRing&&) = delete;

  [[nodiscard]] bool try_push(const value_type& record) noexcept {
    if (!record.has_valid_payload_length()) {
      return false;
    }

    const auto tail = tail_.load(std::memory_order_relaxed);
    const auto head = head_.load(std::memory_order_acquire);
    if (tail - head == Capacity) {
      return false;
    }

    slots_[tail % Capacity] = record;
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool try_pop(value_type& record) noexcept {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_acquire);
    if (tail == head) {
      return false;
    }

    record = slots_[head % Capacity];
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

private:
  std::array<value_type, Capacity> slots_{};
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

} // namespace handoff::record
