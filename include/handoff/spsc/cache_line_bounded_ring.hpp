#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <limits>
#include <utility>

namespace handoff::spsc {

template <typename T, std::size_t Capacity>
  requires std::default_initializable<T> && std::assignable_from<T&, T>
class CacheLineBoundedRing {
  static_assert(Capacity > 0, "an SPSC ring needs at least one slot");
  static_assert(Capacity <= std::numeric_limits<std::size_t>::max() / 2,
                "capacity must fit unambiguously in the counter distance");

  static constexpr std::size_t state_alignment_bytes = 128;

  struct alignas(state_alignment_bytes) ConsumerState {
    std::atomic<std::size_t> head{0};
  };

  struct alignas(state_alignment_bytes) ProducerState {
    std::atomic<std::size_t> tail{0};
  };

  static_assert(sizeof(ConsumerState) == state_alignment_bytes);
  static_assert(sizeof(ProducerState) == state_alignment_bytes);

public:
  using value_type = T;

  static constexpr std::size_t capacity() noexcept { return Capacity; }
  static constexpr std::size_t state_alignment() noexcept { return state_alignment_bytes; }

  CacheLineBoundedRing() = default;
  CacheLineBoundedRing(const CacheLineBoundedRing&) = delete;
  CacheLineBoundedRing& operator=(const CacheLineBoundedRing&) = delete;
  CacheLineBoundedRing(CacheLineBoundedRing&&) = delete;
  CacheLineBoundedRing& operator=(CacheLineBoundedRing&&) = delete;

  [[nodiscard]] bool try_push(const T& value)
    requires std::assignable_from<T&, const T&>
  {
    return try_push_impl(value);
  }

  [[nodiscard]] bool try_push(T&& value) { return try_push_impl(std::move(value)); }

  [[nodiscard]] bool try_pop(T& value)
    requires std::assignable_from<T&, T&&>
  {
    const auto head = consumer_.head.load(std::memory_order_relaxed);
    const auto tail = producer_.tail.load(std::memory_order_acquire);
    if (tail == head) {
      return false;
    }

    value = std::move(slots_[head % Capacity]);
    consumer_.head.store(head + 1, std::memory_order_release);
    return true;
  }

private:
  template <typename U> [[nodiscard]] bool try_push_impl(U&& value) {
    const auto tail = producer_.tail.load(std::memory_order_relaxed);
    const auto head = consumer_.head.load(std::memory_order_acquire);
    if (tail - head == Capacity) {
      return false;
    }

    slots_[tail % Capacity] = std::forward<U>(value);
    producer_.tail.store(tail + 1, std::memory_order_release);
    return true;
  }

  std::array<T, Capacity> slots_{};
  ConsumerState consumer_;
  ProducerState producer_;
};

} // namespace handoff::spsc
