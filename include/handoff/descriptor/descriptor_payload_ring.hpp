#pragma once

#include "handoff/record/record_header.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>

namespace handoff::descriptor {

template <std::size_t DescriptorCapacity, std::size_t PayloadByteCapacity>
class DescriptorPayloadRing {
  static_assert(DescriptorCapacity > 0, "a descriptor ring needs at least one slot");
  static_assert(DescriptorCapacity <= std::numeric_limits<std::size_t>::max() / 2,
                "descriptor capacity must fit unambiguously in the counter distance");
  static_assert(PayloadByteCapacity >= 32,
                "payload storage needs room for an aligned payload from every offset");
  static_assert(std::has_single_bit(PayloadByteCapacity),
                "payload byte capacity must be a power of two");
  static_assert(PayloadByteCapacity <= std::numeric_limits<std::uint32_t>::max(),
                "payload byte capacity must fit in a descriptor offset");
  static_assert(PayloadByteCapacity <= std::numeric_limits<std::size_t>::max() / 2,
                "payload capacity must fit unambiguously in the counter distance");

  struct Descriptor {
    record::RecordHeader header{};
    std::uint32_t payload_offset{};
    std::uint32_t reserved_bytes{};
  };

public:
  enum class PushResult { success, full, invalid_record };
  enum class PopResult { success, empty, output_too_small };

  static constexpr std::size_t payload_alignment = 16;

  [[nodiscard]] static constexpr std::size_t descriptor_capacity() noexcept {
    return DescriptorCapacity;
  }

  [[nodiscard]] static constexpr std::size_t payload_byte_capacity() noexcept {
    return PayloadByteCapacity;
  }

  [[nodiscard]] static constexpr std::size_t maximum_payload_size() noexcept {
    return PayloadByteCapacity / 2;
  }

  [[nodiscard]] static constexpr std::optional<std::size_t>
  payload_footprint(std::size_t payload_length) noexcept {
    if (payload_length > maximum_payload_size()) {
      return std::nullopt;
    }
    return footprint_for_valid_payload(payload_length);
  }

  DescriptorPayloadRing() = default;
  DescriptorPayloadRing(const DescriptorPayloadRing&) = delete;
  DescriptorPayloadRing& operator=(const DescriptorPayloadRing&) = delete;
  DescriptorPayloadRing(DescriptorPayloadRing&&) = delete;
  DescriptorPayloadRing& operator=(DescriptorPayloadRing&&) = delete;

  [[nodiscard]] PushResult try_push(const record::RecordHeader& header,
                                    std::span<const std::byte> payload) noexcept {
    if (header.payload_length != payload.size() || payload.size() > maximum_payload_size()) {
      return PushResult::invalid_record;
    }

    const auto descriptor_tail = descriptor_tail_.load(std::memory_order_relaxed);
    const auto descriptor_head = descriptor_head_.load(std::memory_order_acquire);
    if (descriptor_tail - descriptor_head == DescriptorCapacity) {
      return PushResult::full;
    }

    const auto footprint = footprint_for_valid_payload(payload.size());
    const auto payload_offset = payload_tail_ & (PayloadByteCapacity - 1);
    const auto suffix = PayloadByteCapacity - payload_offset;
    const auto gap = footprint > suffix ? suffix : 0;
    const auto required = gap + footprint;
    const auto payload_head = payload_head_.load(std::memory_order_acquire);
    if (required > PayloadByteCapacity - (payload_tail_ - payload_head)) {
      return PushResult::full;
    }

    const auto stored_offset = (payload_offset + gap) & (PayloadByteCapacity - 1);
    if (!payload.empty()) {
      std::memcpy(payload_storage_.data() + stored_offset, payload.data(), payload.size());
    }
    descriptors_[descriptor_tail % DescriptorCapacity] = {
        .header = header,
        .payload_offset = static_cast<std::uint32_t>(stored_offset),
        .reserved_bytes = static_cast<std::uint32_t>(required)};
    payload_tail_ += required;
    descriptor_tail_.store(descriptor_tail + 1, std::memory_order_release);
    return PushResult::success;
  }

  [[nodiscard]] PopResult try_pop(record::RecordHeader& header,
                                  std::span<std::byte> payload) noexcept {
    const auto descriptor_head = descriptor_head_.load(std::memory_order_relaxed);
    const auto descriptor_tail = descriptor_tail_.load(std::memory_order_acquire);
    if (descriptor_tail == descriptor_head) {
      return PopResult::empty;
    }

    const auto descriptor = descriptors_[descriptor_head % DescriptorCapacity];
    if (payload.size() < descriptor.header.payload_length) {
      return PopResult::output_too_small;
    }

    if (descriptor.header.payload_length != 0) {
      std::memcpy(payload.data(), payload_storage_.data() + descriptor.payload_offset,
                  descriptor.header.payload_length);
    }
    header = descriptor.header;
    const auto payload_head = payload_head_.load(std::memory_order_relaxed);
    payload_head_.store(payload_head + descriptor.reserved_bytes, std::memory_order_release);
    descriptor_head_.store(descriptor_head + 1, std::memory_order_release);
    return PopResult::success;
  }

private:
  [[nodiscard]] static constexpr std::size_t
  footprint_for_valid_payload(std::size_t payload_length) noexcept {
    if (payload_length == 0) {
      return 0;
    }
    return (payload_length + payload_alignment - 1) & ~(payload_alignment - 1);
  }

  std::array<Descriptor, DescriptorCapacity> descriptors_{};
  alignas(payload_alignment) std::array<std::byte, PayloadByteCapacity> payload_storage_{};
  std::atomic<std::size_t> descriptor_head_{0};
  std::atomic<std::size_t> descriptor_tail_{0};
  std::atomic<std::size_t> payload_head_{0};
  std::size_t payload_tail_{0};
};

} // namespace handoff::descriptor
