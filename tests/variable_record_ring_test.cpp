#include "handoff/record/variable_record_ring.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <thread>

#include <catch2/catch_test_macros.hpp>

namespace {

using handoff::record::RecordHeader;

template <std::size_t Size>
std::array<std::byte, Size> make_payload(std::uint64_t sequence, std::size_t length) {
  std::array<std::byte, Size> payload{};
  for (std::size_t index = 0; index < length; ++index) {
    payload[index] = static_cast<std::byte>((sequence + index * 17U) & 0xffU);
  }
  return payload;
}

template <typename Ring, std::size_t Size>
typename Ring::PushResult push(Ring& ring, std::uint64_t sequence, std::uint32_t type_tag,
                               const std::array<std::byte, Size>& payload, std::size_t length) {
  const RecordHeader header{.sequence = sequence,
                            .type_tag = type_tag,
                            .payload_length = static_cast<std::uint32_t>(length)};
  return ring.try_push(header, std::span<const std::byte>(payload).first(length));
}

template <std::size_t Size>
bool matches(const RecordHeader& header, std::span<const std::byte> payload, std::uint64_t sequence,
             std::uint32_t type_tag, const std::array<std::byte, Size>& expected,
             std::size_t expected_length) {
  return header.sequence == sequence && header.type_tag == type_tag &&
         header.payload_length == expected_length &&
         std::ranges::equal(payload.first(expected_length),
                            std::span<const std::byte>(expected).first(expected_length));
}

} // namespace

TEST_CASE("variable-record ring exposes bounded aligned byte accounting") {
  using Ring = handoff::record::VariableRecordRing<128>;

  STATIC_CHECK(Ring::byte_capacity() == 128);
  STATIC_CHECK(Ring::record_alignment == 16);
  STATIC_CHECK(Ring::maximum_payload_size() == 48);
  STATIC_CHECK(Ring::record_footprint(0) == 16);
  STATIC_CHECK(Ring::record_footprint(1) == 32);
  STATIC_CHECK(Ring::record_footprint(16) == 32);
  STATIC_CHECK(Ring::record_footprint(17) == 48);
  STATIC_CHECK_FALSE(Ring::record_footprint(Ring::maximum_payload_size() + 1).has_value());
  STATIC_CHECK_FALSE(Ring::record_footprint(std::numeric_limits<std::size_t>::max()).has_value());
}

TEST_CASE("variable-record ring rejects invalid input and preserves failed output") {
  using Ring = handoff::record::VariableRecordRing<64>;
  Ring ring;
  RecordHeader output{.sequence = 99, .type_tag = 98, .payload_length = 97};
  std::array<std::byte, 16> output_payload;
  output_payload.fill(std::byte{0x5a});
  const auto original_output_payload = output_payload;

  CHECK(ring.try_pop(output, output_payload) == Ring::PopResult::empty);
  CHECK(output.sequence == 99);
  CHECK(std::ranges::equal(output_payload, original_output_payload));

  const auto payload = make_payload<17>(1, 17);
  const RecordHeader reserved_type{
      .sequence = 1, .type_tag = Ring::padding_type_tag, .payload_length = 1};
  const RecordHeader wrong_length{.sequence = 1, .type_tag = 2, .payload_length = 2};
  const RecordHeader too_large{.sequence = 1, .type_tag = 2, .payload_length = 17};
  CHECK(ring.try_push(reserved_type, std::span<const std::byte>(payload).first(1)) ==
        Ring::PushResult::invalid_record);
  CHECK(ring.try_push(wrong_length, std::span<const std::byte>(payload).first(1)) ==
        Ring::PushResult::invalid_record);
  CHECK(ring.try_push(too_large, payload) == Ring::PushResult::invalid_record);

  REQUIRE(push(ring, 7, 3, payload, 8) == Ring::PushResult::success);
  CHECK(ring.try_pop(output, std::span<std::byte>(output_payload).first(7)) ==
        Ring::PopResult::output_too_small);
  CHECK(output.sequence == 99);
  CHECK(std::ranges::equal(output_payload, original_output_payload));
  REQUIRE(ring.try_pop(output, output_payload) == Ring::PopResult::success);
  CHECK(matches(output, output_payload, 7, 3, payload, 8));
  CHECK(std::to_integer<unsigned int>(output_payload[8]) == 0x5aU);
}

