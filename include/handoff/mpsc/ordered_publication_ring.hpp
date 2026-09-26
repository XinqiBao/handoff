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
#include <thread>
#include <utility>

namespace handoff::mpsc {

template <typename T, std::size_t Capacity, std::unsigned_integral Sequence = std::uint64_t>
  requires std::default_initializable<T> && (!std::same_as<Sequence, bool>)
class OrderedPublicationRing {
  static_assert(Capacity > 0, "an MPSC ring needs at least one slot");
  static_assert(Capacity <= static_cast<std::size_t>(std::numeric_limits<Sequence>::max()),
                "capacity must fit the finite position domain");

public:
  class ProducerClaim;
  class ConsumerObservation;

  using value_type = T;
  using sequence_type = Sequence;

  static constexpr std::size_t capacity() noexcept { return Capacity; }
  static constexpr sequence_type position_limit() noexcept {
    return std::numeric_limits<sequence_type>::max();
  }

  OrderedPublicationRing() = default;
  OrderedPublicationRing(const OrderedPublicationRing&) = delete;
  OrderedPublicationRing& operator=(const OrderedPublicationRing&) = delete;
  OrderedPublicationRing(OrderedPublicationRing&&) = delete;
  OrderedPublicationRing& operator=(OrderedPublicationRing&&) = delete;

  [[nodiscard]] std::optional<ProducerClaim> try_claim() noexcept {
    auto next = next_claim_.load(std::memory_order_relaxed);
    for (;;) {
      if (next == position_limit()) {
        return std::nullopt;
      }
      const auto released = released_.load(std::memory_order_acquire);
      if (static_cast<sequence_type>(next - released) >= Capacity) {
        return std::nullopt;
      }
      const auto following = static_cast<sequence_type>(next + 1);
      if (next_claim_.compare_exchange_weak(next, following, std::memory_order_relaxed,
                                            std::memory_order_relaxed)) {
        return ProducerClaim(*this, next);
      }
    }
  }

  [[nodiscard]] std::optional<ConsumerObservation> try_observe() noexcept {
    if (consumer_observing_ || published_.load(std::memory_order_acquire) <= next_to_observe_) {
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
        : ring_(std::exchange(other.ring_, nullptr)), position_(other.position_) {}
    ProducerClaim& operator=(ProducerClaim&& other) noexcept {
      if (this != &other) {
        if (ring_ != nullptr) {
          std::terminate();
        }
        ring_ = std::exchange(other.ring_, nullptr);
        position_ = other.position_;
      }
      return *this;
    }
    ~ProducerClaim() {
      if (ring_ != nullptr) {
        std::terminate();
      }
    }

    [[nodiscard]] sequence_type position() const noexcept { return position_; }
    [[nodiscard]] T& value() noexcept {
      assert(ring_ != nullptr);
      return ring_->slots_[static_cast<std::size_t>(position_) % Capacity];
    }
    [[nodiscard]] bool try_publish() noexcept {
      assert(ring_ != nullptr);
      if (ring_->published_.load(std::memory_order_acquire) != position_) {
        return false;
      }
      ring_->published_.store(static_cast<sequence_type>(position_ + 1), std::memory_order_release);
      ring_ = nullptr;
      return true;
    }
    void publish() noexcept {
      while (!try_publish()) {
        std::this_thread::yield();
      }
    }

  private:
    friend class OrderedPublicationRing;
    ProducerClaim(OrderedPublicationRing& ring, sequence_type position) noexcept
        : ring_(&ring), position_(position) {}

    OrderedPublicationRing* ring_;
    sequence_type position_;
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
    [[nodiscard]] const T& value() const noexcept {
      assert(ring_ != nullptr);
      return ring_->slots_[static_cast<std::size_t>(position_) % Capacity];
    }
    void release() noexcept {
      assert(ring_ != nullptr);
      ring_->next_to_observe_ = static_cast<sequence_type>(position_ + 1);
      ring_->released_.store(ring_->next_to_observe_, std::memory_order_release);
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
    friend class OrderedPublicationRing;
    ConsumerObservation(OrderedPublicationRing& ring, sequence_type position) noexcept
        : ring_(&ring), position_(position) {}

    OrderedPublicationRing* ring_;
    sequence_type position_;
  };

private:
  std::array<T, Capacity> slots_{};
  std::atomic<sequence_type> next_claim_{0};
  std::atomic<sequence_type> published_{0};
  std::atomic<sequence_type> released_{0};
  sequence_type next_to_observe_{0};
  bool consumer_observing_{false};
};

} // namespace handoff::mpsc
