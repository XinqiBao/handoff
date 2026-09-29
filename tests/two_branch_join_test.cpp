#include "handoff/topology/two_branch_join.hpp"

#include <atomic>
#include <concepts>
#include <cstdint>
#include <latch>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {
using Join = handoff::topology::TwoBranchJoin<std::uint64_t, std::uint64_t, std::uint64_t, 2>;

template <typename Token> Token required(std::optional<Token> token) {
  if (!token) {
    throw std::logic_error("expected branch token");
  }
  return std::move(*token);
}

template <typename Ring> void publish(Ring& ring, std::uint64_t position) {
  auto claim = required(ring.try_claim());
  CHECK(claim.position() == position);
  claim.input() = position;
  claim.publish();
}

template <typename Ring> void complete_left(Ring& ring, std::uint64_t position) {
  auto claim = required(ring.try_acquire_left());
  CHECK(claim.position() == position);
  CHECK(claim.input() == position);
  claim.result() = position + 100;
  claim.complete();
}

template <typename Ring> void complete_right(Ring& ring, std::uint64_t position) {
  auto claim = required(ring.try_acquire_right());
  CHECK(claim.position() == position);
  CHECK(claim.input() == position);
  claim.result() = position + 200;
  claim.complete();
}

template <typename Ring> void consume(Ring& ring, std::uint64_t position) {
  auto joined = required(ring.try_join());
  CHECK(joined.position() == position);
  CHECK(joined.input() == position);
  CHECK(joined.left() == position + 100);
  CHECK(joined.right() == position + 200);
  joined.release();
}
} // namespace

TEST_CASE("two-branch join grants disjoint branch writes and a combined const view", "[topology]") {
  STATIC_CHECK(std::same_as<decltype(std::declval<Join::LeftClaim&>().result()), std::uint64_t&>);
  STATIC_CHECK(std::same_as<decltype(std::declval<Join::RightClaim&>().result()), std::uint64_t&>);
  STATIC_CHECK(
      std::same_as<decltype(std::declval<Join::JoinObservation&>().left()), const std::uint64_t&>);
  Join ring;
  CHECK_FALSE(ring.try_acquire_left());
  CHECK_FALSE(ring.try_acquire_right());
  CHECK_FALSE(ring.try_join());
  auto cancelled = required(ring.try_claim());
  cancelled.input() = 99;
  cancelled.cancel();
  publish(ring, 0);
  auto left = required(ring.try_acquire_left());
  left.result() = 100;
  left.cancel();
  CHECK_FALSE(ring.try_join());
  complete_left(ring, 0);
  CHECK_FALSE(ring.try_join());
  complete_right(ring, 0);
  consume(ring, 0);
}

TEST_CASE("one branch completion cannot bypass a held join hole", "[topology]") {
  Join ring;
  publish(ring, 0);
  publish(ring, 1);
  std::latch left_held{1};
  std::latch finish_left{1};
  std::thread left([&] {
    auto claim = required(ring.try_acquire_left());
    claim.result() = 100;
    left_held.count_down();
    finish_left.wait();
    claim.complete();
  });
  left_held.wait();
  complete_right(ring, 0);
  complete_right(ring, 1);
  CHECK_FALSE(ring.try_join());
  CHECK_FALSE(ring.try_claim());
  finish_left.count_down();
  left.join();
  auto joined = required(ring.try_join());
  CHECK(joined.position() == 0);
  CHECK(joined.left() == 100);
  CHECK(joined.right() == 200);
  CHECK_FALSE(ring.try_join());
  CHECK_FALSE(ring.try_claim());
  joined.release();
  publish(ring, 2);
  CHECK_FALSE(ring.try_join());
  complete_left(ring, 1);
  consume(ring, 1);
  complete_left(ring, 2);
  complete_right(ring, 2);
  consume(ring, 2);
}

TEST_CASE("join release alone permits exact-capacity physical reuse", "[topology]") {
  Join ring;
  publish(ring, 0);
  publish(ring, 1);
  complete_left(ring, 0);
  complete_right(ring, 0);
  complete_left(ring, 1);
  complete_right(ring, 1);
  CHECK_FALSE(ring.try_claim());
  auto held = required(ring.try_join());
  CHECK_FALSE(ring.try_claim());
  held.cancel();
  CHECK_FALSE(ring.try_claim());
  auto retry = required(ring.try_join());
  CHECK(retry.position() == 0);
  retry.release();
  publish(ring, 2);
  consume(ring, 1);
  CHECK_FALSE(ring.try_join());
  complete_left(ring, 2);
  CHECK_FALSE(ring.try_join());
  complete_right(ring, 2);
  consume(ring, 2);
}

TEST_CASE("two-branch join stops before finite position rollover", "[topology]") {
  handoff::topology::TwoBranchJoin<std::uint64_t, std::uint64_t, std::uint64_t, 3, std::uint8_t>
      ring;
  for (unsigned int position = 0; position < 255; ++position) {
    publish(ring, position);
    complete_left(ring, position);
    complete_right(ring, position);
    consume(ring, position);
  }
  CHECK_FALSE(ring.try_claim());
  CHECK_FALSE(ring.try_acquire_left());
  CHECK_FALSE(ring.try_acquire_right());
  CHECK_FALSE(ring.try_join());
}

TEST_CASE("two-branch join composes concurrent results through repeated wrap", "[topology]") {
  constexpr std::uint64_t count = 30'000;
  handoff::topology::TwoBranchJoin<std::uint64_t, std::uint64_t, std::uint64_t, 7> ring;
  std::atomic<bool> valid{true};
  std::thread producer([&] {
    for (std::uint64_t position = 0; position < count; ++position) {
      for (;;) {
        auto claim = ring.try_claim();
        if (claim) {
          claim->input() = position;
          claim->publish();
          break;
        }
        std::this_thread::yield();
      }
    }
  });
  std::thread left([&] {
    for (std::uint64_t position = 0; position < count; ++position) {
      for (;;) {
        auto claim = ring.try_acquire_left();
        if (claim) {
          if (claim->position() != position || claim->input() != position) {
            valid.store(false, std::memory_order_relaxed);
          }
          claim->result() = position + 100;
          claim->complete();
          break;
        }
        std::this_thread::yield();
      }
    }
  });
  std::thread right([&] {
    for (std::uint64_t position = 0; position < count; ++position) {
      for (;;) {
        auto claim = ring.try_acquire_right();
        if (claim) {
          if (claim->position() != position || claim->input() != position) {
            valid.store(false, std::memory_order_relaxed);
          }
          claim->result() = position + 200;
          claim->complete();
          break;
        }
        std::this_thread::yield();
      }
    }
  });
  for (std::uint64_t position = 0; position < count; ++position) {
    for (;;) {
      auto joined = ring.try_join();
      if (joined) {
        if (joined->position() != position || joined->input() != position ||
            joined->left() != position + 100 || joined->right() != position + 200) {
          valid.store(false, std::memory_order_relaxed);
        }
        joined->release();
        break;
      }
      std::this_thread::yield();
    }
  }
  producer.join();
  left.join();
  right.join();
  CHECK(valid.load(std::memory_order_relaxed));
}
