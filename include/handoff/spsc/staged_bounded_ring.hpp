#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace handoff::spsc {

// Power-of-two capacity keeps modulo slot mapping continuous at counter rollover.
template <typename T, std::size_t Capacity>
  requires(std::has_single_bit(Capacity)) && std::default_initializable<T>
class StagedBoundedRing {
  static_assert(Capacity > 0, "an SPSC ring needs at least one slot");
  static_assert(Capacity <= std::numeric_limits<std::size_t>::max() / 2,
                "capacity must fit unambiguously in the counter distance");

public:
  class ProducerReservation;
  class ConsumerReservation;

  using value_type = T;

  static constexpr std::size_t capacity() noexcept { return Capacity; }

  StagedBoundedRing() = default;
  StagedBoundedRing(const StagedBoundedRing&) = delete;
  StagedBoundedRing& operator=(const StagedBoundedRing&) = delete;
  StagedBoundedRing(StagedBoundedRing&&) = delete;
  StagedBoundedRing& operator=(StagedBoundedRing&&) = delete;

  [[nodiscard]] std::optional<ProducerReservation> try_reserve_push(std::size_t count) {
    if (count == 0 || count > Capacity || producer_reserved_) {
      return std::nullopt;
    }

    const auto tail = tail_.load(std::memory_order_relaxed);
    const auto head = head_.load(std::memory_order_acquire);
    if (count > Capacity - (tail - head)) {
      return std::nullopt;
    }

    producer_reserved_ = true;
    return ProducerReservation(*this, tail, count);
  }

  [[nodiscard]] std::optional<ConsumerReservation> try_reserve_pop(std::size_t count) {
    if (count == 0 || count > Capacity || consumer_reserved_) {
      return std::nullopt;
    }

    const auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_acquire);
    if (count > tail - head) {
      return std::nullopt;
    }

    consumer_reserved_ = true;
    return ConsumerReservation(*this, head, count);
  }

  class ProducerReservation {
  public:
    ProducerReservation(const ProducerReservation&) = delete;
    ProducerReservation& operator=(const ProducerReservation&) = delete;

    ProducerReservation(ProducerReservation&& other) noexcept
        : ring_(std::exchange(other.ring_, nullptr)), start_(other.start_), count_(other.count_) {}

    ProducerReservation& operator=(ProducerReservation&& other) noexcept {
      if (this != &other) {
        cancel();
        ring_ = std::exchange(other.ring_, nullptr);
        start_ = other.start_;
        count_ = other.count_;
      }
      return *this;
    }

    ~ProducerReservation() { cancel(); }

    [[nodiscard]] bool active() const noexcept { return ring_ != nullptr; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

    [[nodiscard]] std::span<T> first() const noexcept {
      return ring_ == nullptr ? std::span<T>{} : ring_->writable_spans(start_, count_).first;
    }

    [[nodiscard]] std::span<T> second() const noexcept {
      return ring_ == nullptr ? std::span<T>{} : ring_->writable_spans(start_, count_).second;
    }

    void finish() noexcept {
      if (ring_ == nullptr) {
        return;
      }
      ring_->finish_push(start_, count_);
      ring_ = nullptr;
    }

    void cancel() noexcept {
      if (ring_ == nullptr) {
        return;
      }
      ring_->cancel_push();
      ring_ = nullptr;
    }

  private:
    friend class StagedBoundedRing;

    ProducerReservation(StagedBoundedRing& ring, std::size_t start, std::size_t count) noexcept
        : ring_(&ring), start_(start), count_(count) {}

    StagedBoundedRing* ring_;
    std::size_t start_;
    std::size_t count_;
  };

  class ConsumerReservation {
  public:
    ConsumerReservation(const ConsumerReservation&) = delete;
    ConsumerReservation& operator=(const ConsumerReservation&) = delete;

    ConsumerReservation(ConsumerReservation&& other) noexcept
        : ring_(std::exchange(other.ring_, nullptr)), start_(other.start_), count_(other.count_) {}

    ConsumerReservation& operator=(ConsumerReservation&& other) noexcept {
      if (this != &other) {
        cancel();
        ring_ = std::exchange(other.ring_, nullptr);
        start_ = other.start_;
        count_ = other.count_;
      }
      return *this;
    }

    ~ConsumerReservation() { cancel(); }

    [[nodiscard]] bool active() const noexcept { return ring_ != nullptr; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

    [[nodiscard]] std::span<const T> first() const noexcept {
      return ring_ == nullptr ? std::span<const T>{} : ring_->readable_spans(start_, count_).first;
    }

    [[nodiscard]] std::span<const T> second() const noexcept {
      return ring_ == nullptr ? std::span<const T>{} : ring_->readable_spans(start_, count_).second;
    }

    void finish() noexcept {
      if (ring_ == nullptr) {
        return;
      }
      ring_->finish_pop(start_, count_);
      ring_ = nullptr;
    }

    void cancel() noexcept {
      if (ring_ == nullptr) {
        return;
      }
      ring_->cancel_pop();
      ring_ = nullptr;
    }

  private:
    friend class StagedBoundedRing;

    ConsumerReservation(StagedBoundedRing& ring, std::size_t start, std::size_t count) noexcept
        : ring_(&ring), start_(start), count_(count) {}

    StagedBoundedRing* ring_;
    std::size_t start_;
    std::size_t count_;
  };

private:
  friend struct CounterTestAccess;

  template <typename Element> struct SpanPair {
    std::span<Element> first;
    std::span<Element> second;
  };

  [[nodiscard]] SpanPair<T> writable_spans(std::size_t start, std::size_t count) noexcept {
    const auto index = start % Capacity;
    const auto first_count = std::min(count, Capacity - index);
    return {.first = std::span<T>(slots_).subspan(index, first_count),
            .second = std::span<T>(slots_).first(count - first_count)};
  }

  [[nodiscard]] SpanPair<const T> readable_spans(std::size_t start,
                                                 std::size_t count) const noexcept {
    const auto index = start % Capacity;
    const auto first_count = std::min(count, Capacity - index);
    return {.first = std::span<const T>(slots_).subspan(index, first_count),
            .second = std::span<const T>(slots_).first(count - first_count)};
  }

  void finish_push(std::size_t start, std::size_t count) noexcept {
    producer_reserved_ = false;
    tail_.store(start + count, std::memory_order_release);
  }

  void cancel_push() noexcept { producer_reserved_ = false; }

  void finish_pop(std::size_t start, std::size_t count) noexcept {
    consumer_reserved_ = false;
    head_.store(start + count, std::memory_order_release);
  }

  void cancel_pop() noexcept { consumer_reserved_ = false; }

  std::array<T, Capacity> slots_{};
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
  bool producer_reserved_{false};
  bool consumer_reserved_{false};
};

} // namespace handoff::spsc
