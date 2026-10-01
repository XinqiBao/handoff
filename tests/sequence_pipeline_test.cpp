#include "handoff/sequence/bounded_sequence_pipeline.hpp"

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
    throw std::logic_error("expected pipeline token");
  }
  return std::move(*token);
}

} // namespace

TEST_CASE("sequence pipeline exposes stable upstream and downstream roles") {
  using Ring = handoff::sequence::BoundedSequencePipeline<std::uint64_t, 4>;
  using Claim = Ring::ProducerClaim;
  using Upstream = Ring::UpstreamObservation;
  using Downstream = Ring::DownstreamObservation;

  STATIC_CHECK(std::movable<Claim>);
  STATIC_CHECK_FALSE(std::copy_constructible<Claim>);
  STATIC_CHECK(std::movable<Upstream>);
  STATIC_CHECK_FALSE(std::copy_constructible<Upstream>);
  STATIC_CHECK(std::movable<Downstream>);
  STATIC_CHECK_FALSE(std::copy_constructible<Downstream>);
  STATIC_CHECK(Upstream::stage() == handoff::sequence::PipelineStage::upstream);
  STATIC_CHECK(Downstream::stage() == handoff::sequence::PipelineStage::downstream);
  STATIC_CHECK(
      std::same_as<decltype(std::declval<const Upstream&>().value()), const std::uint64_t&>);

  Ring ring;
  CHECK(ring.capacity() == 4);
  CHECK(ring.first_sequence() == 1);
  CHECK(ring.published_sequence() == 0);
  CHECK(ring.upstream_sequence() == 0);
  CHECK(ring.downstream_sequence() == 0);
  CHECK_FALSE(ring.try_observe_upstream());
  CHECK_FALSE(ring.try_observe_downstream());

  auto claim = require_token(ring.try_claim());
  CHECK(claim.sequence() == 1);
  CHECK(claim.value() == 0);
  claim.value() = 42;
  claim.publish();

  CHECK_FALSE(ring.try_observe_downstream());
  auto upstream = require_token(ring.try_observe_upstream());
  CHECK(upstream.sequence() == 1);
  CHECK(upstream.value() == 42);
  CHECK_FALSE(ring.try_observe_upstream());
  CHECK_FALSE(ring.try_observe_downstream());
  upstream.release();

  auto downstream = require_token(ring.try_observe_downstream());
  CHECK(downstream.sequence() == 1);
  CHECK(downstream.value() == 42);
  CHECK_FALSE(ring.try_observe_downstream());
  downstream.release();
}

TEST_CASE("sequence pipeline gates wrap reuse on downstream release") {
  handoff::sequence::BoundedSequencePipeline<std::uint64_t, 2> ring;

  auto first_claim = require_token(ring.try_claim());
  first_claim.value() = 10;
  first_claim.publish();
  auto second_claim = require_token(ring.try_claim());
  second_claim.value() = 20;
  second_claim.publish();
  CHECK_FALSE(ring.try_claim());

  auto upstream_first = require_token(ring.try_observe_upstream());
  upstream_first.release();
  auto upstream_second = require_token(ring.try_observe_upstream());
  upstream_second.release();
  CHECK(ring.upstream_sequence() == 2);
  CHECK(ring.downstream_sequence() == 0);
  CHECK_FALSE(ring.try_claim());

  auto downstream_first = require_token(ring.try_observe_downstream());
  CHECK(downstream_first.sequence() == 1);
  CHECK(downstream_first.value() == 10);
  downstream_first.release();

  auto wrapped = require_token(ring.try_claim());
  CHECK(wrapped.sequence() == 3);
  wrapped.value() = 30;
  wrapped.publish();
  CHECK_FALSE(ring.try_claim());

  auto downstream_second = require_token(ring.try_observe_downstream());
  CHECK(downstream_second.sequence() == 2);
  CHECK(downstream_second.value() == 20);
  downstream_second.release();

  auto upstream_wrapped = require_token(ring.try_observe_upstream());
  CHECK(upstream_wrapped.sequence() == 3);
  CHECK(upstream_wrapped.value() == 30);
  upstream_wrapped.release();
  auto downstream_wrapped = require_token(ring.try_observe_downstream());
  CHECK(downstream_wrapped.sequence() == 3);
  CHECK(downstream_wrapped.value() == 30);
  downstream_wrapped.release();
}

TEST_CASE("sequence pipeline cancellation preserves the owning stage") {
  handoff::sequence::BoundedSequencePipeline<std::uint64_t, 2> ring;

  {
    auto cancelled = require_token(ring.try_claim());
    cancelled.value() = 99;
  }
  auto claim = require_token(ring.try_claim());
  CHECK(claim.sequence() == 1);
  claim.value() = 7;
  claim.publish();

  {
    auto cancelled = require_token(ring.try_observe_upstream());
    CHECK(cancelled.value() == 7);
  }
  CHECK(ring.upstream_sequence() == 0);
  CHECK_FALSE(ring.try_observe_downstream());

  auto upstream = require_token(ring.try_observe_upstream());
  upstream.release();
  {
    auto cancelled = require_token(ring.try_observe_downstream());
    CHECK(cancelled.value() == 7);
  }
  CHECK(ring.downstream_sequence() == 0);

  auto downstream = require_token(ring.try_observe_downstream());
  CHECK(downstream.sequence() == 1);
  downstream.release();
  CHECK(ring.downstream_sequence() == 1);
}

