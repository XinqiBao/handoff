#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>

namespace handoff::spsc {

template <typename T, std::size_t Capacity>
  requires std::default_initializable<T> && std::assignable_from<T&, T>
class BatchBoundedRing {
  static_assert(Capacity > 0, "an SPSC ring needs at least one slot");
  static_assert(Capacity <= std::numeric_limits<std::size_t>::max() / 2,
                "capacity must fit unambiguously in the counter distance");

public:
  using value_type = T;

  static constexpr std::size_t capacity() noexcept { return Capacity; }

  BatchBoundedRing() = default;
  BatchBoundedRing(const BatchBoundedRing&) = delete;
  BatchBoundedRing& operator=(const BatchBoundedRing&) = delete;
  BatchBoundedRing(BatchBoundedRing&&) = delete;
  BatchBoundedRing& operator=(BatchBoundedRing&&) = delete;

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

  [[nodiscard]] bool try_push_batch(std::span<const T> values)
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
    for (std::size_t offset = 0; offset < values.size(); ++offset) {
      slots_[(tail + offset) % Capacity] = values[offset];
    }
    tail_.store(tail + values.size(), std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool try_pop_batch(std::span<T> values)
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
    for (std::size_t offset = 0; offset < values.size(); ++offset) {
      values[offset] = std::move(slots_[(head + offset) % Capacity]);
    }
    head_.store(head + values.size(), std::memory_order_release);
    return true;
  }

private:
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

  std::array<T, Capacity> slots_{};
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

} // namespace handoff::spsc
