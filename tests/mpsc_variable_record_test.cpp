#include "handoff/mpsc/variable_record_ring.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace handoff::mpsc {
struct VariableRecordTestAccess {
  using Ring = VariableRecordRing<4, 128>;

  // Only a fresh, empty, quiescent ring: descriptor ordinals and ready tags stay at zero.
  static void seed_empty_bytes(Ring& ring, std::uint64_t position) {
    assert(position % Ring::payload_alignment == 0);
    assert(ring.next_claim_ == 0 && ring.next_to_observe_ == 0);
    assert(ring.next_byte_claim_ == 0 && ring.next_byte_release_ == 0);
    assert(ring.released_records_.load() == 0 && ring.released_bytes_.load() == 0);
    assert(!ring.consumer_observing_);
    ring.next_byte_claim_ = position;
    ring.next_byte_release_ = position;
    ring.released_bytes_.store(position);
  }

  static auto cursors(const Ring& ring) {
    return std::array{
        ring.next_claim_,      ring.released_records_.load(), ring.next_to_observe_,
        ring.next_byte_claim_, ring.released_bytes_.load(),   ring.next_byte_release_};
  }
};
} // namespace handoff::mpsc

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

TEST_CASE("MPSC byte exhaustion preserves outstanding records and zero-byte progress",
          "[mpsc][record]") {
  using Access = handoff::mpsc::VariableRecordTestAccess;
  using Ring = Access::Ring;
  constexpr auto last = std::numeric_limits<std::uint64_t>::max() - 15;
  Ring ring;
  Access::seed_empty_bytes(ring, last - 32);
  auto claim = required(ring.try_claim(17)); // Last admissible 32-byte aligned extent.
  CHECK(claim.position() == 0);
  fill(claim);
  claim.publish(7);
  auto held = required(ring.try_observe());
  const auto before = Access::cursors(ring);
  CHECK(before[3] == last);
  CHECK_FALSE(ring.try_claim(1));
  CHECK(Access::cursors(ring) == before);
  CHECK(matches(held, 0, 17, 7));

  auto empty = required(ring.try_claim(0));
  CHECK(empty.position() == 1);
  CHECK(empty.payload().empty());
  empty.publish(8);
  CHECK(Access::cursors(ring)[3] == last);
  CHECK_FALSE(ring.try_observe());
  CHECK(matches(held, 0, 17, 7));
  held.release();
  auto observation = required(ring.try_observe());
  CHECK(matches(observation, 1, 0, 8));
  observation.release();
  CHECK(Access::cursors(ring)[4] == last);

  const auto drained = Access::cursors(ring);
  CHECK_FALSE(ring.try_claim(1)); // Release returns credit but cannot extend the finite stream.
  CHECK(Access::cursors(ring) == drained);
  empty = required(ring.try_claim(0));
  CHECK(empty.position() == 2);
  empty.publish(9);
  observation = required(ring.try_observe());
  CHECK(matches(observation, 2, 0, 9));
  observation.release();
  CHECK_FALSE(ring.try_observe());
}

TEST_CASE("an overflowing MPSC wrap gap reserves neither resource", "[mpsc][record]") {
  using Access = handoff::mpsc::VariableRecordTestAccess;
  using Ring = Access::Ring;
  constexpr auto last = std::numeric_limits<std::uint64_t>::max() - 15;
  Ring ring;
  Access::seed_empty_bytes(ring, last - 48); // Physical offset 64.
  auto first = required(ring.try_claim(17)); // Ends at offset 96, leaving a 32-byte suffix.
  fill(first);
  first.publish(10);
  auto held = required(ring.try_observe());
  const auto before = Access::cursors(ring);
  CHECK_FALSE(ring.try_claim(33)); // 32-byte gap + 48-byte footprint exceeds the byte limit.
  CHECK(Access::cursors(ring) == before);
  CHECK(matches(held, 0, 17, 10));

  // A smaller extent still fits: failed admission consumed no ordinal, descriptor, or byte credit.
  auto smaller = required(ring.try_claim(1));
  CHECK(smaller.position() == 1);
  fill(smaller);
  smaller.publish(11);
  CHECK(Access::cursors(ring)[3] == last);
  CHECK(matches(held, 0, 17, 10));
  held.release();
  auto observation = required(ring.try_observe());
  CHECK(matches(observation, 1, 1, 11));
  observation.release();
  CHECK(Access::cursors(ring)[4] == last);
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
