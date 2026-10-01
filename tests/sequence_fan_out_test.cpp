#include "handoff/sequence/bounded_sequence_fan_out.hpp"

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
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
    throw std::logic_error("expected fan-out token");
  }
  return std::move(*token);
}

} // namespace

TEST_CASE("sequence fan-out starts empty and validates stable consumer indices") {
  using Ring = handoff::sequence::BoundedSequenceFanOut<std::uint64_t, 4, 3>;
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
  CHECK(ring.consumer_count() == 3);
  CHECK(ring.first_sequence() == 1);
  CHECK(ring.published_sequence() == 0);
  CHECK(ring.minimum_gating_sequence() == 0);
  for (std::size_t index = 0; index < ring.consumer_count(); ++index) {
    CHECK(ring.gating_sequence(index) == 0);
    CHECK_FALSE(ring.try_observe(index));
  }
  CHECK_THROWS_AS(ring.gating_sequence(3), std::out_of_range);
  CHECK_THROWS_AS(ring.try_observe(3), std::out_of_range);

  auto claim = require_token(ring.try_claim());
  CHECK(claim.sequence() == 1);
  CHECK(claim.value() == 0);
  claim.value() = 42;
  CHECK_FALSE(ring.try_claim());
  claim.publish();

  for (std::size_t index = 0; index < ring.consumer_count(); ++index) {
    auto observation = require_token(ring.try_observe(index));
    CHECK(observation.consumer_index() == index);
    CHECK(observation.sequence() == 1);
    CHECK(observation.value() == 42);
    CHECK_FALSE(ring.try_observe(index));
    observation.release();
  }
  CHECK(ring.minimum_gating_sequence() == 1);
}

TEST_CASE("sequence fan-out gates producer reuse on the slowest consumer") {
  handoff::sequence::BoundedSequenceFanOut<std::uint64_t, 2, 2> ring;

  auto first_claim = require_token(ring.try_claim());
  first_claim.value() = 10;
  first_claim.publish();
  auto second_claim = require_token(ring.try_claim());
  second_claim.value() = 20;
  second_claim.publish();
  CHECK_FALSE(ring.try_claim());

  auto slow_first = require_token(ring.try_observe(1));
  CHECK(slow_first.sequence() == 1);
  CHECK(slow_first.value() == 10);

  auto fast_first = require_token(ring.try_observe(0));
  CHECK(fast_first.value() == 10);
  fast_first.release();
  auto fast_second = require_token(ring.try_observe(0));
  CHECK(fast_second.sequence() == 2);
  CHECK(fast_second.value() == 20);
  fast_second.release();
  CHECK(ring.gating_sequence(0) == 2);
  CHECK(ring.gating_sequence(1) == 0);
  CHECK(ring.minimum_gating_sequence() == 0);
  CHECK_FALSE(ring.try_claim());

  slow_first.release();
  CHECK(ring.minimum_gating_sequence() == 1);
  auto wrapped = require_token(ring.try_claim());
  CHECK(wrapped.sequence() == 3);
  wrapped.value() = 30;
  wrapped.publish();
  CHECK_FALSE(ring.try_claim());

  auto slow_second = require_token(ring.try_observe(1));
  CHECK(slow_second.sequence() == 2);
  CHECK(slow_second.value() == 20);
  slow_second.release();
  auto slow_wrapped = require_token(ring.try_observe(1));
  CHECK(slow_wrapped.sequence() == 3);
  CHECK(slow_wrapped.value() == 30);
  slow_wrapped.release();

  auto fast_wrapped = require_token(ring.try_observe(0));
  CHECK(fast_wrapped.sequence() == 3);
  CHECK(fast_wrapped.value() == 30);
  fast_wrapped.release();
}

