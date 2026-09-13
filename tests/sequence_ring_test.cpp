#include "handoff/sequence/bounded_sequence_ring.hpp"

#include <atomic>
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {

struct Message {
  std::uint64_t sequence{};
  std::uint64_t inverse{};
};

template <typename Token> Token require_token(std::optional<Token> token) {
  if (!token.has_value()) {
    throw std::logic_error("expected sequence token");
  }
  return std::move(*token);
}

} // namespace

TEST_CASE("sequence ring starts empty with one-based sequences") {
  using Ring = handoff::sequence::BoundedSequenceRing<std::uint64_t, 4>;
  using Claim = Ring::ProducerClaim;
  using Observation = Ring::ConsumerObservation;

  STATIC_CHECK(std::movable<Claim>);
  STATIC_CHECK_FALSE(std::copy_constructible<Claim>);
  STATIC_CHECK(std::movable<Observation>);
  STATIC_CHECK_FALSE(std::copy_constructible<Observation>);
  STATIC_CHECK(
      std::same_as<decltype(std::declval<const Observation&>().value()), const std::uint64_t&>);

  Ring ring;
  CHECK(ring.capacity() == 4);
  CHECK(ring.first_sequence() == 1);
  CHECK(ring.published_sequence() == 0);
  CHECK(ring.gating_sequence() == 0);
  CHECK_FALSE(ring.try_observe());

  auto claim = require_token(ring.try_claim());
  CHECK(claim.sequence() == 1);
  claim.value() = 10;
  CHECK_FALSE(ring.try_claim());
  CHECK_FALSE(ring.try_observe());
  claim.publish();

  CHECK(ring.published_sequence() == 1);
  auto observation = require_token(ring.try_observe());
  CHECK(observation.sequence() == 1);
  CHECK(observation.value() == 10);
  CHECK_FALSE(ring.try_observe());
  observation.release();
  CHECK(ring.gating_sequence() == 1);
}

TEST_CASE("sequence ring cancellation retries the same sequence") {
  handoff::sequence::BoundedSequenceRing<std::uint64_t, 2> first_ring;
  handoff::sequence::BoundedSequenceRing<std::uint64_t, 2> second_ring;

  {
    auto cancelled = require_token(first_ring.try_claim());
    cancelled.value() = 99;
    CHECK(cancelled.sequence() == 1);
  }
  CHECK(first_ring.published_sequence() == 0);
  auto first = require_token(first_ring.try_claim());
  CHECK(first.sequence() == 1);

  auto second = require_token(second_ring.try_claim());
  auto moved = std::move(second);
  first = std::move(moved);
  CHECK(first.active());
  auto retried = require_token(first_ring.try_claim());
  CHECK(retried.sequence() == 1);
  retried.value() = 7;
  retried.publish();
  first.cancel();

  {
    auto cancelled = require_token(first_ring.try_observe());
    CHECK(cancelled.sequence() == 1);
    CHECK(cancelled.value() == 7);
  }
  CHECK(first_ring.gating_sequence() == 0);
  auto observation = require_token(first_ring.try_observe());
  CHECK(observation.sequence() == 1);
  observation.release();
  CHECK(first_ring.gating_sequence() == 1);
}

TEST_CASE("sequence ring enforces exact capacity and release gating across wrap") {
  handoff::sequence::BoundedSequenceRing<std::uint64_t, 2> ring;

  auto first_claim = require_token(ring.try_claim());
  CHECK(first_claim.sequence() == 1);
  first_claim.value() = 10;
  first_claim.publish();

  auto second_claim = require_token(ring.try_claim());
  CHECK(second_claim.sequence() == 2);
  second_claim.value() = 20;
  second_claim.publish();
  CHECK_FALSE(ring.try_claim());

  auto first = require_token(ring.try_observe());
  CHECK(first.sequence() == 1);
  CHECK(first.value() == 10);
  CHECK_FALSE(ring.try_claim());
  first.release();

  auto wrapped = require_token(ring.try_claim());
  CHECK(wrapped.sequence() == 3);
  wrapped.value() = 30;
  wrapped.publish();
  CHECK_FALSE(ring.try_claim());

  auto second = require_token(ring.try_observe());
  CHECK(second.sequence() == 2);
  CHECK(second.value() == 20);
  second.release();

  auto third = require_token(ring.try_observe());
  CHECK(third.sequence() == 3);
  CHECK(third.value() == 30);
  third.release();
  CHECK_FALSE(ring.try_observe());
}

TEST_CASE("sequence ring retains slot resources until producer reuse") {
  handoff::sequence::BoundedSequenceRing<std::shared_ptr<int>, 1> ring;
  auto first = std::make_shared<int>(10);

  auto claim = require_token(ring.try_claim());
  claim.value() = first;
  claim.publish();
  CHECK(first.use_count() == 2);

  auto observation = require_token(ring.try_observe());
  REQUIRE(observation.value());
  CHECK(*observation.value() == 10);
  observation.release();
  CHECK(first.use_count() == 2);

  auto second = std::make_shared<int>(20);
  claim = require_token(ring.try_claim());
  claim.value() = second;
  CHECK(first.use_count() == 1);
  CHECK(second.use_count() == 2);
  claim.publish();
}

TEST_CASE("sequence ring rejects progress after its finite sequence limit") {
  using Ring = handoff::sequence::BoundedSequenceRing<std::uint64_t, 4, std::uint8_t>;
  Ring ring;

  for (unsigned int expected = 1; expected <= Ring::sequence_limit(); ++expected) {
    auto claim = require_token(ring.try_claim());
    CHECK(static_cast<unsigned int>(claim.sequence()) == expected);
    claim.value() = expected;
    claim.publish();

    auto observation = require_token(ring.try_observe());
    CHECK(static_cast<unsigned int>(observation.sequence()) == expected);
    CHECK(observation.value() == expected);
    observation.release();
  }

  CHECK(ring.published_sequence() == Ring::sequence_limit());
  CHECK(ring.gating_sequence() == Ring::sequence_limit());
  CHECK_FALSE(ring.try_claim());
  CHECK_FALSE(ring.try_observe());
}

TEST_CASE("sequence ring preserves long concurrent sequence and payload integrity") {
  constexpr std::uint64_t message_count = 200'000;
  handoff::sequence::BoundedSequenceRing<Message, 63> ring;
  std::atomic<bool> valid{true};

  std::thread producer([&] {
    for (std::uint64_t expected = 1; expected <= message_count; ++expected) {
      auto claim = ring.try_claim();
      while (!claim) {
        std::this_thread::yield();
        claim = ring.try_claim();
      }
      auto token = std::move(claim).value();
      if (token.sequence() != expected) {
        valid.store(false, std::memory_order_relaxed);
      }
      token.value() = {.sequence = expected, .inverse = ~expected};
      token.publish();
    }
  });

  std::thread consumer([&] {
    for (std::uint64_t expected = 1; expected <= message_count; ++expected) {
      auto observation = ring.try_observe();
      while (!observation) {
        std::this_thread::yield();
        observation = ring.try_observe();
      }
      auto token = std::move(observation).value();
      const auto& message = token.value();
      if (token.sequence() != expected || message.sequence != expected ||
          message.inverse != ~expected) {
        valid.store(false, std::memory_order_relaxed);
      }
      token.release();
    }
  });

  producer.join();
  consumer.join();
  CHECK(valid.load(std::memory_order_relaxed));
}
