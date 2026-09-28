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

namespace handoff::stage {

template <typename T, std::size_t Capacity, std::unsigned_integral Sequence = std::uint64_t>
  requires std::default_initializable<T> && (!std::same_as<Sequence, bool>)
class OrderedWorkerRing {
  static_assert(Capacity > 0);
  static_assert(Capacity <= static_cast<std::size_t>(std::numeric_limits<Sequence>::max()));

public:
  class ProducerClaim;
  class WorkerClaim;
  class DownstreamClaim;
  using sequence_type = Sequence;

  static constexpr std::size_t capacity() noexcept { return Capacity; }
  static constexpr Sequence position_limit() noexcept {
    return std::numeric_limits<Sequence>::max();
  }

  OrderedWorkerRing() = default;
  OrderedWorkerRing(const OrderedWorkerRing&) = delete;
  OrderedWorkerRing& operator=(const OrderedWorkerRing&) = delete;

  [[nodiscard]] Sequence published_prefix() const noexcept {
    return published_.load(std::memory_order_acquire);
  }

  [[nodiscard]] Sequence released_prefix() const noexcept {
    return released_.load(std::memory_order_acquire);
  }

  // Called only by the ordered downstream participant.
  [[nodiscard]] Sequence completed_prefix() noexcept {
    const auto published = published_.load(std::memory_order_acquire);
    while (completed_prefix_ < published &&
           completed_[static_cast<std::size_t>(completed_prefix_) % Capacity].load(
               std::memory_order_acquire) == static_cast<Sequence>(completed_prefix_ + 1)) {
      ++completed_prefix_;
    }
    return completed_prefix_;
  }

  [[nodiscard]] std::optional<ProducerClaim> try_claim() noexcept {
    assert(!producer_claimed_);
    if (next_publish_ == position_limit() ||
        static_cast<Sequence>(next_publish_ - released_.load(std::memory_order_acquire)) >=
            Capacity) {
      return std::nullopt;
    }
    producer_claimed_ = true;
    return ProducerClaim(*this, next_publish_);
  }

  [[nodiscard]] std::optional<WorkerClaim> try_acquire_worker() noexcept {
    auto next = next_worker_.load(std::memory_order_relaxed);
    for (;;) {
      if (next >= published_.load(std::memory_order_acquire)) {
        return std::nullopt;
      }
      if (next_worker_.compare_exchange_weak(next, static_cast<Sequence>(next + 1),
                                             std::memory_order_relaxed,
                                             std::memory_order_relaxed)) {
        return WorkerClaim(*this, next);
      }
    }
  }

  [[nodiscard]] std::optional<DownstreamClaim> try_acquire_downstream() noexcept {
    if (downstream_claimed_ || next_downstream_ == position_limit() ||
        next_downstream_ == completed_prefix()) {
      return std::nullopt;
    }
    downstream_claimed_ = true;
    return DownstreamClaim(*this, next_downstream_);
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
    friend class OrderedWorkerRing;
    ProducerClaim(OrderedWorkerRing& ring, Sequence position) noexcept
        : ring_(&ring), position_(position) {}
    OrderedWorkerRing* ring_;
    Sequence position_;
  };

  class WorkerClaim {
  public:
    WorkerClaim(const WorkerClaim&) = delete;
    WorkerClaim& operator=(const WorkerClaim&) = delete;
    WorkerClaim(WorkerClaim&& other) noexcept
        : ring_(std::exchange(other.ring_, nullptr)), position_(other.position_) {}
    WorkerClaim& operator=(WorkerClaim&&) = delete;
    ~WorkerClaim() {
      if (ring_ != nullptr) {
        std::terminate();
      }
    }
    [[nodiscard]] Sequence position() const noexcept { return position_; }
    [[nodiscard]] T& value() noexcept {
      assert(ring_ != nullptr);
      return ring_->slots_[static_cast<std::size_t>(position_) % Capacity];
    }
    void complete() noexcept {
      assert(ring_ != nullptr);
      ring_->completed_[static_cast<std::size_t>(position_) % Capacity].store(
          static_cast<Sequence>(position_ + 1), std::memory_order_release);
      ring_ = nullptr;
    }

  private:
    friend class OrderedWorkerRing;
    WorkerClaim(OrderedWorkerRing& ring, Sequence position) noexcept
        : ring_(&ring), position_(position) {}
    OrderedWorkerRing* ring_;
    Sequence position_;
  };

  class DownstreamClaim {
  public:
    DownstreamClaim(const DownstreamClaim&) = delete;
    DownstreamClaim& operator=(const DownstreamClaim&) = delete;
    DownstreamClaim(DownstreamClaim&& other) noexcept
        : ring_(std::exchange(other.ring_, nullptr)), position_(other.position_) {}
    DownstreamClaim& operator=(DownstreamClaim&&) = delete;
    ~DownstreamClaim() {
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
      ring_->next_downstream_ = static_cast<Sequence>(position_ + 1);
      ring_->released_.store(ring_->next_downstream_, std::memory_order_release);
      ring_->downstream_claimed_ = false;
      ring_ = nullptr;
    }

  private:
    friend class OrderedWorkerRing;
    DownstreamClaim(OrderedWorkerRing& ring, Sequence position) noexcept
        : ring_(&ring), position_(position) {}
    OrderedWorkerRing* ring_;
    Sequence position_;
  };

private:
  std::array<T, Capacity> slots_{};
  std::array<std::atomic<Sequence>, Capacity> completed_{};
  std::atomic<Sequence> published_{0};
  std::atomic<Sequence> next_worker_{0};
  std::atomic<Sequence> released_{0};
  Sequence next_publish_{0};     // Producer thread only.
  Sequence completed_prefix_{0}; // Downstream thread only.
  Sequence next_downstream_{0};  // Downstream thread only.
  bool producer_claimed_{false};
  bool downstream_claimed_{false};
};

} // namespace handoff::stage
