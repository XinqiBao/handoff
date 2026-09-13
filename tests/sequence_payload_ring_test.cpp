#include "handoff/metadata/sequence_payload_ring.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>

#include <catch2/catch_test_macros.hpp>

namespace {

using Metadata = handoff::metadata::Metadata;

template <std::size_t Size>
constexpr std::array<std::byte, Size> payload_for(std::uint64_t sequence) {
  std::array<std::byte, Size> payload{};
  for (std::size_t index = 0; index < payload.size(); ++index) {
    payload[index] = std::byte{static_cast<unsigned char>((sequence + index) & 0xffU)};
  }
  return payload;
}

constexpr std::uint32_t control_for(std::uint64_t sequence) {
  return static_cast<std::uint32_t>(sequence ^ 0xa5a5a5a5U);
}

} // namespace

TEST_CASE("sequence payload ring exposes exact chunk storage and starts empty") {
  using Ring = handoff::metadata::SequencePayloadRing<4, 16>;
  Ring ring;
  Metadata metadata{.signature = 99, .chunk = 99, .length = 99, .control = 99};
  auto payload = payload_for<16>(99);

  STATIC_CHECK(Ring::capacity() == 4);
  STATIC_CHECK(Ring::chunk_size() == 16);
  STATIC_CHECK(Ring::payload_byte_capacity() == 64);
  CHECK(ring.published_sequence() == 0);
  CHECK(ring.available_range().empty());
  CHECK(ring.try_read(1, metadata, payload) == Ring::ReadResult::not_yet_published);
  CHECK(metadata.signature == 99);
  CHECK(std::ranges::equal(payload, payload_for<16>(99)));
  CHECK(ring.try_read(0, metadata, payload) == Ring::ReadResult::invalid_sequence);
  CHECK(ring.try_read(Ring::sequence_limit() + 1, metadata, payload) ==
        Ring::ReadResult::invalid_sequence);
}

TEST_CASE("sequence payload ring copies zero and maximum payloads from direct chunks") {
  using Ring = handoff::metadata::SequencePayloadRing<4, 16>;
  Ring ring;

  const std::array<std::byte, 0> empty{};
  CHECK(ring.try_publish(11, 21, empty) == Ring::PublishResult::success);

  Metadata metadata{};
  auto output = payload_for<16>(99);
  CHECK(ring.try_read(1, metadata, output) == Ring::ReadResult::success);
  CHECK(metadata == Metadata{.signature = 11, .chunk = 0, .length = 0, .control = 21});
  CHECK(std::ranges::equal(output, payload_for<16>(99)));

  const auto maximum = payload_for<16>(2);
  CHECK(ring.try_publish(12, 22, maximum) == Ring::PublishResult::success);
  output.fill(std::byte{0});
  CHECK(ring.try_read(2, metadata, output) == Ring::ReadResult::success);
  CHECK(metadata == Metadata{.signature = 12, .chunk = 1, .length = 16, .control = 22});
  CHECK(std::ranges::equal(output, maximum));
}

TEST_CASE("sequence payload ring rejects oversized input without advancing") {
  using Ring = handoff::metadata::SequencePayloadRing<4, 8>;
  Ring ring;
  const auto oversized = payload_for<9>(1);

  CHECK(ring.try_publish(1, 2, oversized) == Ring::PublishResult::payload_too_large);
  CHECK(ring.published_sequence() == 0);
  CHECK(ring.available_range().empty());

  const auto valid = payload_for<8>(1);
  CHECK(ring.try_publish(1, 2, valid) == Ring::PublishResult::success);
  CHECK(ring.published_sequence() == 1);
}

TEST_CASE("sequence payload ring preserves undersized and overwritten output") {
  using Ring = handoff::metadata::SequencePayloadRing<2, 8>;
  Ring ring;
  const auto first = payload_for<8>(1);
  CHECK(ring.try_publish(1, control_for(1), first) == Ring::PublishResult::success);

  Metadata metadata{.signature = 99, .chunk = 99, .length = 99, .control = 99};
  auto short_output = payload_for<7>(99);
  CHECK(ring.try_read(1, metadata, short_output) == Ring::ReadResult::output_too_small);
  CHECK(metadata.signature == 99);
  CHECK(std::ranges::equal(short_output, payload_for<7>(99)));

  CHECK(ring.try_publish(2, control_for(2), payload_for<1>(2)) == Ring::PublishResult::success);
  CHECK(ring.try_publish(3, control_for(3), payload_for<2>(3)) == Ring::PublishResult::success);
  CHECK(ring.try_read(1, metadata, short_output) == Ring::ReadResult::overwritten);
  CHECK(metadata.signature == 99);
  CHECK(std::ranges::equal(short_output, payload_for<7>(99)));

  auto output = payload_for<8>(99);
  const auto initial_output = output;
  CHECK(ring.try_read(3, metadata, output) == Ring::ReadResult::success);
  CHECK(metadata == Metadata{.signature = 3, .chunk = 0, .length = 2, .control = control_for(3)});
  CHECK(std::equal(output.begin(), output.begin() + 2, payload_for<2>(3).begin()));
  CHECK(std::equal(output.begin() + 2, output.end(), initial_output.begin() + 2));
}

