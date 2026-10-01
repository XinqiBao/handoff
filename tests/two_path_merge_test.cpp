#include "handoff/topology/two_path_merge.hpp"

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <latch>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {
using handoff::topology::MergeSource;

struct Message {
  std::uint64_t sequence{};
  std::uint64_t inverse{};
};

template <typename Token> Token required(std::optional<Token> token) {
  if (!token) {
    throw std::logic_error("expected merge token");
  }
  return std::move(*token);
}

template <typename Merge> void publish(Merge& merge, MergeSource source, Message value) {
  auto claim = source == MergeSource::first ? required(merge.try_reserve_first())
                                            : required(merge.try_reserve_second());
  claim.first().front() = value;
  claim.finish();
}
} // namespace

TEST_CASE("two-path merge rotates between ready path heads", "[topology]") {
  using Merge = handoff::topology::TwoPathMerge<Message, 4>;
  STATIC_CHECK(std::same_as<decltype(std::declval<Merge::Observation&>().value()), const Message&>);
  Merge merge;
  CHECK_FALSE(merge.try_acquire());
  publish(merge, MergeSource::first, {0, ~0ULL});
  publish(merge, MergeSource::first, {1, ~1ULL});
  publish(merge, MergeSource::second, {10, ~10ULL});
  publish(merge, MergeSource::second, {11, ~11ULL});
  for (const auto [expected, lane] :
       std::array{std::pair{0ULL, MergeSource::first}, std::pair{10ULL, MergeSource::second},
                  std::pair{1ULL, MergeSource::first}, std::pair{11ULL, MergeSource::second}}) {
    auto observed = required(merge.try_acquire());
    CHECK(observed.source() == lane);
    CHECK(observed.value().sequence == expected);
    observed.release();
  }
  CHECK_FALSE(merge.try_acquire());
}

TEST_CASE("merge selection need not follow cross-producer publication order", "[topology]") {
  handoff::topology::TwoPathMerge<Message, 1> merge;
  publish(merge, MergeSource::second, {10, ~10ULL});
  publish(merge, MergeSource::first, {0, ~0ULL});
  auto first = required(merge.try_acquire());
  CHECK(first.source() == MergeSource::first);
  first.release();
  auto second = required(merge.try_acquire());
  CHECK(second.source() == MergeSource::second);
  second.release();
}

TEST_CASE("an unpublished first path does not block the second", "[topology]") {
  handoff::topology::TwoPathMerge<Message, 2> merge;
  std::latch first_reserved{1};
  std::latch publish_first{1};
  std::thread first([&] {
    auto claim = required(merge.try_reserve_first());
    claim.first().front() = {1, ~1ULL};
    first_reserved.count_down();
    publish_first.wait();
    claim.finish();
  });
  first_reserved.wait();
  publish(merge, MergeSource::second, {10, ~10ULL});
  auto second = required(merge.try_acquire());
  CHECK(second.source() == MergeSource::second);
  CHECK(second.value().sequence == 10);
  second.release();
  CHECK_FALSE(merge.try_acquire());
  publish_first.count_down();
  first.join();
  auto observed = required(merge.try_acquire());
  CHECK(observed.source() == MergeSource::first);
  CHECK(observed.value().sequence == 1);
  observed.release();
}

TEST_CASE("held and cancelled observations gate only their own path", "[topology]") {
  handoff::topology::TwoPathMerge<Message, 2> merge;
  CHECK(merge.path_capacity() == 2);
  CHECK(merge.total_capacity() == 4);
  publish(merge, MergeSource::first, {0, ~0ULL});
  publish(merge, MergeSource::first, {1, ~1ULL});
  publish(merge, MergeSource::second, {10, ~10ULL});
  publish(merge, MergeSource::second, {11, ~11ULL});
  CHECK_FALSE(merge.try_reserve_first());
  CHECK_FALSE(merge.try_reserve_second());

  auto held = required(merge.try_acquire());
  CHECK(held.source() == MergeSource::first);
  CHECK(held.value().sequence == 0);
  CHECK_FALSE(merge.try_reserve_first());
  auto second = required(merge.try_acquire());
  CHECK(second.source() == MergeSource::second);
  second.release();
  publish(merge, MergeSource::second, {12, ~12ULL});
  held.cancel();
  CHECK_FALSE(merge.try_reserve_first());
  auto retry = required(merge.try_acquire());
  CHECK(retry.source() == MergeSource::first);
  CHECK(retry.value().sequence == 0);
  retry.release();
  publish(merge, MergeSource::first, {2, ~2ULL});
  for (const auto [expected, lane] :
       std::array{std::pair{11ULL, MergeSource::second}, std::pair{1ULL, MergeSource::first},
                  std::pair{12ULL, MergeSource::second}, std::pair{2ULL, MergeSource::first}}) {
    auto observed = required(merge.try_acquire());
    CHECK(observed.source() == lane);
    CHECK(observed.value().sequence == expected);
    observed.release();
  }
}

TEST_CASE("one held path permits repeated physical wrap of the other", "[topology]") {
  handoff::topology::TwoPathMerge<Message, 1> merge;
  publish(merge, MergeSource::first, {0, ~0ULL});
  auto held = required(merge.try_acquire());
  CHECK(held.source() == MergeSource::first);
  for (std::uint64_t n = 0; n < 100; ++n) {
    publish(merge, MergeSource::second, {n, ~n});
    auto observed = required(merge.try_acquire());
    CHECK(observed.source() == MergeSource::second);
    CHECK(observed.value().sequence == n);
    observed.release();
    CHECK_FALSE(merge.try_reserve_first());
  }
  CHECK(held.value().sequence == 0);
  held.release();
  publish(merge, MergeSource::first, {1, ~1ULL});
}

TEST_CASE("two-path merge preserves both FIFOs under concurrent wrap", "[topology]") {
  constexpr std::uint64_t count = 50'000;
  handoff::topology::TwoPathMerge<Message, 8> merge;
  std::atomic<bool> valid{true};
  std::thread first([&] {
    for (std::uint64_t n = 0; n < count; ++n) {
      for (;;) {
        auto attempt = merge.try_reserve_first();
        if (attempt) {
          attempt->first().front() = {n, ~n};
          attempt->finish();
          break;
        }
        std::this_thread::yield();
      }
    }
  });
  std::thread second([&] {
    for (std::uint64_t n = 0; n < count; ++n) {
      for (;;) {
        auto attempt = merge.try_reserve_second();
        if (attempt) {
          attempt->first().front() = {n, ~n};
          attempt->finish();
          break;
        }
        std::this_thread::yield();
      }
    }
  });
  std::array<std::uint64_t, 2> expected{};
  for (std::uint64_t received = 0; received < 2 * count;) {
    auto attempt = merge.try_acquire();
    if (!attempt) {
      std::this_thread::yield();
      continue;
    }
    const std::size_t lane = attempt->source() == MergeSource::first ? 0 : 1;
    const auto value = attempt->value();
    if (value.sequence != expected[lane] || value.inverse != ~value.sequence) {
      valid.store(false, std::memory_order_relaxed);
    }
    attempt->release();
    ++expected[lane];
    ++received;
  }
  first.join();
  second.join();
  CHECK(valid.load(std::memory_order_relaxed));
  CHECK(expected == std::array<std::uint64_t, 2>{count, count});
}