TEST_CASE("variable-record ring uses exact byte capacity without an empty slot") {
  using Ring = handoff::record::VariableRecordRing<64>;
  Ring ring;
  const auto first = make_payload<16>(1, 16);
  const auto second = make_payload<16>(2, 16);

  REQUIRE(push(ring, 1, 10, first, first.size()) == Ring::PushResult::success);
  REQUIRE(push(ring, 2, 20, second, second.size()) == Ring::PushResult::success);
  CHECK(push(ring, 3, 30, first, 0) == Ring::PushResult::full);

  RecordHeader output;
  std::array<std::byte, 16> output_payload{};
  REQUIRE(ring.try_pop(output, output_payload) == Ring::PopResult::success);
  CHECK(matches(output, output_payload, 1, 10, first, first.size()));
  REQUIRE(ring.try_pop(output, output_payload) == Ring::PopResult::success);
  CHECK(matches(output, output_payload, 2, 20, second, second.size()));
  CHECK(ring.try_pop(output, output_payload) == Ring::PopResult::empty);
}

TEST_CASE("variable-record ring transfers a zero-length payload") {
  using Ring = handoff::record::VariableRecordRing<64>;
  Ring ring;
  const std::array<std::byte, 1> payload{std::byte{0x5a}};

  REQUIRE(push(ring, 7, 11, payload, 0) == Ring::PushResult::success);

  RecordHeader output;
  std::array<std::byte, 1> output_payload{std::byte{0xa5}};
  REQUIRE(ring.try_pop(output, output_payload) == Ring::PopResult::success);
  CHECK(matches(output, output_payload, 7, 11, payload, 0));
  CHECK(std::to_integer<unsigned int>(output_payload[0]) == 0xa5U);
}

TEST_CASE("variable-record ring publishes padding only with the wrapped record") {
  using Ring = handoff::record::VariableRecordRing<128>;
  Ring ring;
  const auto payload = make_payload<48>(1, 16);

  for (std::uint64_t sequence = 1; sequence <= 3; ++sequence) {
    REQUIRE(push(ring, sequence, 7, payload, 16) == Ring::PushResult::success);
  }
  CHECK(push(ring, 4, 7, payload, 48) == Ring::PushResult::full);

  RecordHeader output;
  std::array<std::byte, 48> output_payload{};
  REQUIRE(ring.try_pop(output, output_payload) == Ring::PopResult::success);
  CHECK(output.sequence == 1);
  CHECK(push(ring, 4, 7, payload, 48) == Ring::PushResult::full);
  REQUIRE(ring.try_pop(output, output_payload) == Ring::PopResult::success);
  CHECK(output.sequence == 2);

  REQUIRE(push(ring, 4, 7, payload, 48) == Ring::PushResult::success);
  REQUIRE(ring.try_pop(output, output_payload) == Ring::PopResult::success);
  CHECK(output.sequence == 3);
  const auto output_before_failure = output;
  const auto payload_before_failure = output_payload;
  CHECK(ring.try_pop(output, std::span<std::byte>(output_payload).first(47)) ==
        Ring::PopResult::output_too_small);
  CHECK(output.sequence == output_before_failure.sequence);
  CHECK(std::ranges::equal(output_payload, payload_before_failure));
  REQUIRE(ring.try_pop(output, output_payload) == Ring::PopResult::success);
  CHECK(matches(output, output_payload, 4, 7, payload, 48));
  CHECK(ring.try_pop(output, output_payload) == Ring::PopResult::empty);
}

TEST_CASE("variable-record ring preserves concurrent mixed-length records") {
  constexpr std::uint64_t message_count = 300'000;
  constexpr std::array<std::size_t, 10> lengths{0, 1, 15, 16, 17, 31, 32, 64, 127, 256};
  using Ring = handoff::record::VariableRecordRing<4'096>;
  Ring ring;
  std::atomic<bool> valid{true};

  std::thread producer([&] {
    for (std::uint64_t sequence = 0; sequence < message_count; ++sequence) {
      const auto length = lengths[sequence % lengths.size()];
      const auto payload = make_payload<256>(sequence, length);
      auto result = push(ring, sequence, 42, payload, length);
      while (result == Ring::PushResult::full) {
        std::this_thread::yield();
        result = push(ring, sequence, 42, payload, length);
      }
      if (result != Ring::PushResult::success) {
        valid.store(false, std::memory_order_relaxed);
        return;
      }
    }
  });

  std::thread consumer([&] {
    RecordHeader header;
    std::array<std::byte, 256> payload{};
    for (std::uint64_t sequence = 0; sequence < message_count; ++sequence) {
      auto result = ring.try_pop(header, payload);
      while (result == Ring::PopResult::empty) {
        std::this_thread::yield();
        result = ring.try_pop(header, payload);
      }
      if (result != Ring::PopResult::success) {
        valid.store(false, std::memory_order_relaxed);
        return;
      }
      const auto length = lengths[sequence % lengths.size()];
      const auto expected = make_payload<256>(sequence, length);
      if (!matches(header, payload, sequence, 42, expected, length)) {
        valid.store(false, std::memory_order_relaxed);
      }
    }
  });

  producer.join();
  consumer.join();
  CHECK(valid.load(std::memory_order_relaxed));
}
