#pragma once

#include "handoff/record/record_header.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <utility>

namespace handoff::mpsc {

template <std::size_t DescriptorCapacity, std::size_t PayloadByteCapacity,
          std::unsigned_integral Sequence = std::uint64_t>
  requires(!std::same_as<Sequence, bool>)
class VariableRecordRing {
  static_assert(DescriptorCapacity > 0);
  static_assert(DescriptorCapacity <=
                static_cast<std::size_t>(std::numeric_limits<Sequence>::max()));
  static_assert(PayloadByteCapacity >= 32);
  static_assert(std::has_single_bit(PayloadByteCapacity));
  static_assert(PayloadByteCapacity <= std::numeric_limits<std::uint32_t>::max());
  static_assert(PayloadByteCapacity <= std::numeric_limits<std::size_t>::max() / 2);

  struct Descriptor {
    record::RecordHeader header{};
    std::uint32_t offset{};
    std::uint32_t reserved_bytes{};
  };

public:
  class ProducerClaim;
  class ConsumerObservation;
  using sequence_type = Sequence;

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
  [[nodiscard]] static constexpr sequence_type position_limit() noexcept {
    return std::numeric_limits<sequence_type>::max();
  }
  [[nodiscard]] static constexpr std::optional<std::size_t>
  payload_footprint(std::size_t length) noexcept {
    if (length > maximum_payload_size()) {
      return std::nullopt;
    }
    return length == 0 ? 0 : (length + payload_alignment - 1) & ~(payload_alignment - 1);
  }

  VariableRecordRing() = default;
  VariableRecordRing(const VariableRecordRing&) = delete;
  VariableRecordRing& operator=(const VariableRecordRing&) = delete;

  [[nodiscard]] std::optional<ProducerClaim> try_claim(std::size_t length) {
    const auto footprint = payload_footprint(length);
    if (!footprint) {
      return std::nullopt;
    }

    // Descriptor order and byte-range order must be assigned by the same reservation step.
    const std::scoped_lock lock(reservation_mutex_);
    if (next_claim_ == position_limit()) {
      return std::nullopt;
    }
    const auto released_records = released_records_.load(std::memory_order_acquire);
    if (static_cast<std::size_t>(next_claim_ - released_records) >= DescriptorCapacity) {
      return std::nullopt;
    }

    const auto offset = next_byte_claim_ & (PayloadByteCapacity - 1);
    const auto suffix = PayloadByteCapacity - offset;
    const auto gap = *footprint > suffix ? suffix : 0;
    const auto required = gap + *footprint;
    if (next_byte_claim_ > std::numeric_limits<std::uint64_t>::max() - required) {
      return std::nullopt;
    }
    const auto released_bytes = released_bytes_.load(std::memory_order_acquire);
    const auto occupied = next_byte_claim_ - released_bytes;
    if (occupied > PayloadByteCapacity || required > PayloadByteCapacity - occupied) {
      return std::nullopt;
    }

    const auto position = next_claim_;
    const auto payload_offset = (offset + gap) & (PayloadByteCapacity - 1);
    descriptors_[static_cast<std::size_t>(position) % DescriptorCapacity] = {
        .header = {},
        .offset = static_cast<std::uint32_t>(payload_offset),
        .reserved_bytes = static_cast<std::uint32_t>(required)};
    next_byte_claim_ += required;
    next_claim_ = static_cast<sequence_type>(position + 1);
    return ProducerClaim(*this, position, payload_offset, length);
  }

  [[nodiscard]] std::optional<ConsumerObservation> try_observe() noexcept {
    if (consumer_observing_ || next_to_observe_ == position_limit()) {
      return std::nullopt;
    }
    const auto position = next_to_observe_;
    if (ready_[static_cast<std::size_t>(position) % DescriptorCapacity].load(
            std::memory_order_acquire) != static_cast<sequence_type>(position + 1)) {
      return std::nullopt;
    }
    consumer_observing_ = true;
    return ConsumerObservation(*this, position);
  }

  class ProducerClaim {
  public:
    ProducerClaim(const ProducerClaim&) = delete;
    ProducerClaim& operator=(const ProducerClaim&) = delete;
    ProducerClaim(ProducerClaim&& other) noexcept
        : ring_(std::exchange(other.ring_, nullptr)), position_(other.position_),
          offset_(other.offset_), length_(other.length_) {}
    ProducerClaim& operator=(ProducerClaim&& other) noexcept {
      if (this != &other) {
        if (ring_ != nullptr) {
          std::terminate();
        }
        ring_ = std::exchange(other.ring_, nullptr);
        position_ = other.position_;
        offset_ = other.offset_;
        length_ = other.length_;
      }
      return *this;
    }
    ~ProducerClaim() {
      if (ring_ != nullptr) {
        std::terminate();
      }
    }

