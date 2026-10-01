#include "handoff/descriptor/descriptor_payload_ring.hpp"
#include "handoff/record/fixed_record_ring.hpp"
#include "handoff/record/variable_record_ring.hpp"
#include "handoff/spsc/basic_bounded_ring.hpp"
#include "handoff/spsc/batch_bounded_ring.hpp"
#include "handoff/spsc/bulk_burst_bounded_ring.hpp"
#include "handoff/spsc/cache_line_bounded_ring.hpp"
#include "handoff/spsc/cached_index_bounded_ring.hpp"
#include "handoff/spsc/staged_bounded_ring.hpp"
#include "handoff/topology/two_path_merge.hpp"

#include <array>
#include <cstddef>
#include <limits>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

// Seed only an empty, quiescent ring. The operations below use the production
// size_t counters and cross their actual machine rollover, not just slot wrap.
namespace handoff::spsc {
struct CounterTestAccess {
  template <typename Ring> static void seed_empty(Ring& ring, std::size_t position) {
    if constexpr (requires { ring.consumer_; }) {
      ring.consumer_.head.store(position);
      ring.producer_.tail.store(position);
    } else {
      ring.head_.store(position);
      ring.tail_.store(position);
    }
    if constexpr (requires { ring.cached_head_; }) {
      ring.cached_head_ = position;
      ring.cached_tail_ = position;
    }
  }
};
} // namespace handoff::spsc

namespace handoff::record {
struct CounterTestAccess {
  template <typename Ring> static void seed_empty(Ring& ring, std::size_t position) {
    ring.head_.store(position);
    ring.tail_.store(position);
  }
};
} // namespace handoff::record

namespace handoff::descriptor {
struct CounterTestAccess {
  template <typename Ring>
  static void seed_empty(Ring& ring, std::size_t descriptor_position, std::size_t byte_position) {
    ring.descriptor_head_.store(descriptor_position);
    ring.descriptor_tail_.store(descriptor_position);
    ring.payload_head_.store(byte_position);
    ring.payload_tail_ = byte_position;
  }
};
} // namespace handoff::descriptor

namespace {
constexpr auto maximum = std::numeric_limits<std::size_t>::max();
using Basic = handoff::spsc::BasicBoundedRing<int, 4>;
using CacheLine = handoff::spsc::CacheLineBoundedRing<int, 4>;
using CachedIndex = handoff::spsc::CachedIndexBoundedRing<int, 4>;
using Batch = handoff::spsc::BatchBoundedRing<int, 4>;
using BulkBurst = handoff::spsc::BulkBurstBoundedRing<int, 4>;

template <template <typename, std::size_t> typename Ring, std::size_t Capacity>
concept CapacitySupported = requires { typename Ring<int, Capacity>; };

template <std::size_t Capacity>
concept FixedCapacitySupported =
    requires { typename handoff::record::FixedRecordRing<8, Capacity>; };

template <std::size_t Capacity>
concept DescriptorCapacitySupported =
    requires { typename handoff::descriptor::DescriptorPayloadRing<Capacity, 128>; };

template <std::size_t Capacity>
concept WrappingSlotsSupported = requires {
  typename handoff::spsc::BasicBoundedRing<int, Capacity>;
  typename handoff::spsc::CacheLineBoundedRing<int, Capacity>;
  typename handoff::spsc::CachedIndexBoundedRing<int, Capacity>;
  typename handoff::spsc::BatchBoundedRing<int, Capacity>;
  typename handoff::spsc::BulkBurstBoundedRing<int, Capacity>;
  typename handoff::spsc::StagedBoundedRing<int, Capacity>;
  typename handoff::record::FixedRecordRing<8, Capacity>;
  typename handoff::descriptor::DescriptorPayloadRing<Capacity, 128>;
  typename handoff::topology::TwoPathMerge<int, Capacity>;
};
} // namespace