TEST_CASE("sequence pipeline token moves transfer and cancel responsibilities") {
  using Ring = handoff::sequence::BoundedSequencePipeline<std::uint64_t, 2>;
  Ring first_ring;
  Ring second_ring;

  auto first_claim = require_token(first_ring.try_claim());
  auto second_claim = require_token(second_ring.try_claim());
  auto moved_claim = std::move(second_claim);
  first_claim = std::move(moved_claim);

  auto retried_first_claim = require_token(first_ring.try_claim());
  retried_first_claim.value() = 10;
  retried_first_claim.publish();
  first_claim.cancel();
  auto retried_second_claim = require_token(second_ring.try_claim());
  retried_second_claim.value() = 20;
  retried_second_claim.publish();

  auto first_upstream = require_token(first_ring.try_observe_upstream());
  auto second_upstream = require_token(second_ring.try_observe_upstream());
  auto moved_upstream = std::move(second_upstream);
  first_upstream = std::move(moved_upstream);

  auto retried_first_upstream = require_token(first_ring.try_observe_upstream());
  CHECK(retried_first_upstream.value() == 10);
  retried_first_upstream.release();
  first_upstream.cancel();
  auto retried_second_upstream = require_token(second_ring.try_observe_upstream());
  CHECK(retried_second_upstream.value() == 20);
  retried_second_upstream.release();
}

TEST_CASE("sequence pipeline retains resources until producer reuse") {
  handoff::sequence::BoundedSequencePipeline<std::shared_ptr<int>, 1> ring;
  auto first = std::make_shared<int>(10);

  auto claim = require_token(ring.try_claim());
  claim.value() = first;
  claim.publish();
  CHECK(first.use_count() == 2);

  auto upstream = require_token(ring.try_observe_upstream());
  REQUIRE(upstream.value());
  CHECK(*upstream.value() == 10);
  upstream.release();
  auto downstream = require_token(ring.try_observe_downstream());
  REQUIRE(downstream.value());
  CHECK(*downstream.value() == 10);
  downstream.release();
  CHECK(first.use_count() == 2);

  auto second = std::make_shared<int>(20);
  claim = require_token(ring.try_claim());
  claim.value() = second;
  CHECK(first.use_count() == 1);
  CHECK(second.use_count() == 2);
  claim.publish();
}

TEST_CASE("sequence pipeline rejects progress after the finite sequence limit") {
  using Ring = handoff::sequence::BoundedSequencePipeline<std::uint64_t, 3, std::uint8_t>;
  Ring ring;

  for (unsigned int expected = 1; expected <= Ring::sequence_limit(); ++expected) {
    auto claim = require_token(ring.try_claim());
    CHECK(static_cast<unsigned int>(claim.sequence()) == expected);
    claim.value() = expected;
    claim.publish();

    auto upstream = require_token(ring.try_observe_upstream());
    CHECK(static_cast<unsigned int>(upstream.sequence()) == expected);
    CHECK(upstream.value() == expected);
    upstream.release();
    auto downstream = require_token(ring.try_observe_downstream());
    CHECK(static_cast<unsigned int>(downstream.sequence()) == expected);
    CHECK(downstream.value() == expected);
    downstream.release();
  }

  CHECK(ring.published_sequence() == Ring::sequence_limit());
  CHECK(ring.upstream_sequence() == Ring::sequence_limit());
  CHECK(ring.downstream_sequence() == Ring::sequence_limit());
  CHECK_FALSE(ring.try_claim());
  CHECK_FALSE(ring.try_observe_upstream());
  CHECK_FALSE(ring.try_observe_downstream());
}

TEST_CASE("sequence pipeline preserves concurrent stage and payload integrity") {
  constexpr std::uint64_t message_count = 200'000;
  handoff::sequence::BoundedSequencePipeline<Message, 63> ring;
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

  std::thread upstream([&] {
    for (std::uint64_t expected = 1; expected <= message_count; ++expected) {
      if (expected % 5 == 0) {
        std::this_thread::yield();
      }
      auto observation = ring.try_observe_upstream();
      while (!observation) {
        std::this_thread::yield();
        observation = ring.try_observe_upstream();
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

  std::thread downstream([&] {
    for (std::uint64_t expected = 1; expected <= message_count; ++expected) {
      if (expected % 7 == 0) {
        std::this_thread::yield();
      }
      auto observation = ring.try_observe_downstream();
      while (!observation) {
        std::this_thread::yield();
        observation = ring.try_observe_downstream();
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
  upstream.join();
  downstream.join();
  CHECK(valid.load(std::memory_order_relaxed));
}