    [[nodiscard]] sequence_type position() const noexcept { return position_; }
    [[nodiscard]] std::span<std::byte> payload() noexcept {
      assert(ring_ != nullptr);
      return {ring_->payload_.data() + offset_, length_};
    }
    void publish(std::uint32_t type_tag) noexcept {
      assert(ring_ != nullptr);
      const auto index = static_cast<std::size_t>(position_) % DescriptorCapacity;
      ring_->descriptors_[index].header = {.sequence = static_cast<std::uint64_t>(position_),
                                           .type_tag = type_tag,
                                           .payload_length = static_cast<std::uint32_t>(length_)};
      ring_->ready_[index].store(static_cast<sequence_type>(position_ + 1),
                                 std::memory_order_release);
      ring_ = nullptr;
    }

  private:
    friend class VariableRecordRing;
    ProducerClaim(VariableRecordRing& ring, sequence_type position, std::size_t offset,
                  std::size_t length) noexcept
        : ring_(&ring), position_(position), offset_(offset), length_(length) {}
    VariableRecordRing* ring_;
    sequence_type position_;
    std::size_t offset_;
    std::size_t length_;
  };

  class ConsumerObservation {
  public:
    ConsumerObservation(const ConsumerObservation&) = delete;
    ConsumerObservation& operator=(const ConsumerObservation&) = delete;
    ConsumerObservation(ConsumerObservation&& other) noexcept
        : ring_(std::exchange(other.ring_, nullptr)), position_(other.position_) {}
    ConsumerObservation& operator=(ConsumerObservation&& other) noexcept {
      if (this != &other) {
        cancel();
        ring_ = std::exchange(other.ring_, nullptr);
        position_ = other.position_;
      }
      return *this;
    }
    ~ConsumerObservation() { cancel(); }

    [[nodiscard]] sequence_type position() const noexcept { return position_; }
    [[nodiscard]] const record::RecordHeader& header() const noexcept {
      assert(ring_ != nullptr);
      return descriptor().header;
    }
    [[nodiscard]] std::span<const std::byte> payload() const noexcept {
      assert(ring_ != nullptr);
      const auto& current = descriptor();
      return {ring_->payload_.data() + current.offset, current.header.payload_length};
    }
    void release() noexcept {
      assert(ring_ != nullptr);
      ring_->next_byte_release_ += descriptor().reserved_bytes;
      ring_->released_bytes_.store(ring_->next_byte_release_, std::memory_order_release);
      ring_->next_to_observe_ = static_cast<sequence_type>(position_ + 1);
      ring_->released_records_.store(ring_->next_to_observe_, std::memory_order_release);
      ring_->consumer_observing_ = false;
      ring_ = nullptr;
    }
    void cancel() noexcept {
      if (ring_ != nullptr) {
        ring_->consumer_observing_ = false;
        ring_ = nullptr;
      }
    }

  private:
    friend class VariableRecordRing;
    ConsumerObservation(VariableRecordRing& ring, sequence_type position) noexcept
        : ring_(&ring), position_(position) {}
    [[nodiscard]] const Descriptor& descriptor() const noexcept {
      return ring_->descriptors_[static_cast<std::size_t>(position_) % DescriptorCapacity];
    }
    VariableRecordRing* ring_;
    sequence_type position_;
  };

private:
  friend struct VariableRecordTestAccess;

  alignas(payload_alignment) std::array<std::byte, PayloadByteCapacity> payload_{};
  std::uint64_t next_byte_claim_{0};
  std::atomic<std::uint64_t> released_bytes_{0};
  std::uint64_t next_byte_release_{0};
  std::mutex reservation_mutex_;
  std::array<Descriptor, DescriptorCapacity> descriptors_{};
  sequence_type next_claim_{0};
  std::atomic<sequence_type> released_records_{0};
  sequence_type next_to_observe_{0};
  bool consumer_observing_{false};
  std::array<std::atomic<sequence_type>, DescriptorCapacity> ready_{};
};

} // namespace handoff::mpsc