TEST_CASE("wrapping slot capacities preserve physical mapping at machine rollover", "[rollover]") {
  STATIC_CHECK(WrappingSlotsSupported<1>);
  STATIC_CHECK(WrappingSlotsSupported<4>);
  STATIC_CHECK_FALSE(WrappingSlotsSupported<0>);
  STATIC_CHECK_FALSE(WrappingSlotsSupported<3>);
  STATIC_CHECK_FALSE(CapacitySupported<handoff::spsc::BasicBoundedRing, 3>);
  STATIC_CHECK_FALSE(CapacitySupported<handoff::spsc::CacheLineBoundedRing, 3>);
  STATIC_CHECK_FALSE(CapacitySupported<handoff::spsc::CachedIndexBoundedRing, 3>);
  STATIC_CHECK_FALSE(CapacitySupported<handoff::spsc::BatchBoundedRing, 3>);
  STATIC_CHECK_FALSE(CapacitySupported<handoff::spsc::BulkBurstBoundedRing, 3>);
  STATIC_CHECK_FALSE(CapacitySupported<handoff::spsc::StagedBoundedRing, 3>);
  STATIC_CHECK_FALSE(CapacitySupported<handoff::topology::TwoPathMerge, 3>);
  STATIC_CHECK_FALSE(FixedCapacitySupported<3>);
  STATIC_CHECK_FALSE(DescriptorCapacitySupported<3>);
  STATIC_CHECK(maximum % 3 == 0);
  STATIC_CHECK(std::size_t{0} % 3 == 0);
  STATIC_CHECK(maximum % 4 == 3);
  STATIC_CHECK(std::size_t{0} % 4 == 0);
}

TEMPLATE_TEST_CASE("scalar SPSC FIFO and reuse cross machine rollover", "[rollover]", Basic,
                   CacheLine, CachedIndex, Batch, BulkBurst) {
  TestType ring;
  handoff::spsc::CounterTestAccess::seed_empty(ring, maximum - 1);
  int value = -1;
  CHECK_FALSE(ring.try_pop(value));
  for (int input = 10; input < 14; ++input) {
    REQUIRE(ring.try_push(input));
  }
  CHECK_FALSE(ring.try_push(99));
  REQUIRE(ring.try_pop(value));
  CHECK(value == 10);
  REQUIRE(ring.try_push(14));
  CHECK_FALSE(ring.try_push(99));
  for (int expected = 11; expected < 15; ++expected) {
    REQUIRE(ring.try_pop(value));
    CHECK(value == expected);
  }
  CHECK_FALSE(ring.try_pop(value));
}

TEST_CASE("grouped SPSC publication crosses machine rollover", "[rollover]") {
  const std::array inputs{10, 11, 12};
  std::array<int, 3> output{};
  SECTION("batch") {
    Batch ring;
    handoff::spsc::CounterTestAccess::seed_empty(ring, maximum - 1);
    REQUIRE(ring.try_push_batch(inputs));
    CHECK_FALSE(ring.try_push_batch(inputs));
    REQUIRE(ring.try_pop_batch(output));
    CHECK(output == inputs);
  }
  SECTION("bulk and burst") {
    BulkBurst ring;
    handoff::spsc::CounterTestAccess::seed_empty(ring, maximum - 1);
    REQUIRE(ring.try_push_bulk(inputs));
    CHECK(ring.try_push_burst(inputs) == 1);
    REQUIRE(ring.try_pop_burst(output) == 3);
    CHECK(output == inputs);
    int value = 0;
    REQUIRE(ring.try_pop(value));
    CHECK(value == inputs.front());
    REQUIRE(ring.try_push_bulk(inputs));
    REQUIRE(ring.try_pop_bulk(output));
    CHECK(output == inputs);
  }
}

TEST_CASE("staged spans and cancelled claims cross machine rollover", "[rollover]") {
  handoff::spsc::StagedBoundedRing<int, 4> ring;
  handoff::spsc::CounterTestAccess::seed_empty(ring, maximum - 1);
  auto claim = ring.try_reserve_push(3);
  REQUIRE(claim);
  REQUIRE(claim->first().size() == 2);
  REQUIRE(claim->second().size() == 1);
  claim->first()[0] = 10;
  claim->first()[1] = 11;
  claim->second()[0] = 12;
  CHECK_FALSE(ring.try_reserve_pop(1));
  claim->cancel();
  CHECK_FALSE(ring.try_reserve_pop(1));
  claim = ring.try_reserve_push(3);
  REQUIRE(claim);
  claim->finish();
  auto observation = ring.try_reserve_pop(3);
  REQUIRE(observation);
  REQUIRE(observation->first().size() == 2);
  REQUIRE(observation->second().size() == 1);
  CHECK(observation->first()[0] == 10);
  CHECK(observation->first()[1] == 11);
  CHECK(observation->second()[0] == 12);
  CHECK_FALSE(ring.try_reserve_push(2));
  observation->cancel();
  observation = ring.try_reserve_pop(3);
  REQUIRE(observation);
  observation->finish();
  CHECK_FALSE(ring.try_reserve_pop(1));
  REQUIRE(ring.try_reserve_push(4));
}

