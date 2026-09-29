#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace handoff::topology {

template <typename Input, typename Left, typename Right, std::size_t Capacity,
          std::unsigned_integral Sequence = std::uint64_t>
  requires std::default_initializable<Input> && std::default_initializable<Left> &&
           std::default_initializable<Right> && (!std::same_as<Sequence, bool>)
class TwoBranchJoin {
  static_assert(Capacity > 0);
  static_assert(Capacity <= static_cast<std::size_t>(std::numeric_limits<Sequence>::max()));

  struct Slot {
    Input input{};
    Left left{};
    Right right{};
  };

public:
  class ProducerClaim;
  class LeftClaim;
  class RightClaim;
  class JoinObservation;

  static constexpr std::size_t capacity() noexcept { return Capacity; }
  static constexpr Sequence position_limit() noexcept {
    return std::numeric_limits<Sequence>::max();
  }

  TwoBranchJoin() = default;
  TwoBranchJoin(const TwoBranchJoin&) = delete;
  TwoBranchJoin& operator=(const TwoBranchJoin&) = delete;

  [[nodiscard]] std::optional<ProducerClaim> try_claim() noexcept {
    if (producer_busy_ || next_publish_ == position_limit() ||
        static_cast<Sequence>(next_publish_ - released_.load(std::memory_order_acquire)) >=
            Capacity) {
      return std::nullopt;
    }
    producer_busy_ = true;
    return ProducerClaim(*this, next_publish_);
  }

  [[nodiscard]] std::optional<LeftClaim> try_acquire_left() noexcept {
    if (left_busy_ || left_next_ == position_limit() ||
        left_next_ == published_.load(std::memory_order_acquire)) {
      return std::nullopt;
    }
    left_busy_ = true;
    return LeftClaim(*this, left_next_);
  }

  [[nodiscard]] std::optional<RightClaim> try_acquire_right() noexcept {
    if (right_busy_ || right_next_ == position_limit() ||
        right_next_ == published_.load(std::memory_order_acquire)) {
      return std::nullopt;
    }
    right_busy_ = true;
    return RightClaim(*this, right_next_);
  }

  [[nodiscard]] std::optional<JoinObservation> try_join() noexcept {
    if (join_busy_ || next_join_ == position_limit() ||
        next_join_ == left_complete_.load(std::memory_order_acquire) ||
        next_join_ == right_complete_.load(std::memory_order_acquire)) {
      return std::nullopt;
    }
    join_busy_ = true;
    return JoinObservation(*this, next_join_);
  }

  class ProducerClaim {
  public:
    ProducerClaim(const ProducerClaim&) = delete;
    ProducerClaim& operator=(const ProducerClaim&) = delete;
    ProducerClaim(ProducerClaim&& other) noexcept
        : ring_(std::exchange(other.ring_, nullptr)), position_(other.position_) {}
    ProducerClaim& operator=(ProducerClaim&&) = delete;
    ~ProducerClaim() { cancel(); }

    [[nodiscard]] Sequence position() const noexcept { return position_; }
    [[nodiscard]] Input& input() noexcept { return ring_->slot(position_).input; }
    void publish() noexcept {
      assert(ring_ != nullptr);
      ring_->next_publish_ = static_cast<Sequence>(position_ + 1);
      ring_->published_.store(ring_->next_publish_, std::memory_order_release);
      ring_->producer_busy_ = false;
      ring_ = nullptr;
    }
    void cancel() noexcept {
      if (ring_ != nullptr) {
        ring_->producer_busy_ = false;
        ring_ = nullptr;
      }
    }

  private:
    friend class TwoBranchJoin;
    ProducerClaim(TwoBranchJoin& ring, Sequence position) noexcept
        : ring_(&ring), position_(position) {}
    TwoBranchJoin* ring_;
    Sequence position_;
  };

  class LeftClaim {
  public:
    LeftClaim(const LeftClaim&) = delete;
    LeftClaim& operator=(const LeftClaim&) = delete;
    LeftClaim(LeftClaim&& other) noexcept
        : ring_(std::exchange(other.ring_, nullptr)), position_(other.position_) {}
    LeftClaim& operator=(LeftClaim&&) = delete;
    ~LeftClaim() { cancel(); }

