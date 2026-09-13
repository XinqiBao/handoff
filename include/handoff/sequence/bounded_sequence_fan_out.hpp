#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace handoff::sequence {

template <typename T, std::size_t Capacity, std::size_t ConsumerCount,
          std::unsigned_integral Sequence = std::uint64_t>
  requires std::default_initializable<T> && (!std::same_as<Sequence, bool>)
class BoundedSequenceFanOut {
  static_assert(Capacity > 0, "a sequence fan-out needs at least one slot");
  static_assert(ConsumerCount >= 2, "a sequence fan-out needs at least two consumers");
  static_assert(Capacity <= static_cast<std::size_t>(std::numeric_limits<Sequence>::max()),
                "capacity must fit in the sequence range");

  struct ConsumerState {
    std::atomic<Sequence> gating_sequence{0};
    Sequence next_to_observe{1};
    bool observing{false};
    bool exhausted{false};
  };

public:
  class ProducerClaim;
  class ConsumerObservation;

  using value_type = T;
  using sequence_type = Sequence;

  static constexpr std::size_t capacity() noexcept { return Capacity; }
  static constexpr std::size_t consumer_count() noexcept { return ConsumerCount; }
  static constexpr sequence_type first_sequence() noexcept { return sequence_type{1}; }
  static constexpr sequence_type sequence_limit() noexcept {
    return std::numeric_limits<sequence_type>::max();
  }

  BoundedSequenceFanOut() = default;
  BoundedSequenceFanOut(const BoundedSequenceFanOut&) = delete;
  BoundedSequenceFanOut& operator=(const BoundedSequenceFanOut&) = delete;
  BoundedSequenceFanOut(BoundedSequenceFanOut&&) = delete;
  BoundedSequenceFanOut& operator=(BoundedSequenceFanOut&&) = delete;

  [[nodiscard]] sequence_type published_sequence() const noexcept {
    return producer_cursor_.load(std::memory_order_acquire);
  }

  [[nodiscard]] sequence_type gating_sequence(std::size_t consumer_index) const {
    return consumer(consumer_index).gating_sequence.load(std::memory_order_acquire);
  }

  [[nodiscard]] sequence_type minimum_gating_sequence() const noexcept {
    auto minimum = consumers_.front().gating_sequence.load(std::memory_order_acquire);
    for (std::size_t index = 1; index < ConsumerCount; ++index) {
      const auto gating = consumers_[index].gating_sequence.load(std::memory_order_acquire);
      if (gating < minimum) {
        minimum = gating;
      }
    }
    return minimum;
  }

  [[nodiscard]] std::optional<ProducerClaim> try_claim() {
    if (producer_claimed_ || producer_exhausted_) {
      return std::nullopt;
    }

    const auto gating = minimum_gating_sequence();
    const auto occupied = static_cast<sequence_type>((next_to_claim_ - sequence_type{1}) - gating);
    if (occupied >= static_cast<sequence_type>(Capacity)) {
      return std::nullopt;
    }

    producer_claimed_ = true;
    return ProducerClaim(*this, next_to_claim_);
  }

  [[nodiscard]] std::optional<ConsumerObservation> try_observe(std::size_t consumer_index) {
    auto& state = consumer(consumer_index);
    if (state.observing || state.exhausted) {
      return std::nullopt;
    }

    const auto published = producer_cursor_.load(std::memory_order_acquire);
    if (published < state.next_to_observe) {
      return std::nullopt;
    }

    state.observing = true;
    return ConsumerObservation(*this, consumer_index, state.next_to_observe);
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
    friend class BoundedSequenceFanOut;

    ProducerClaim(BoundedSequenceFanOut& ring, sequence_type sequence) noexcept
        : ring_(&ring), sequence_(sequence) {}

    BoundedSequenceFanOut* ring_;
    sequence_type sequence_;
  };

  class ConsumerObservation {
  public:
    ConsumerObservation(const ConsumerObservation&) = delete;
    ConsumerObservation& operator=(const ConsumerObservation&) = delete;

    ConsumerObservation(ConsumerObservation&& other) noexcept
        : ring_(std::exchange(other.ring_, nullptr)), consumer_index_(other.consumer_index_),
          sequence_(other.sequence_) {}

    ConsumerObservation& operator=(ConsumerObservation&& other) noexcept {
      if (this != &other) {
        cancel();
        ring_ = std::exchange(other.ring_, nullptr);
        consumer_index_ = other.consumer_index_;
        sequence_ = other.sequence_;
      }
      return *this;
    }

    ~ConsumerObservation() { cancel(); }

    [[nodiscard]] bool active() const noexcept { return ring_ != nullptr; }
    [[nodiscard]] std::size_t consumer_index() const noexcept { return consumer_index_; }
    [[nodiscard]] sequence_type sequence() const noexcept { return sequence_; }
    [[nodiscard]] const T& value() const noexcept { return ring_->slot(sequence_); }

    void release() noexcept {
      if (ring_ == nullptr) {
        return;
      }
      ring_->release(consumer_index_, sequence_);
      ring_ = nullptr;
    }

    void cancel() noexcept {
      if (ring_ == nullptr) {
        return;
      }
      ring_->cancel_observation(consumer_index_);
      ring_ = nullptr;
    }

  private:
    friend class BoundedSequenceFanOut;

    ConsumerObservation(BoundedSequenceFanOut& ring, std::size_t consumer_index,
                        sequence_type sequence) noexcept
        : ring_(&ring), consumer_index_(consumer_index), sequence_(sequence) {}

    BoundedSequenceFanOut* ring_;
    std::size_t consumer_index_;
    sequence_type sequence_;
  };

private:
  [[nodiscard]] ConsumerState& consumer(std::size_t consumer_index) {
    if (consumer_index >= ConsumerCount) {
      throw std::out_of_range("consumer index is outside the fan-out");
    }
    return consumers_[consumer_index];
  }

  [[nodiscard]] const ConsumerState& consumer(std::size_t consumer_index) const {
    if (consumer_index >= ConsumerCount) {
      throw std::out_of_range("consumer index is outside the fan-out");
    }
    return consumers_[consumer_index];
  }

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

  void release(std::size_t consumer_index, sequence_type sequence) noexcept {
    auto& state = consumers_[consumer_index];
    state.observing = false;
    state.gating_sequence.store(sequence, std::memory_order_release);
    if (sequence == sequence_limit()) {
      state.exhausted = true;
    } else {
      state.next_to_observe = static_cast<sequence_type>(sequence + sequence_type{1});
    }
  }

  void cancel_observation(std::size_t consumer_index) noexcept {
    consumers_[consumer_index].observing = false;
  }

  std::array<T, Capacity> slots_{};
  std::atomic<sequence_type> producer_cursor_{0};
  std::array<ConsumerState, ConsumerCount> consumers_{};
  sequence_type next_to_claim_{first_sequence()};
  bool producer_claimed_{false};
  bool producer_exhausted_{false};
};

} // namespace handoff::sequence