TEST_CASE("sequence payload ring leaves observer progress and resynchronization to callers") {
  using Ring = handoff::metadata::SequencePayloadRing<4, 8>;
  Ring ring;
  for (std::uint64_t sequence = 1; sequence <= 7; ++sequence) {
    CHECK(ring.try_publish(sequence, control_for(sequence), payload_for<4>(sequence)) ==
          Ring::PublishResult::success);
  }

  const auto range = ring.available_range();
  CHECK(range.oldest == 4);
  CHECK(range.latest == 7);

  std::uint64_t first_position = range.oldest;
  std::uint64_t second_position = range.latest;
  Metadata metadata{};
  std::array<std::byte, 8> output{};
  CHECK(ring.try_read(first_position, metadata, output) == Ring::ReadResult::success);
  CHECK(metadata.signature == first_position);
  CHECK(ring.try_read(second_position, metadata, output) == Ring::ReadResult::success);
  CHECK(metadata.signature == second_position);
  ++first_position;
  CHECK(ring.try_read(first_position, metadata, output) == Ring::ReadResult::success);
  CHECK(metadata.signature == first_position);
}

TEST_CASE("sequence payload ring rejects publication after finite sequence exhaustion") {
  using Ring = handoff::metadata::SequencePayloadRing<4, 4, std::uint8_t>;
  Ring ring;

  for (unsigned int expected = 1; expected <= Ring::sequence_limit(); ++expected) {
    CHECK(ring.try_publish(expected, control_for(expected), payload_for<1>(expected)) ==
          Ring::PublishResult::success);
  }
  CHECK(ring.published_sequence() == Ring::sequence_limit());
  CHECK(ring.try_publish(255, control_for(255), payload_for<1>(255)) ==
        Ring::PublishResult::sequence_exhausted);

  Metadata metadata{};
  std::array<std::byte, 4> output{};
  CHECK(ring.try_read(Ring::sequence_limit(), metadata, output) == Ring::ReadResult::success);
  CHECK(metadata.signature == Ring::sequence_limit());
  CHECK(std::to_integer<unsigned int>(output[0]) ==
        std::to_integer<unsigned int>(payload_for<1>(Ring::sequence_limit())[0]));
}

TEST_CASE("sequence payload ring concurrent observers never accept torn records") {
  using Ring = handoff::metadata::SequencePayloadRing<64, 32>;
  constexpr std::uint64_t publication_count = 100'000;
  Ring ring;
  std::atomic<bool> producer_done{false};
  std::atomic<bool> valid{true};

  std::thread producer([&] {
    for (std::uint64_t sequence = 1; sequence <= publication_count; ++sequence) {
      const auto length = static_cast<std::size_t>(sequence % (Ring::chunk_size() + 1));
      const auto payload = payload_for<Ring::chunk_size()>(sequence);
      if (ring.try_publish(sequence, control_for(sequence), std::span{payload}.first(length)) !=
          Ring::PublishResult::success) {
        valid.store(false, std::memory_order_relaxed);
        break;
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  std::array<std::thread, 2> observers;
  for (std::size_t observer = 0; observer < observers.size(); ++observer) {
    observers[observer] = std::thread([&, observer] {
      std::uint64_t requested = 1;
      while (requested <= publication_count) {
        Metadata metadata{.signature = 0, .chunk = 0, .length = 0, .control = 0};
        auto output = payload_for<Ring::chunk_size()>(0);
        const auto result = ring.try_read(requested, metadata, output);
        if (result == Ring::ReadResult::success) {
          const auto expected_length =
              static_cast<std::size_t>(requested % (Ring::chunk_size() + 1));
          const auto expected_payload = payload_for<Ring::chunk_size()>(requested);
          if (metadata.signature != requested ||
              metadata.chunk != ((requested - 1) & (Ring::capacity() - 1)) ||
              metadata.length != expected_length || metadata.control != control_for(requested) ||
              !std::equal(output.begin(), output.begin() + expected_length,
                          expected_payload.begin())) {
            valid.store(false, std::memory_order_relaxed);
          }
          ++requested;
        } else if (result == Ring::ReadResult::overwritten) {
          const auto range = ring.available_range();
          requested = std::max(requested + 1, static_cast<std::uint64_t>(range.oldest));
        } else if (result == Ring::ReadResult::not_yet_published ||
                   result == Ring::ReadResult::retry) {
          if (metadata != Metadata{} || output != payload_for<Ring::chunk_size()>(0)) {
            valid.store(false, std::memory_order_relaxed);
          }
          std::this_thread::yield();
        } else {
          valid.store(false, std::memory_order_relaxed);
          return;
        }
        if (observer == 1 && requested % 5 == 0) {
          std::this_thread::yield();
        }
        if (producer_done.load(std::memory_order_acquire) &&
            requested > ring.published_sequence()) {
          break;
        }
      }
    });
  }

  producer.join();
  for (auto& observer : observers) {
    observer.join();
  }
  CHECK(valid.load(std::memory_order_relaxed));
}