    [[nodiscard]] Sequence position() const noexcept { return position_; }
    [[nodiscard]] const Input& input() const noexcept { return ring_->slot(position_).input; }
    [[nodiscard]] Left& result() noexcept { return ring_->slot(position_).left; }
    void complete() noexcept {
      assert(ring_ != nullptr);
      ring_->left_next_ = static_cast<Sequence>(position_ + 1);
      ring_->left_complete_.store(ring_->left_next_, std::memory_order_release);
      ring_->left_busy_ = false;
      ring_ = nullptr;
    }
    void cancel() noexcept {
      if (ring_ != nullptr) {
        ring_->left_busy_ = false;
        ring_ = nullptr;
      }
    }

  private:
    friend class TwoBranchJoin;
    LeftClaim(TwoBranchJoin& ring, Sequence position) noexcept
        : ring_(&ring), position_(position) {}
    TwoBranchJoin* ring_;
    Sequence position_;
  };

  class RightClaim {
  public:
    RightClaim(const RightClaim&) = delete;
    RightClaim& operator=(const RightClaim&) = delete;
    RightClaim(RightClaim&& other) noexcept
        : ring_(std::exchange(other.ring_, nullptr)), position_(other.position_) {}
    RightClaim& operator=(RightClaim&&) = delete;
    ~RightClaim() { cancel(); }

    [[nodiscard]] Sequence position() const noexcept { return position_; }
    [[nodiscard]] const Input& input() const noexcept { return ring_->slot(position_).input; }
    [[nodiscard]] Right& result() noexcept { return ring_->slot(position_).right; }
    void complete() noexcept {
      assert(ring_ != nullptr);
      ring_->right_next_ = static_cast<Sequence>(position_ + 1);
      ring_->right_complete_.store(ring_->right_next_, std::memory_order_release);
      ring_->right_busy_ = false;
      ring_ = nullptr;
    }
    void cancel() noexcept {
      if (ring_ != nullptr) {
        ring_->right_busy_ = false;
        ring_ = nullptr;
      }
    }

  private:
    friend class TwoBranchJoin;
    RightClaim(TwoBranchJoin& ring, Sequence position) noexcept
        : ring_(&ring), position_(position) {}
    TwoBranchJoin* ring_;
    Sequence position_;
  };

  class JoinObservation {
  public:
    JoinObservation(const JoinObservation&) = delete;
    JoinObservation& operator=(const JoinObservation&) = delete;
    JoinObservation(JoinObservation&& other) noexcept
        : ring_(std::exchange(other.ring_, nullptr)), position_(other.position_) {}
    JoinObservation& operator=(JoinObservation&&) = delete;
    ~JoinObservation() { cancel(); }

    [[nodiscard]] Sequence position() const noexcept { return position_; }
    [[nodiscard]] const Input& input() const noexcept { return ring_->slot(position_).input; }
    [[nodiscard]] const Left& left() const noexcept { return ring_->slot(position_).left; }
    [[nodiscard]] const Right& right() const noexcept { return ring_->slot(position_).right; }
    void release() noexcept {
      assert(ring_ != nullptr);
      ring_->next_join_ = static_cast<Sequence>(position_ + 1);
      ring_->released_.store(ring_->next_join_, std::memory_order_release);
      ring_->join_busy_ = false;
      ring_ = nullptr;
    }
    void cancel() noexcept {
      if (ring_ != nullptr) {
        ring_->join_busy_ = false;
        ring_ = nullptr;
      }
    }

  private:
    friend class TwoBranchJoin;
    JoinObservation(TwoBranchJoin& ring, Sequence position) noexcept
        : ring_(&ring), position_(position) {}
    TwoBranchJoin* ring_;
    Sequence position_;
  };

private:
  [[nodiscard]] Slot& slot(Sequence position) noexcept {
    return slots_[static_cast<std::size_t>(position) % Capacity];
  }

  std::array<Slot, Capacity> slots_{};
  std::atomic<Sequence> published_{0};
  std::atomic<Sequence> left_complete_{0};
  std::atomic<Sequence> right_complete_{0};
  std::atomic<Sequence> released_{0};
  Sequence next_publish_{0}; // Producer-owned.
  Sequence left_next_{0};    // Left branch-owned.
  Sequence right_next_{0};   // Right branch-owned.
  Sequence next_join_{0};    // Join-owned.
  bool producer_busy_{false};
  bool left_busy_{false};
  bool right_busy_{false};
  bool join_busy_{false};
};

} // namespace handoff::topology