TEST_CASE("sequence fan-out cancellations affect only their owning operation") {
  handoff::sequence::BoundedSequenceFanOut<std::uint64_t, 2, 2> first_ring;
  handoff::sequence::BoundedSequenceFanOut<std::uint64_t, 2, 2> second_ring;

  {
    auto cancelled = require_token(first_ring.try_claim());
    cancelled.value() = 99;
  }
  auto first = require_token(first_ring.try_claim());
  CHECK(first.sequence() == 1);

  auto second = require_token(second_ring.try_claim());
  auto moved = std::move(second);
  first = std::move(moved);
  auto retried = require_token(first_ring.try_claim());
  retried.value() = 7;
  retried.publish();
  first.cancel();

  {
    auto cancelled = require_token(first_ring.try_observe(0));
    CHECK(cancelled.consumer_index() == 0);
    CHECK(cancelled.value() == 7);
  }
  CHECK(first_ring.gating_sequence(0) == 0);

  auto other_consumer = require_token(first_ring.try_observe(1));
  other_consumer.release();
  CHECK(first_ring.gating_sequence(1) == 1);
  CHECK(first_ring.gating_sequence(0) == 0);

  auto retry = require_token(first_ring.try_observe(0));
  CHECK(retry.sequence() == 1);
  retry.release();
  CHECK(first_ring.minimum_gating_sequence() == 1);
}

TEST_CASE("sequence fan-out retains resources until producer reuse") {
  handoff::sequence::BoundedSequenceFanOut<std::shared_ptr<int>, 1, 2> ring;
  auto first = std::make_shared<int>(10);

  auto claim = require_token(ring.try_claim());
  claim.value() = first;
  claim.publish();
  CHECK(first.use_count() == 2);

  for (std::size_t index = 0; index < ring.consumer_count(); ++index) {
    auto observation = require_token(ring.try_observe(index));
    REQUIRE(observation.value());
    CHECK(*observation.value() == 10);
    observation.release();
  }
  CHECK(first.use_count() == 2);

  auto second = std::make_shared<int>(20);
  claim = require_token(ring.try_claim());
  claim.value() = second;
  CHECK(first.use_count() == 1);
  CHECK(second.use_count() == 2);
  claim.publish();
}

TEST_CASE("sequence fan-out rejects progress after the finite sequence limit") {
  using Ring = handoff::sequence::BoundedSequenceFanOut<std::uint64_t, 3, 2, std::uint8_t>;
  Ring ring;

  for (unsigned int expected = 1; expected <= Ring::sequence_limit(); ++expected) {
    auto claim = require_token(ring.try_claim());
    CHECK(static_cast<unsigned int>(claim.sequence()) == expected);
    claim.value() = expected;
    claim.publish();

    for (std::size_t index = 0; index < ring.consumer_count(); ++index) {
      auto observation = require_token(ring.try_observe(index));
      CHECK(static_cast<unsigned int>(observation.sequence()) == expected);
      CHECK(observation.value() == expected);
      observation.release();
    }
  }

  CHECK(ring.published_sequence() == Ring::sequence_limit());
  CHECK(ring.minimum_gating_sequence() == Ring::sequence_limit());
  CHECK_FALSE(ring.try_claim());
  CHECK_FALSE(ring.try_observe(0));
  CHECK_FALSE(ring.try_observe(1));
}

TEST_CASE("sequence fan-out preserves concurrent integrity for differently paced consumers") {
  constexpr std::uint64_t message_count = 200'000;
  handoff::sequence::BoundedSequenceFanOut<Message, 63, 2> ring;
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

  std::array<std::thread, 2> consumers;
  for (std::size_t index = 0; index < consumers.size(); ++index) {
    consumers[index] = std::thread([&, index] {
      for (std::uint64_t expected = 1; expected <= message_count; ++expected) {
        if (index == 1 && expected % 7 == 0) {
          std::this_thread::yield();
        }
        auto observation = ring.try_observe(index);
        while (!observation) {
          std::this_thread::yield();
          observation = ring.try_observe(index);
        }
        auto token = std::move(observation).value();
        const auto& message = token.value();
        if (token.consumer_index() != index || token.sequence() != expected ||
            message.sequence != expected || message.inverse != ~expected) {
          valid.store(false, std::memory_order_relaxed);
        }
        token.release();
      }
    });
  }

  producer.join();
  for (auto& consumer : consumers) {
    consumer.join();
  }
  CHECK(valid.load(std::memory_order_relaxed));
}
