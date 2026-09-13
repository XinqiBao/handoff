#include "handoff/record/fixed_record_ring.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

namespace {

template <std::size_t PayloadCapacity>
handoff::record::FixedRecord<PayloadCapacity>
make_record(std::uint64_t sequence, std::uint32_t type_tag, std::span<const std::byte> payload) {
  handoff::record::FixedRecord<PayloadCapacity> record;
  record.header = {.sequence = sequence,
                   .type_tag = type_tag,
                   .payload_length = static_cast<std::uint32_t>(payload.size())};
  std::ranges::copy(payload, record.payload.begin());
  return record;
}

template <std::size_t PayloadCapacity>
bool matches(const handoff::record::FixedRecord<PayloadCapacity>& record, std::uint64_t sequence,
             std::uint32_t type_tag, std::span<const std::byte> payload) {
  return record.header.sequence == sequence && record.header.type_tag == type_tag &&
         record.header.payload_length == payload.size() &&
         std::ranges::equal(payload,
                            std::span<const std::byte>(record.payload).first(payload.size()));
}

} // namespace

TEST_CASE("fixed records expose a stable header followed by inline payload") {
  using Header = handoff::record::FixedRecordHeader;
  using Record = handoff::record::FixedRecord<8>;
  using Ring = handoff::record::FixedRecordRing<8, 4>;

  STATIC_CHECK(std::is_standard_layout_v<Header>);
  STATIC_CHECK(std::is_standard_layout_v<Record>);
  STATIC_CHECK(std::is_trivially_copyable_v<Record>);
  STATIC_CHECK(sizeof(Header) == 16);
  STATIC_CHECK(offsetof(Header, sequence) == 0);
  STATIC_CHECK(offsetof(Header, type_tag) == 8);
  STATIC_CHECK(offsetof(Header, payload_length) == 12);
  STATIC_CHECK(offsetof(Record, payload) == sizeof(Header));
  STATIC_CHECK(sizeof(Record) == sizeof(Header) + Record::payload_capacity());

  CHECK(Ring::capacity() == 4);
  CHECK(Ring::payload_capacity() == 8);
}

TEST_CASE("fixed-record ring handles zero, full, and oversized logical payloads") {
  using Ring = handoff::record::FixedRecordRing<4, 2>;
  Ring ring;
  Ring::value_type output{.header = {.sequence = 99, .type_tag = 99, .payload_length = 0}};

  CHECK_FALSE(ring.try_pop(output));
  CHECK(output.header.sequence == 99);

  const auto empty = make_record<4>(1, 10, {});
  const std::array full_payload{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
  const auto full = make_record<4>(2, 20, full_payload);
  auto oversized = full;
  oversized.header.payload_length = 5;

  REQUIRE(ring.try_push(empty));
  CHECK_FALSE(ring.try_push(oversized));
  REQUIRE(ring.try_push(full));
  CHECK_FALSE(ring.try_push(empty));

  REQUIRE(ring.try_pop(output));
  CHECK(matches(output, 1, 10, {}));
  REQUIRE(ring.try_pop(output));
  CHECK(matches(output, 2, 20, full_payload));
  CHECK_FALSE(ring.try_pop(output));
  CHECK(matches(output, 2, 20, full_payload));
}

TEST_CASE("fixed-record ring preserves FIFO records through slot wrap") {
  using Ring = handoff::record::FixedRecordRing<8, 3>;
  Ring ring;
  Ring::value_type output;

  for (std::uint64_t cycle = 0; cycle < 1'000; ++cycle) {
    for (std::uint64_t offset = 0; offset < Ring::capacity(); ++offset) {
      const auto sequence = cycle * Ring::capacity() + offset;
      const std::array payload{static_cast<std::byte>(sequence & 0xffU),
                               static_cast<std::byte>((sequence >> 8U) & 0xffU)};
      REQUIRE(ring.try_push(make_record<8>(sequence, 7, payload)));
    }
    CHECK_FALSE(ring.try_push(make_record<8>(0, 0, {})));

    for (std::uint64_t offset = 0; offset < Ring::capacity(); ++offset) {
      const auto sequence = cycle * Ring::capacity() + offset;
      const std::array payload{static_cast<std::byte>(sequence & 0xffU),
                               static_cast<std::byte>((sequence >> 8U) & 0xffU)};
      REQUIRE(ring.try_pop(output));
      CHECK(matches(output, sequence, 7, payload));
    }
  }
}

TEST_CASE("fixed-record ring preserves concurrent header and payload integrity") {
  constexpr std::uint64_t message_count = 500'000;
  using Ring = handoff::record::FixedRecordRing<16, 63>;
  Ring ring;
  std::atomic<bool> valid{true};

  std::thread producer([&] {
    for (std::uint64_t sequence = 0; sequence < message_count; ++sequence) {
      const std::array payload{static_cast<std::byte>(sequence & 0xffU),
                               static_cast<std::byte>((sequence >> 8U) & 0xffU),
                               static_cast<std::byte>((sequence >> 16U) & 0xffU),
                               static_cast<std::byte>((sequence >> 24U) & 0xffU)};
      const auto record = make_record<16>(sequence, 42, payload);
      while (!ring.try_push(record)) {
        std::this_thread::yield();
      }
    }
  });

  std::thread consumer([&] {
    Ring::value_type record;
    for (std::uint64_t sequence = 0; sequence < message_count; ++sequence) {
      while (!ring.try_pop(record)) {
        std::this_thread::yield();
      }
      const std::array payload{static_cast<std::byte>(sequence & 0xffU),
                               static_cast<std::byte>((sequence >> 8U) & 0xffU),
                               static_cast<std::byte>((sequence >> 16U) & 0xffU),
                               static_cast<std::byte>((sequence >> 24U) & 0xffU)};
      if (!matches(record, sequence, 42, payload)) {
        valid.store(false, std::memory_order_relaxed);
      }
    }
  });

  producer.join();
  consumer.join();
  CHECK(valid.load(std::memory_order_relaxed));
}
