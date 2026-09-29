#include "handoff/mpsc/variable_record_ring.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

template <typename Token> Token required(std::optional<Token> token) {
  if (!token) {
    throw std::runtime_error("required variable-record token was unavailable");
  }
  return std::move(*token);
}

template <typename Claim> void fill(Claim& claim) {
  const auto position = claim.position();
  auto payload = claim.payload();
  for (std::size_t index = 0; index < payload.size(); ++index) {
    payload[index] = std::byte{static_cast<unsigned char>((position + index * 29U) & 0xffU)};
  }
}

template <typename Observation>
bool matches(const Observation& observation, std::uint64_t position, std::size_t length,
             std::uint32_t type_tag) {
  if (observation.position() != position || observation.header().sequence != position ||
      observation.header().payload_length != length || observation.header().type_tag != type_tag ||
      observation.payload().size() != length) {
    return false;
  }
  for (std::size_t index = 0; index < length; ++index) {
    if (observation.payload()[index] !=
        std::byte{static_cast<unsigned char>((position + index * 29U) & 0xffU)}) {
      return false;
    }
  }
  return true;
}

} // namespace

TEST_CASE("MPSC variable records expose separate native capacities", "[mpsc][record]") {
  using Ring = handoff::mpsc::VariableRecordRing<4, 128>;
  STATIC_CHECK(Ring::descriptor_capacity() == 4);
  STATIC_CHECK(Ring::payload_byte_capacity() == 128);
  STATIC_CHECK(Ring::maximum_payload_size() == 64);
  STATIC_CHECK(Ring::payload_footprint(0) == 0);
  STATIC_CHECK(Ring::payload_footprint(1) == 16);
  STATIC_CHECK(Ring::payload_footprint(17) == 32);
  STATIC_CHECK_FALSE(Ring::payload_footprint(65).has_value());
  STATIC_CHECK_FALSE(Ring::payload_footprint(std::numeric_limits<std::size_t>::max()).has_value());

  Ring ring;
  CHECK_FALSE(ring.try_claim(65));
  CHECK_FALSE(ring.try_observe());
  auto claim = required(ring.try_claim(64));
  CHECK(claim.position() == 0);
  fill(claim);
  claim.publish(7);
  auto observation = required(ring.try_observe());
  CHECK(matches(observation, 0, 64, 7));
  observation.release();
}

TEST_CASE("zero-byte records exhaust descriptors without consuming byte credit", "[mpsc][record]") {
  using Ring = handoff::mpsc::VariableRecordRing<2, 64>;
  Ring ring;
  auto first = required(ring.try_claim(0));
  auto second = required(ring.try_claim(0));
  CHECK(first.payload().empty());
  CHECK(second.payload().empty());
  CHECK_FALSE(ring.try_claim(0));
  second.publish(12);
  CHECK_FALSE(ring.try_observe());
  first.publish(11);

  auto observation = required(ring.try_observe());
  CHECK(matches(observation, 0, 0, 11));
  observation.release();
  auto third = required(ring.try_claim(0));
  CHECK(third.position() == 2);
  third.publish(13);
  observation = required(ring.try_observe());
  CHECK(matches(observation, 1, 0, 12));
  observation.release();
  observation = required(ring.try_observe());
  CHECK(matches(observation, 2, 0, 13));
  observation.release();
}

TEST_CASE("byte credit returns only after the final consumer read", "[mpsc][record]") {
  using Ring = handoff::mpsc::VariableRecordRing<4, 64>;
  Ring ring;
  for (std::uint32_t position = 0; position < 2; ++position) {
    auto claim = required(ring.try_claim(32));
    fill(claim);
    claim.publish(position);
  }
  CHECK(ring.try_claim(1) == std::nullopt);

  {
    auto observation = required(ring.try_observe());
    CHECK(matches(observation, 0, 32, 0));
    CHECK_FALSE(ring.try_claim(32));
  }
  auto first = required(ring.try_observe());
  CHECK(matches(first, 0, 32, 0));
  first.release();

  auto wrapped = required(ring.try_claim(32));
  CHECK(wrapped.position() == 2);
  fill(wrapped);
  wrapped.publish(2);
  for (std::uint64_t position = 1; position <= 2; ++position) {
    auto observation = required(ring.try_observe());
    CHECK(matches(observation, position, 32, static_cast<std::uint32_t>(position)));
    observation.release();
  }
}