TEST_CASE("fixed-record slots cross machine rollover", "[rollover]") {
  using Ring = handoff::record::FixedRecordRing<8, 4>;
  Ring ring;
  handoff::record::CounterTestAccess::seed_empty(ring, maximum - 1);
  for (unsigned int sequence = 0; sequence < 4; ++sequence) {
    Ring::value_type record{.header = {.sequence = sequence, .type_tag = 7, .payload_length = 1}};
    record.payload[0] = static_cast<std::byte>(sequence);
    REQUIRE(ring.try_push(record));
  }
  CHECK_FALSE(ring.try_push({}));
  Ring::value_type output;
  for (unsigned int sequence = 0; sequence < 4; ++sequence) {
    REQUIRE(ring.try_pop(output));
    CHECK(output.header.sequence == sequence);
    CHECK(output.payload[0] == static_cast<std::byte>(sequence));
  }
  CHECK_FALSE(ring.try_pop(output));
  REQUIRE(ring.try_push({}));
}

TEST_CASE("masked byte storage and padding cross machine rollover", "[rollover]") {
  using Ring = handoff::record::VariableRecordRing<128>;
  Ring ring;
  handoff::record::CounterTestAccess::seed_empty(ring, maximum - 31);
  const std::array<std::byte, 17> payload{std::byte{42}};
  const handoff::record::RecordHeader header{.sequence = 7, .type_tag = 9, .payload_length = 17};
  REQUIRE(ring.try_push(header, payload) == Ring::PushResult::success);
  handoff::record::RecordHeader output_header;
  CHECK(ring.try_pop(output_header, {}) == Ring::PopResult::output_too_small);
  std::array<std::byte, 17> output{};
  REQUIRE(ring.try_pop(output_header, output) == Ring::PopResult::success);
  CHECK(output_header.sequence == header.sequence);
  CHECK(output == payload);
  CHECK(ring.try_pop(output_header, output) == Ring::PopResult::empty);
  REQUIRE(ring.try_push(header, payload) == Ring::PushResult::success);
}

TEST_CASE("descriptor slots and masked payload credits cross machine rollover", "[rollover]") {
  using Ring = handoff::descriptor::DescriptorPayloadRing<4, 128>;
  Ring ring;
  handoff::descriptor::CounterTestAccess::seed_empty(ring, maximum - 1, maximum - 15);
  const std::array<std::byte, 17> payload{std::byte{42}};
  for (unsigned int sequence = 0; sequence < 3; ++sequence) {
    const handoff::record::RecordHeader header{
        .sequence = sequence, .type_tag = 9, .payload_length = 17};
    REQUIRE(ring.try_push(header, payload) == Ring::PushResult::success);
  }
  REQUIRE(ring.try_push({.sequence = 3, .type_tag = 9, .payload_length = 0}, {}) ==
          Ring::PushResult::success);
  CHECK(ring.try_push({}, {}) == Ring::PushResult::full);
  handoff::record::RecordHeader output_header;
  CHECK(ring.try_pop(output_header, {}) == Ring::PopResult::output_too_small);
  CHECK(ring.try_push({}, {}) == Ring::PushResult::full);
  std::array<std::byte, 17> output{};
  for (unsigned int sequence = 0; sequence < 4; ++sequence) {
    REQUIRE(ring.try_pop(output_header, output) == Ring::PopResult::success);
    CHECK(output_header.sequence == sequence);
    if (sequence < 3) {
      CHECK(output == payload);
    }
  }
  CHECK(ring.try_pop(output_header, output) == Ring::PopResult::empty);
  REQUIRE(ring.try_push({.sequence = 4, .type_tag = 9, .payload_length = 17}, payload) ==
          Ring::PushResult::success);
  REQUIRE(ring.try_pop(output_header, output) == Ring::PopResult::success);
  CHECK(output_header.sequence == 4);
  CHECK(output == payload);
}
