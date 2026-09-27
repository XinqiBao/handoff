#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <utility>

namespace handoff::spmc {

template <typename T, std::size_t Capacity, std::unsigned_integral Sequence = std::uint64_t>
  requires std::default_initializable<T> && (!std::same_as<Sequence, bool>)
class SlotCompletionRing {
  static_assert(Capacity > 0);
  static_assert(Capacity <= static_cast<std::size_t>(std::numeric_limits<Sequence>::max()));

public:
  class ProducerClaim;
  class ConsumerClaim;
  using sequence_type = Sequence;
  static constexpr std::size_t capacity() noexcept { return Capacity; }
  static constexpr Sequence position_limit() noexcept {
    return std::numeric_limits<Sequence>::max();
  }

  SlotCompletionRing() = default;
  SlotCompletionRing(const SlotCompletionRing&) = delete;
  SlotCompletionRing& operator=(const SlotCompletionRing&) = delete;

  [[nodiscard]] Sequence reusable_prefix() noexcept {
    while (reusable_ < next_publish_ &&
           completed_[static_cast<std::size_t>(reusable_) % Capacity].load(
               std::memory_order_acquire) == static_cast<Sequence>(reusable_ + 1)) {
      ++reusable_;
    }
    return reusable_;
  }

  [[nodiscard]] std::optional<ProducerClaim> try_claim() noexcept {
    assert(!producer_claimed_);
    if (next_publish_ == position_limit() ||
        static_cast<Sequence>(next_publish_ - reusable_prefix()) >= Capacity) {
      return std::nullopt;
    }
    producer_claimed_ = true;
    return ProducerClaim(*this, next_publish_);
  }

  [[nodiscard]] std::optional<ConsumerClaim> try_acquire() noexcept {
    auto next = next_acquire_.load(std::memory_order_relaxed);
    for (;;) {
      if (next == published_.load(std::memory_order_acquire)) {
        return std::nullopt;
      }
      if (next_acquire_.compare_exchange_weak(next, static_cast<Sequence>(next + 1),
                                              std::memory_order_relaxed,
                                              std::memory_order_relaxed)) {
        return ConsumerClaim(*this, next);
      }
    }
  }

  class ProducerClaim {
  public:
    ProducerClaim(const ProducerClaim&) = delete;
    ProducerClaim& operator=(const ProducerClaim&) = delete;
    ProducerClaim(ProducerClaim&& other) noexcept
        : ring_(std::exchange(other.ring_, nullptr)), position_(other.position_) {}
    ProducerClaim& operator=(ProducerClaim&&) = delete;
    ~ProducerClaim() {
      if (ring_ != nullptr) {
        std::terminate();
      }
    }
    [[nodiscard]] Sequence position() const noexcept { return position_; }
    [[nodiscard]] T& value() noexcept {
      assert(ring_ != nullptr);
      return ring_->slots_[static_cast<std::size_t>(position_) % Capacity];
    }
    void publish() noexcept {
      assert(ring_ != nullptr);
      ring_->next_publish_ = static_cast<Sequence>(position_ + 1);
      ring_->published_.store(ring_->next_publish_, std::memory_order_release);
      ring_->producer_claimed_ = false;
      ring_ = nullptr;
    }
    void cancel() noexcept {
      if (ring_ != nullptr) {
        ring_->producer_claimed_ = false;
        ring_ = nullptr;
      }
    }

  private:
    friend class SlotCompletionRing;
    ProducerClaim(SlotCompletionRing& ring, Sequence position) noexcept
        : ring_(&ring), position_(position) {}
    SlotCompletionRing* ring_;
    Sequence position_;
  };

  class ConsumerClaim {
  public:
    ConsumerClaim(const ConsumerClaim&) = delete;
    ConsumerClaim& operator=(const ConsumerClaim&) = delete;
    ConsumerClaim(ConsumerClaim&& other) noexcept
        : ring_(std::exchange(other.ring_, nullptr)), position_(other.position_) {}
    ConsumerClaim& operator=(ConsumerClaim&&) = delete;
    ~ConsumerClaim() {
      if (ring_ != nullptr) {
        std::terminate();
      }
    }
    [[nodiscard]] Sequence position() const noexcept { return position_; }
    [[nodiscard]] const T& value() const noexcept {
      assert(ring_ != nullptr);
      return ring_->slots_[static_cast<std::size_t>(position_) % Capacity];
    }
    void release() noexcept {
      assert(ring_ != nullptr);
      ring_->completed_[static_cast<std::size_t>(position_) % Capacity].store(
          static_cast<Sequence>(position_ + 1), std::memory_order_release);
      ring_ = nullptr;
    }

  private:
    friend class SlotCompletionRing;
    ConsumerClaim(SlotCompletionRing& ring, Sequence position) noexcept
        : ring_(&ring), position_(position) {}
    SlotCompletionRing* ring_;
    Sequence position_;
  };

private:
  std::array<T, Capacity> slots_{};
  std::array<std::atomic<Sequence>, Capacity> completed_{};
  std::atomic<Sequence> published_{0};
  std::atomic<Sequence> next_acquire_{0};
  Sequence next_publish_{0}; // Producer thread only.
  Sequence reusable_{0};     // Producer thread only.
  bool producer_claimed_{false};
};

} // namespace handoff::spmc