TEST_CASE("a wrapped byte gap belongs to its claim across a publication hole", "[mpsc][record]") {
  using Ring = handoff::mpsc::VariableRecordRing<8, 128>;
  Ring ring;
  for (std::uint32_t position = 0; position < 7; ++position) {
    auto claim = required(ring.try_claim(16));
    fill(claim);
    claim.publish(position);
    auto observation = required(ring.try_observe());
    CHECK(matches(observation, position, 16, position));
    observation.release();
  }

  auto delayed = required(ring.try_claim(32)); // 16-byte end gap plus 32 payload bytes.
  CHECK(delayed.position() == 7);
  fill(delayed);
  std::thread later([&] {
    auto claim = required(ring.try_claim(16));
    fill(claim);
    claim.publish(8);
  });
  later.join();
  CHECK_FALSE(ring.try_observe());

  auto newer = required(ring.try_claim(64));
  fill(newer);
  CHECK_FALSE(ring.try_claim(32));
  delayed.publish(7);
  auto first = required(ring.try_observe());
  CHECK(matches(first, 7, 32, 7));
  first.release();

  // The first release returns its gap and payload, while the newer claim stays unfinished.
  auto recycled = required(ring.try_claim(32));
  CHECK(recycled.position() == 10);
  fill(recycled);
  recycled.publish(10);
  auto second = required(ring.try_observe());
  CHECK(matches(second, 8, 16, 8));
  second.release();
  CHECK_FALSE(ring.try_observe());
  newer.publish(9);
  for (std::uint64_t position = 9; position <= 10; ++position) {
    auto observation = required(ring.try_observe());
    const std::size_t length = position == 9 ? 64 : 32;
    CHECK(matches(observation, position, length, static_cast<std::uint32_t>(position)));
    observation.release();
  }
}

TEST_CASE("MPSC variable record positions stop at the finite limit", "[mpsc][record]") {
  using Ring = handoff::mpsc::VariableRecordRing<3, 64, std::uint8_t>;
  Ring ring;
  for (unsigned int position = 0; position < Ring::position_limit(); ++position) {
    auto claim = required(ring.try_claim(position % 2));
    CHECK(claim.position() == position);
    fill(claim);
    claim.publish(42);
    auto observation = required(ring.try_observe());
    CHECK(matches(observation, position, position % 2, 42));
    observation.release();
  }
  CHECK_FALSE(ring.try_claim(0));
  CHECK_FALSE(ring.try_observe());
}

TEST_CASE("two producers preserve mixed-length byte integrity across repeated reuse",
          "[mpsc][record]") {
  using Ring = handoff::mpsc::VariableRecordRing<32, 1024>;
  constexpr std::array<std::size_t, 9> lengths{0, 1, 15, 16, 17, 31, 32, 63, 64};
  constexpr std::uint64_t per_producer = 20'000;
  Ring ring;
  std::atomic<bool> valid{true};
  std::vector<std::thread> producers;
  producers.reserve(2);
  for (std::uint32_t producer = 0; producer < 2; ++producer) {
    producers.emplace_back([&, producer] {
      for (std::uint64_t index = 0; index < per_producer; ++index) {
        const auto length = lengths[(index + producer) % lengths.size()];
        auto claim = ring.try_claim(length);
        while (!claim) {
          std::this_thread::yield();
          claim = ring.try_claim(length);
        }
        auto token = std::move(*claim);
        fill(token);
        token.publish(producer);
      }
    });
  }
  std::thread consumer([&] {
    for (std::uint64_t position = 0; position < 2 * per_producer; ++position) {
      auto observation = ring.try_observe();
      while (!observation) {
        std::this_thread::yield();
        observation = ring.try_observe();
      }
      const auto length = observation->header().payload_length;
      if (observation->header().type_tag > 1 ||
          std::find(lengths.begin(), lengths.end(), length) == lengths.end() ||
          !matches(*observation, position, observation->header().payload_length,
                   observation->header().type_tag)) {
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
