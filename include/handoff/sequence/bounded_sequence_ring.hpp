#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace handoff::sequence {

template <typename T, std::size_t Capacity, std::unsigned_integral Sequence = std::uint64_t>
  requires std::default_initializable<T> && (!std::same_as<Sequence, bool>)
class BoundedSequenceRing {
  static_assert(Capacity > 0, "a sequence ring needs at least one slot");
  static_assert(Capacity <= static_cast<std::size_t>(std::numeric_limits<Sequence>::max()),
                "capacity must fit in the sequence range");

public:
  class ProducerClaim;
  class ConsumerObservation;

  using value_type = T;
  using sequence_type = Sequence;

  static constexpr std::size_t capacity() noexcept { return Capacity; }
  static constexpr sequence_type first_sequence() noexcept { return sequence_type{1}; }
  static constexpr sequence_type sequence_limit() noexcept {
    return std::numeric_limits<sequence_type>::max();
  }

  BoundedSequenceRing() = default;
  BoundedSequenceRing(const BoundedSequenceRing&) = delete;
  BoundedSequenceRing& operator=(const BoundedSequenceRing&) = delete;
  BoundedSequenceRing(BoundedSequenceRing&&) = delete;
  BoundedSequenceRing& operator=(BoundedSequenceRing&&) = delete;

  [[nodiscard]] sequence_type published_sequence() const noexcept {
    return producer_cursor_.load(std::memory_order_acquire);
  }

  [[nodiscard]] sequence_type gating_sequence() const noexcept {
    return consumer_gating_sequence_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::optional<ProducerClaim> try_claim() {
    if (producer_claimed_ || producer_exhausted_) {
      return std::nullopt;
    }

    const auto gating = consumer_gating_sequence_.load(std::memory_order_acquire);
    const auto occupied = static_cast<sequence_type>((next_to_claim_ - sequence_type{1}) - gating);
    if (occupied >= static_cast<sequence_type>(Capacity)) {
      return std::nullopt;
    }

    producer_claimed_ = true;
    return ProducerClaim(*this, next_to_claim_);
  }

  [[nodiscard]] std::optional<ConsumerObservation> try_observe() {
    if (consumer_observing_ || consumer_exhausted_) {
      return std::nullopt;
    }

    const auto published = producer_cursor_.load(std::memory_order_acquire);
    if (published < next_to_observe_) {
      return std::nullopt;
    }

    consumer_observing_ = true;
    return ConsumerObservation(*this, next_to_observe_);
  }

  class ProducerClaim {
  public:
    ProducerClaim(const ProducerClaim&) = delete;
    ProducerClaim& operator=(const ProducerClaim&) = delete;

    ProducerClaim(ProducerClaim&& other) noexcept
        : ring_(std::exchange(other.ring_, nullptr)), sequence_(other.sequence_) {}

    ProducerClaim& operator=(ProducerClaim&& other) noexcept {
      if (this != &other) {
        cancel();
        ring_ = std::exchange(other.ring_, nullptr);
        sequence_ = other.sequence_;
      }
      return *this;
    }

    ~ProducerClaim() { cancel(); }

    [[nodiscard]] bool active() const noexcept { return ring_ != nullptr; }
    [[nodiscard]] sequence_type sequence() const noexcept { return sequence_; }
    [[nodiscard]] T& value() noexcept { return ring_->slot(sequence_); }

    void publish() noexcept {
      if (ring_ == nullptr) {
        return;
      }
      ring_->publish(sequence_);
      ring_ = nullptr;
    }

    void cancel() noexcept {
      if (ring_ == nullptr) {
        return;
      }
      ring_->cancel_claim();
      ring_ = nullptr;
    }

  private:
    friend class BoundedSequenceRing;

    ProducerClaim(BoundedSequenceRing& ring, sequence_type sequence) noexcept
        : ring_(&ring), sequence_(sequence) {}

    BoundedSequenceRing* ring_;
    sequence_type sequence_;
  };

  class ConsumerObservation {
  public:
    ConsumerObservation(const ConsumerObservation&) = delete;
    ConsumerObservation& operator=(const ConsumerObservation&) = delete;

    ConsumerObservation(ConsumerObservation&& other) noexcept
        : ring_(std::exchange(other.ring_, nullptr)), sequence_(other.sequence_) {}

    ConsumerObservation& operator=(ConsumerObservation&& other) noexcept {
      if (this != &other) {
        cancel();
        ring_ = std::exchange(other.ring_, nullptr);
        sequence_ = other.sequence_;
      }
      return *this;
    }

    ~ConsumerObservation() { cancel(); }

    [[nodiscard]] bool active() const noexcept { return ring_ != nullptr; }
    [[nodiscard]] sequence_type sequence() const noexcept { return sequence_; }
    [[nodiscard]] const T& value() const noexcept { return ring_->slot(sequence_); }

    void release() noexcept {
      if (ring_ == nullptr) {
        return;
      }
      ring_->release(sequence_);
      ring_ = nullptr;
    }

    void cancel() noexcept {
      if (ring_ == nullptr) {
        return;
      }
      ring_->cancel_observation();
      ring_ = nullptr;
    }

  private:
    friend class BoundedSequenceRing;

    ConsumerObservation(BoundedSequenceRing& ring, sequence_type sequence) noexcept
        : ring_(&ring), sequence_(sequence) {}

    BoundedSequenceRing* ring_;
    sequence_type sequence_;
  };

private:
  [[nodiscard]] T& slot(sequence_type sequence) noexcept {
    const auto index = static_cast<std::size_t>(sequence - sequence_type{1}) % Capacity;
    return slots_[index];
  }

  [[nodiscard]] const T& slot(sequence_type sequence) const noexcept {
    const auto index = static_cast<std::size_t>(sequence - sequence_type{1}) % Capacity;
    return slots_[index];
  }

  void publish(sequence_type sequence) noexcept {
    producer_claimed_ = false;
    producer_cursor_.store(sequence, std::memory_order_release);
    if (sequence == sequence_limit()) {
      producer_exhausted_ = true;
    } else {
      next_to_claim_ = static_cast<sequence_type>(sequence + sequence_type{1});
    }
  }

  void cancel_claim() noexcept { producer_claimed_ = false; }

  void release(sequence_type sequence) noexcept {
    consumer_observing_ = false;
    consumer_gating_sequence_.store(sequence, std::memory_order_release);
    if (sequence == sequence_limit()) {
      consumer_exhausted_ = true;
    } else {
      next_to_observe_ = static_cast<sequence_type>(sequence + sequence_type{1});
    }
  }

  void cancel_observation() noexcept { consumer_observing_ = false; }

  std::array<T, Capacity> slots_{};
  std::atomic<sequence_type> producer_cursor_{0};
  std::atomic<sequence_type> consumer_gating_sequence_{0};
  sequence_type next_to_claim_{first_sequence()};
  sequence_type next_to_observe_{first_sequence()};
  bool producer_claimed_{false};
  bool consumer_observing_{false};
  bool producer_exhausted_{false};
  bool consumer_exhausted_{false};
};

} // namespace handoff::sequence
