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

enum class PipelineStage { upstream, downstream };

template <typename T, std::size_t Capacity, std::unsigned_integral Sequence = std::uint64_t>
  requires std::default_initializable<T> && (!std::same_as<Sequence, bool>)
class BoundedSequencePipeline {
  static_assert(Capacity > 0, "a sequence pipeline needs at least one slot");
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
  template <PipelineStage Stage> class ConsumerObservation;

  using value_type = T;
  using sequence_type = Sequence;
  using UpstreamObservation = ConsumerObservation<PipelineStage::upstream>;
  using DownstreamObservation = ConsumerObservation<PipelineStage::downstream>;

  static constexpr std::size_t capacity() noexcept { return Capacity; }
  static constexpr sequence_type first_sequence() noexcept { return sequence_type{1}; }
  static constexpr sequence_type sequence_limit() noexcept {
    return std::numeric_limits<sequence_type>::max();
  }

  BoundedSequencePipeline() = default;
  BoundedSequencePipeline(const BoundedSequencePipeline&) = delete;
  BoundedSequencePipeline& operator=(const BoundedSequencePipeline&) = delete;
  BoundedSequencePipeline(BoundedSequencePipeline&&) = delete;
  BoundedSequencePipeline& operator=(BoundedSequencePipeline&&) = delete;

  [[nodiscard]] sequence_type published_sequence() const noexcept {
    return producer_cursor_.load(std::memory_order_acquire);
  }

  [[nodiscard]] sequence_type upstream_sequence() const noexcept {
    return upstream_.gating_sequence.load(std::memory_order_acquire);
  }

  [[nodiscard]] sequence_type downstream_sequence() const noexcept {
    return downstream_.gating_sequence.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::optional<ProducerClaim> try_claim() {
    if (producer_claimed_ || producer_exhausted_) {
      return std::nullopt;
    }

    const auto gating = downstream_.gating_sequence.load(std::memory_order_acquire);
    const auto occupied = static_cast<sequence_type>((next_to_claim_ - sequence_type{1}) - gating);
    if (occupied >= static_cast<sequence_type>(Capacity)) {
      return std::nullopt;
    }

    producer_claimed_ = true;
    return ProducerClaim(*this, next_to_claim_);
  }

  [[nodiscard]] std::optional<UpstreamObservation> try_observe_upstream() {
    return try_observe<PipelineStage::upstream>();
  }

  [[nodiscard]] std::optional<DownstreamObservation> try_observe_downstream() {
    return try_observe<PipelineStage::downstream>();
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
    friend class BoundedSequencePipeline;

    ProducerClaim(BoundedSequencePipeline& ring, sequence_type sequence) noexcept
        : ring_(&ring), sequence_(sequence) {}

    BoundedSequencePipeline* ring_;
    sequence_type sequence_;
  };

  template <PipelineStage Stage> class ConsumerObservation {
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
    [[nodiscard]] static constexpr PipelineStage stage() noexcept { return Stage; }
    [[nodiscard]] sequence_type sequence() const noexcept { return sequence_; }
    [[nodiscard]] const T& value() const noexcept { return ring_->slot(sequence_); }

    void release() noexcept {
      if (ring_ == nullptr) {
        return;
      }
      ring_->template release<Stage>(sequence_);
      ring_ = nullptr;
    }

    void cancel() noexcept {
      if (ring_ == nullptr) {
        return;
      }
      ring_->template cancel_observation<Stage>();
      ring_ = nullptr;
    }

  private:
    friend class BoundedSequencePipeline;

    ConsumerObservation(BoundedSequencePipeline& ring, sequence_type sequence) noexcept
        : ring_(&ring), sequence_(sequence) {}

    BoundedSequencePipeline* ring_;
    sequence_type sequence_;
  };

private:
  template <PipelineStage Stage> [[nodiscard]] ConsumerState& state() noexcept {
    if constexpr (Stage == PipelineStage::upstream) {
      return upstream_;
    } else {
      return downstream_;
    }
  }

  template <PipelineStage Stage>
  [[nodiscard]] std::optional<ConsumerObservation<Stage>> try_observe() {
    auto& consumer = state<Stage>();
    if (consumer.observing || consumer.exhausted) {
      return std::nullopt;
    }

    const auto available = [&] {
      if constexpr (Stage == PipelineStage::upstream) {
        return producer_cursor_.load(std::memory_order_acquire);
      } else {
        return upstream_.gating_sequence.load(std::memory_order_acquire);
      }
    }();
    if (available < consumer.next_to_observe) {
      return std::nullopt;
    }

    consumer.observing = true;
    return ConsumerObservation<Stage>(*this, consumer.next_to_observe);
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

  template <PipelineStage Stage> void release(sequence_type sequence) noexcept {
    auto& consumer = state<Stage>();
    consumer.observing = false;
    consumer.gating_sequence.store(sequence, std::memory_order_release);
    if (sequence == sequence_limit()) {
      consumer.exhausted = true;
    } else {
      consumer.next_to_observe = static_cast<sequence_type>(sequence + sequence_type{1});
    }
  }

  template <PipelineStage Stage> void cancel_observation() noexcept {
    state<Stage>().observing = false;
  }

  std::array<T, Capacity> slots_{};
  std::atomic<sequence_type> producer_cursor_{0};
  ConsumerState upstream_{};
  ConsumerState downstream_{};
  sequence_type next_to_claim_{first_sequence()};
  bool producer_claimed_{false};
  bool producer_exhausted_{false};
};

} // namespace handoff::sequence
