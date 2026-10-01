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

namespace handoff::record {

template <std::size_t ByteCapacity> class VariableRecordRing {
  static constexpr std::size_t header_size = sizeof(RecordHeader);

  static_assert(ByteCapacity >= 2 * header_size,
                "a variable-record ring needs room for a record and padding header");
  static_assert(std::has_single_bit(ByteCapacity), "byte capacity must be a power of two");
  static_assert(ByteCapacity <= std::numeric_limits<std::uint32_t>::max(),
                "byte capacity must fit in the padding length field");
  static_assert(ByteCapacity <= std::numeric_limits<std::size_t>::max() / 2,
                "byte capacity must fit unambiguously in the counter distance");

public:
  enum class PushResult { success, full, invalid_record };
  enum class PopResult { success, empty, output_too_small };

  static constexpr std::size_t record_alignment = header_size;
  static constexpr std::uint32_t padding_type_tag = std::numeric_limits<std::uint32_t>::max();

  [[nodiscard]] static constexpr std::size_t byte_capacity() noexcept { return ByteCapacity; }

  [[nodiscard]] static constexpr std::size_t maximum_payload_size() noexcept {
    return ByteCapacity / 2 - header_size;
  }

  [[nodiscard]] static constexpr std::optional<std::size_t>
  record_footprint(std::size_t payload_length) noexcept {
    if (payload_length > maximum_payload_size()) {
      return std::nullopt;
    }
    return footprint_for_valid_payload(payload_length);
  }

  VariableRecordRing() = default;
  VariableRecordRing(const VariableRecordRing&) = delete;
  VariableRecordRing& operator=(const VariableRecordRing&) = delete;
  VariableRecordRing(VariableRecordRing&&) = delete;
  VariableRecordRing& operator=(VariableRecordRing&&) = delete;

  [[nodiscard]] PushResult try_push(const RecordHeader& header,
                                    std::span<const std::byte> payload) noexcept {
    if (header.type_tag == padding_type_tag || header.payload_length != payload.size() ||
        payload.size() > maximum_payload_size()) {
      return PushResult::invalid_record;
    }

    const auto footprint = footprint_for_valid_payload(payload.size());
    const auto tail = tail_.load(std::memory_order_relaxed);
    const auto offset = tail & (ByteCapacity - 1);
    const auto suffix = ByteCapacity - offset;
    const auto padding = footprint > suffix ? suffix : 0;
    const auto required = padding + footprint;
    const auto head = head_.load(std::memory_order_acquire);
    if (required > ByteCapacity - (tail - head)) {
      return PushResult::full;
    }

    if (padding != 0) {
      const RecordHeader marker{.sequence = 0,
                                .type_tag = padding_type_tag,
                                .payload_length = static_cast<std::uint32_t>(padding)};
      write_header(offset, marker);
    }

    const auto record_offset = (offset + padding) & (ByteCapacity - 1);
    write_header(record_offset, header);
    if (!payload.empty()) {
      std::memcpy(storage_.data() + record_offset + header_size, payload.data(), payload.size());
    }
    tail_.store(tail + required, std::memory_order_release);
    return PushResult::success;
  }

  [[nodiscard]] PopResult try_pop(RecordHeader& header, std::span<std::byte> payload) noexcept {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_acquire);
    if (tail == head) {
      return PopResult::empty;
    }

    auto record_position = head;
    auto record_offset = record_position & (ByteCapacity - 1);
    auto stored_header = read_header(record_offset);
    if (stored_header.type_tag == padding_type_tag) {
      record_position += stored_header.payload_length;
      record_offset = 0;
      stored_header = read_header(record_offset);
    }

    if (payload.size() < stored_header.payload_length) {
      return PopResult::output_too_small;
    }

    if (stored_header.payload_length != 0) {
      std::memcpy(payload.data(), storage_.data() + record_offset + header_size,
                  stored_header.payload_length);
    }
    header = stored_header;
    const auto released =
        record_position - head + footprint_for_valid_payload(stored_header.payload_length);
    head_.store(head + released, std::memory_order_release);
    return PopResult::success;
  }

private:
  friend struct CounterTestAccess;

  [[nodiscard]] static constexpr std::size_t
  footprint_for_valid_payload(std::size_t payload_length) noexcept {
    return (header_size + payload_length + record_alignment - 1) & ~(record_alignment - 1);
  }

  void write_header(std::size_t offset, const RecordHeader& header) noexcept {
    std::memcpy(storage_.data() + offset, &header, header_size);
  }

  [[nodiscard]] RecordHeader read_header(std::size_t offset) const noexcept {
    RecordHeader header;
    std::memcpy(&header, storage_.data() + offset, header_size);
    return header;
  }

  alignas(record_alignment) std::array<std::byte, ByteCapacity> storage_{};
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

} // namespace handoff::record
