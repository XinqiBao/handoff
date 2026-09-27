#include "handoff/mpsc/completion_count_ring.hpp"
#include "handoff/mpsc/slot_availability_ring.hpp"

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

template <typename Token> Token required(std::optional<Token> token) {
  if (!token) {
    throw std::runtime_error("required MPSC token was unavailable");
  }
  return std::move(*token);
}

template <typename Ring> void check_concurrent_integrity() {
  constexpr std::uint64_t per_producer = 25'000;
  constexpr std::uint64_t producer_count = 4;
  constexpr std::uint64_t total = per_producer * producer_count;
  Ring ring;
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
      if (observation->position() != expected || observation->value().position != expected ||
          observation->value().inverse != ~expected) {
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

template <typename Ring> void check_finite_limit() {
  Ring ring;
  for (unsigned int position = 0; position < 255; ++position) {
    auto claim = required(ring.try_claim());
    CHECK(claim.position() == position);
    claim.value() = {position, ~static_cast<std::uint64_t>(position)};
    claim.publish();
    auto observation = required(ring.try_observe());
    CHECK(observation.value().position == position);
    observation.release();
  }
  CHECK_FALSE(ring.try_claim());
  CHECK_FALSE(ring.try_observe());
}

} // namespace

TEST_CASE("completion count returns across a hole but waits for the whole claim group", "[mpsc]") {
  handoff::mpsc::CompletionCountRing<Message, 3> ring;
  std::latch first_claimed{1};
  std::latch close_hole{1};
  std::thread first([&] {
    auto token = required(ring.try_claim());
    first_claimed.count_down();
    close_hole.wait();
    token.value() = {0, ~std::uint64_t{0}};
    token.publish();
  });
  first_claimed.wait();
  auto second = required(ring.try_claim());
  second.value() = {1, ~std::uint64_t{1}};
  second.publish();
  CHECK_FALSE(ring.try_observe());
  auto third = required(ring.try_claim());
  third.value() = {2, ~std::uint64_t{2}};
  CHECK_FALSE(ring.try_claim());
  close_hole.count_down();
  first.join();
  CHECK_FALSE(ring.try_observe());
  third.publish();
  for (std::uint64_t expected = 0; expected < 3; ++expected) {
    auto observation = required(ring.try_observe());
    CHECK(observation.position() == expected);
    CHECK(observation.value().position == expected);
    CHECK(observation.value().inverse == ~expected);
    observation.release();
  }
  auto wrapped = required(ring.try_claim());
  CHECK(wrapped.position() == 3);
  wrapped.value() = {3, ~std::uint64_t{3}};
  wrapped.publish();
  auto observation = required(ring.try_observe());
  CHECK(observation.value().position == 3);
  observation.release();
}

TEST_CASE("slot availability discovers a ready prefix when the hole closes", "[mpsc]") {
  handoff::mpsc::SlotAvailabilityRing<Message, 3> ring;
  std::latch first_claimed{1};
  std::latch close_hole{1};
  std::thread first([&] {
    auto token = required(ring.try_claim());
    first_claimed.count_down();
    close_hole.wait();
    token.value() = {0, ~std::uint64_t{0}};
    token.publish();
  });
  first_claimed.wait();
  auto second = required(ring.try_claim());
  second.value() = {1, ~std::uint64_t{1}};
  second.publish();
  auto third = required(ring.try_claim());
  third.value() = {2, ~std::uint64_t{2}};
  CHECK_FALSE(ring.try_claim());
  CHECK_FALSE(ring.try_observe());
  close_hole.count_down();
  first.join();
  for (std::uint64_t expected = 0; expected < 2; ++expected) {
    auto observation = required(ring.try_observe());
    CHECK(observation.position() == expected);
    CHECK(observation.value().position == expected);
    observation.release();
  }
  CHECK_FALSE(ring.try_observe());
  auto wrapped = required(ring.try_claim());
  CHECK(wrapped.position() == 3);
  wrapped.value() = {3, ~std::uint64_t{3}};
  wrapped.publish();
  CHECK_FALSE(ring.try_observe());
  third.publish();
  for (std::uint64_t expected = 2; expected < 4; ++expected) {
    auto observation = required(ring.try_observe());
    CHECK(observation.position() == expected);
    CHECK(observation.value().position == expected);
    observation.release();
  }
}

TEST_CASE("completion MPSC variants stop at finite position exhaustion", "[mpsc]") {
  check_finite_limit<handoff::mpsc::CompletionCountRing<Message, 4, std::uint8_t>>();
  check_finite_limit<handoff::mpsc::SlotAvailabilityRing<Message, 4, std::uint8_t>>();
}

TEST_CASE("completion MPSC variants preserve unique claims across repeated reuse", "[mpsc]") {
  check_concurrent_integrity<handoff::mpsc::CompletionCountRing<Message, 64>>();
  check_concurrent_integrity<handoff::mpsc::SlotAvailabilityRing<Message, 64>>();
}
