#include "handoff/mpsc/ordered_publication_ring.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <latch>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

struct Message {
  std::uint64_t position{};
  std::uint64_t inverse{};
};

using SmallRing = handoff::mpsc::OrderedPublicationRing<Message, 3>;

template <typename Token> Token required_token(std::optional<Token> token) {
  if (!token) {
    throw std::runtime_error("required MPSC token was unavailable");
  }
  return std::move(*token);
}

} // namespace

TEST_CASE("ordered MPSC publication holds a reverse-completion hole", "[mpsc]") {
  SmallRing ring;
  std::latch first_claimed{1};
  std::latch second_ready{1};
  std::latch close_hole{1};
  std::atomic<bool> second_returned{false};
  std::atomic<bool> claim_failed{false};
  std::array<std::uint64_t, 2> positions{};

  std::thread first([&] {
    auto claim = ring.try_claim();
    if (!claim) {
      claim_failed.store(true, std::memory_order_relaxed);
      first_claimed.count_down();
      return;
    }
    auto token = std::move(*claim);
    positions[0] = token.position();
    first_claimed.count_down();
    close_hole.wait();
    token.value() = {positions[0], ~positions[0]};
    token.publish();
  });

  first_claimed.wait();
  if (claim_failed.load(std::memory_order_relaxed)) {
    first.join();
    FAIL("first producer could not claim an empty ring");
  }
  std::thread second([&] {
    auto claim = ring.try_claim();
    if (!claim) {
      claim_failed.store(true, std::memory_order_relaxed);
      second_ready.count_down();
      return;
    }
    auto token = std::move(*claim);
    positions[1] = token.position();
    token.value() = {positions[1], ~positions[1]};
    second_ready.count_down();
    token.publish();
    second_returned.store(true, std::memory_order_release);
  });

  second_ready.wait();
  if (claim_failed.load(std::memory_order_relaxed)) {
    close_hole.count_down();
    first.join();
    second.join();
    FAIL("second producer could not claim the next position");
  }
  auto third = ring.try_claim();
  const bool third_claimed = third.has_value();
  if (third) {
    third->value() = {third->position(), ~static_cast<std::uint64_t>(third->position())};
  }
  const bool full_with_hole = !ring.try_claim();
  const bool hole_empty = !ring.try_observe();
  const bool second_waited = !second_returned.load(std::memory_order_acquire);
  close_hole.count_down();
  first.join();
  second.join();
  if (third) {
    third->publish();
  }

  CHECK(positions == std::array<std::uint64_t, 2>{0, 1});
  CHECK(third_claimed);
  CHECK(full_with_hole);
  CHECK(hole_empty);
  CHECK(second_waited);
  CHECK(second_returned.load(std::memory_order_acquire));
  for (std::uint64_t expected = 0; expected < 3; ++expected) {
    auto observation = ring.try_observe();
    REQUIRE(observation);
    auto token = required_token(std::move(observation));
    CHECK(token.position() == expected);
    CHECK(token.value().position == expected);
    CHECK(token.value().inverse == ~expected);
    token.release();
  }
}

TEST_CASE("ordered MPSC charges unfinished claims and reuses only released slots", "[mpsc]") {
  SmallRing ring;
  auto first = ring.try_claim();
  auto second = ring.try_claim();
  auto third = ring.try_claim();
  REQUIRE(first);
  REQUIRE(second);
  REQUIRE(third);
  auto first_token = required_token(std::move(first));
  auto second_token = required_token(std::move(second));
  auto third_token = required_token(std::move(third));
  CHECK_FALSE(ring.try_claim());
  CHECK_FALSE(ring.try_observe());

  third_token.value() = {2, ~std::uint64_t{2}};
  second_token.value() = {1, ~std::uint64_t{1}};
  first_token.value() = {0, ~std::uint64_t{0}};
  first_token.publish();
  CHECK_FALSE(ring.try_claim());
  auto observed = ring.try_observe();
  REQUIRE(observed);
  auto observed_token = required_token(std::move(observed));
  CHECK(observed_token.value().position == 0);
  CHECK_FALSE(ring.try_claim());
  observed_token.release();

  auto wrapped = ring.try_claim();
  REQUIRE(wrapped);
  auto wrapped_token = required_token(std::move(wrapped));
  CHECK(wrapped_token.position() == 3);
  wrapped_token.value() = {3, ~std::uint64_t{3}};
  second_token.publish();
  third_token.publish();
  wrapped_token.publish();
  for (std::uint64_t expected = 1; expected <= 3; ++expected) {
    auto item = ring.try_observe();
    REQUIRE(item);
    auto token = required_token(std::move(item));
    CHECK(token.position() == expected);
    CHECK(token.value().position == expected);
    CHECK(token.value().inverse == ~expected);
    token.release();
  }
  CHECK_FALSE(ring.try_observe());
}

TEST_CASE("ordered MPSC stops at its finite position limit", "[mpsc]") {
  handoff::mpsc::OrderedPublicationRing<Message, 3, std::uint8_t> ring;
  for (unsigned int position = 0; position < 255; ++position) {
    auto claim = ring.try_claim();
    REQUIRE(claim);
    auto token = required_token(std::move(claim));
    CHECK(token.position() == position);
    token.value() = {position, ~static_cast<std::uint64_t>(position)};
    token.publish();
    auto observation = ring.try_observe();
    REQUIRE(observation);
    auto observed = required_token(std::move(observation));
    CHECK(observed.value().position == position);
    observed.release();
  }
  CHECK_FALSE(ring.try_claim());
  CHECK_FALSE(ring.try_observe());
}

TEST_CASE("ordered MPSC preserves unique claims and payloads through repeated wrap", "[mpsc]") {
  constexpr std::uint64_t per_producer = 25'000;
  constexpr std::uint64_t producer_count = 4;
  constexpr std::uint64_t total = per_producer * producer_count;
  handoff::mpsc::OrderedPublicationRing<Message, 64> ring;
  std::atomic<bool> valid{true};
  std::vector<std::thread> producers;
  producers.reserve(producer_count);
  for (std::uint64_t producer = 0; producer < producer_count; ++producer) {
    producers.emplace_back([&] {
      for (std::uint64_t index = 0; index < per_producer; ++index) {
        auto claim = ring.try_claim();
        while (!claim) {
          std::this_thread::yield();
          claim = ring.try_claim();
        }
        auto token = std::move(*claim);
        const auto position = token.position();
        token.value() = {position, ~position};
        token.publish();
      }
    });
  }
  std::thread consumer([&] {
    for (std::uint64_t expected = 0; expected < total; ++expected) {
      auto observation = ring.try_observe();
      while (!observation) {
        std::this_thread::yield();
        observation = ring.try_observe();
      }
      const auto& value = observation->value();
      if (observation->position() != expected || value.position != expected ||
          value.inverse != ~expected) {
        valid.store(false, std::memory_order_relaxed);
      }
      observation->release();
    }
  });

  for (auto& producer : producers) {
    producer.join();
  }
  consumer.join();
  CHECK(valid.load(std::memory_order_relaxed));
  CHECK_FALSE(ring.try_observe());
}
