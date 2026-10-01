#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>

namespace handoff::spsc {

// Power-of-two capacity keeps modulo slot mapping continuous at counter rollover.
template <typename T, std::size_t Capacity>
  requires(std::has_single_bit(Capacity)) && std::default_initializable<T> &&
          std::assignable_from<T&, T>
class BulkBurstBoundedRing {
  static_assert(Capacity > 0, "an SPSC ring needs at least one slot");
  static_assert(Capacity <= std::numeric_limits<std::size_t>::max() / 2,
                "capacity must fit unambiguously in the counter distance");

public:
  using value_type = T;

  static constexpr std::size_t capacity() noexcept { return Capacity; }

  BulkBurstBoundedRing() = default;
  BulkBurstBoundedRing(const BulkBurstBoundedRing&) = delete;
  BulkBurstBoundedRing& operator=(const BulkBurstBoundedRing&) = delete;
  BulkBurstBoundedRing(BulkBurstBoundedRing&&) = delete;
  BulkBurstBoundedRing& operator=(BulkBurstBoundedRing&&) = delete;

  [[nodiscard]] bool try_push(const T& value)
    requires std::assignable_from<T&, const T&>
  {
    return try_push_impl(value);
  }

  [[nodiscard]] bool try_push(T&& value) { return try_push_impl(std::move(value)); }

  [[nodiscard]] bool try_pop(T& value)
    requires std::assignable_from<T&, T&&>
  {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_acquire);
    if (tail == head) {
      return false;
    }

    value = std::move(slots_[head % Capacity]);
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool try_push_bulk(std::span<const T> values)
    requires std::assignable_from<T&, const T&>
  {
    if (values.empty()) {
      return true;
    }
    if (values.size() > Capacity) {
      return false;
    }

    const auto tail = tail_.load(std::memory_order_relaxed);
    const auto head = head_.load(std::memory_order_acquire);
    if (values.size() > Capacity - (tail - head)) {
      return false;
    }
    assign_to_slots(tail, values);
    tail_.store(tail + values.size(), std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool try_pop_bulk(std::span<T> values)
    requires std::assignable_from<T&, T&&>
  {
    if (values.empty()) {
      return true;
    }
    if (values.size() > Capacity) {
      return false;
    }

    const auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_acquire);
    if (values.size() > tail - head) {
      return false;
    }
    assign_from_slots(head, values);
    head_.store(head + values.size(), std::memory_order_release);
    return true;
  }

  [[nodiscard]] std::size_t try_push_burst(std::span<const T> values)
    requires std::assignable_from<T&, const T&>
  {
    if (values.empty()) {
      return 0;
    }
    const auto tail = tail_.load(std::memory_order_relaxed);
    const auto head = head_.load(std::memory_order_acquire);
    const auto count = std::min(values.size(), Capacity - (tail - head));
    if (count == 0) {
      return 0;
    }
    assign_to_slots(tail, values.first(count));
    tail_.store(tail + count, std::memory_order_release);
    return count;
  }

  [[nodiscard]] std::size_t try_pop_burst(std::span<T> values)
    requires std::assignable_from<T&, T&&>
  {
    if (values.empty()) {
      return 0;
    }
    const auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_acquire);
    const auto count = std::min(values.size(), tail - head);
    if (count == 0) {
      return 0;
    }
    assign_from_slots(head, values.first(count));
    head_.store(head + count, std::memory_order_release);
    return count;
  }

private:
  friend struct CounterTestAccess;

  template <typename U> [[nodiscard]] bool try_push_impl(U&& value) {
    const auto tail = tail_.load(std::memory_order_relaxed);
    const auto head = head_.load(std::memory_order_acquire);
    if (tail - head == Capacity) {
      return false;
    }

    slots_[tail % Capacity] = std::forward<U>(value);
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  void assign_to_slots(std::size_t tail, std::span<const T> values)
    requires std::assignable_from<T&, const T&>
  {
    for (std::size_t offset = 0; offset < values.size(); ++offset) {
      slots_[(tail + offset) % Capacity] = values[offset];
    }
  }

  void assign_from_slots(std::size_t head, std::span<T> values)
    requires std::assignable_from<T&, T&&>
  {
    for (std::size_t offset = 0; offset < values.size(); ++offset) {
      values[offset] = std::move(slots_[(head + offset) % Capacity]);
    }
  }

  std::array<T, Capacity> slots_{};
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

} // namespace handoff::spsc
