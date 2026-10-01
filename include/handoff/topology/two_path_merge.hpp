#pragma once

#include "handoff/spsc/staged_bounded_ring.hpp"

#include <bit>
#include <concepts>
#include <cstddef>
#include <optional>
#include <utility>

namespace handoff::topology {

enum class MergeSource { first, second };

template <typename T, std::size_t PathCapacity>
  requires(std::has_single_bit(PathCapacity)) && std::default_initializable<T>
class TwoPathMerge {
  using Path = spsc::StagedBoundedRing<T, PathCapacity>;

public:
  class Observation;
  using value_type = T;
  using ProducerReservation = typename Path::ProducerReservation;

  static constexpr std::size_t path_capacity() noexcept { return PathCapacity; }
  static constexpr std::size_t total_capacity() noexcept { return 2 * PathCapacity; }

  TwoPathMerge() = default;
  TwoPathMerge(const TwoPathMerge&) = delete;
  TwoPathMerge& operator=(const TwoPathMerge&) = delete;
  TwoPathMerge(TwoPathMerge&&) = delete;
  TwoPathMerge& operator=(TwoPathMerge&&) = delete;

  [[nodiscard]] std::optional<ProducerReservation> try_reserve_first() {
    return first_.try_reserve_push(1);
  }
  [[nodiscard]] std::optional<ProducerReservation> try_reserve_second() {
    return second_.try_reserve_push(1);
  }

  [[nodiscard]] std::optional<Observation> try_acquire() {
    if (next_ == MergeSource::first) {
      if (auto held = first_.try_reserve_pop(1)) {
        next_ = MergeSource::second;
        return Observation(MergeSource::first, std::move(*held));
      }
      if (auto held = second_.try_reserve_pop(1)) {
        return Observation(MergeSource::second, std::move(*held));
      }
    } else {
      if (auto held = second_.try_reserve_pop(1)) {
        next_ = MergeSource::first;
        return Observation(MergeSource::second, std::move(*held));
      }
      if (auto held = first_.try_reserve_pop(1)) {
        return Observation(MergeSource::first, std::move(*held));
      }
    }
    return std::nullopt;
  }

  class Observation {
  public:
    Observation(const Observation&) = delete;
    Observation& operator=(const Observation&) = delete;
    Observation(Observation&&) noexcept = default;
    Observation& operator=(Observation&&) noexcept = default;

    [[nodiscard]] MergeSource source() const noexcept { return source_; }
    [[nodiscard]] const T& value() const noexcept { return reservation_.first().front(); }
    void release() noexcept { reservation_.finish(); }
    void cancel() noexcept { reservation_.cancel(); }

  private:
    friend class TwoPathMerge;
    Observation(MergeSource source, typename Path::ConsumerReservation&& reservation) noexcept
        : source_(source), reservation_(std::move(reservation)) {}

    MergeSource source_;
    typename Path::ConsumerReservation reservation_;
  };

private:
  Path first_;
  Path second_;
  MergeSource next_{MergeSource::first}; // Consumer-owned polling preference.
};

} // namespace handoff::topology
