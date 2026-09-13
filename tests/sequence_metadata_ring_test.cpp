#include "handoff/metadata/sequence_metadata_ring.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <thread>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

namespace {

using Metadata = handoff::metadata::Metadata;

constexpr Metadata metadata_for(std::uint64_t sequence) {
  return {.signature = sequence,
          .chunk = ~sequence,
          .length = static_cast<std::uint32_t>(sequence),
          .control = static_cast<std::uint32_t>(sequence ^ 0xa5a5a5a5U)};
}

constexpr bool matches(const Metadata& metadata, std::uint64_t sequence) {
  return metadata == metadata_for(sequence);
}

} // namespace

TEST_CASE("metadata ring starts empty and validates direct sequence reads") {
  using Ring = handoff::metadata::SequenceMetadataRing<4>;
  Ring ring;
  Metadata output = metadata_for(99);

  STATIC_CHECK(std::is_standard_layout_v<Metadata>);
  STATIC_CHECK(Ring::capacity() == 4);
  STATIC_CHECK(Ring::first_sequence() == 1);
  CHECK(ring.published_sequence() == 0);
  CHECK(ring.available_range().empty());
  CHECK(ring.try_read(1, output) == Ring::ReadResult::not_yet_published);
  CHECK(output == metadata_for(99));
  CHECK(ring.try_read(0, output) == Ring::ReadResult::invalid_sequence);
  CHECK(ring.try_read(Ring::sequence_limit() + 1, output) == Ring::ReadResult::invalid_sequence);
  CHECK(output == metadata_for(99));

  const auto first = ring.try_publish(metadata_for(1));
  CHECK(first == std::optional<std::uint64_t>{1});
  CHECK(ring.published_sequence() == 1);
  CHECK(ring.try_read(1, output) == Ring::ReadResult::success);
  CHECK(matches(output, 1));
  CHECK(ring.try_read(2, output) == Ring::ReadResult::not_yet_published);
  CHECK(matches(output, 1));
}

TEST_CASE("metadata ring maps sequences directly and detects exact-wrap overwrite") {
  using Ring = handoff::metadata::SequenceMetadataRing<4>;
  Ring ring;

  for (std::uint64_t sequence = 1; sequence <= 4; ++sequence) {
    REQUIRE(ring.try_publish(metadata_for(sequence)) == sequence);
  }
  Metadata output{};
  for (std::uint64_t sequence = 1; sequence <= 4; ++sequence) {
    CHECK(ring.try_read(sequence, output) == Ring::ReadResult::success);
    CHECK(matches(output, sequence));
  }

  REQUIRE(ring.try_publish(metadata_for(5)) == 5);
  output = metadata_for(99);
  CHECK(ring.try_read(1, output) == Ring::ReadResult::overwritten);
  CHECK(output == metadata_for(99));
  CHECK(ring.try_read(5, output) == Ring::ReadResult::success);
  CHECK(matches(output, 5));
  CHECK(ring.try_read(2, output) == Ring::ReadResult::success);
  CHECK(matches(output, 2));
}

TEST_CASE("metadata ring supports independent caller-owned observers") {
  using Ring = handoff::metadata::SequenceMetadataRing<8>;
  Ring ring;
  for (std::uint64_t sequence = 1; sequence <= 6; ++sequence) {
    REQUIRE(ring.try_publish(metadata_for(sequence)) == sequence);
  }

  std::uint64_t first_position = 1;
  std::uint64_t second_position = 1;
  Metadata first{};
  Metadata second{};
  for (; first_position <= 6; ++first_position) {
    CHECK(ring.try_read(first_position, first) == Ring::ReadResult::success);
    CHECK(matches(first, first_position));
  }
  for (; second_position <= 3; ++second_position) {
    CHECK(ring.try_read(second_position, second) == Ring::ReadResult::success);
    CHECK(matches(second, second_position));
  }
  CHECK(first_position == 7);
  CHECK(second_position == 4);
}

TEST_CASE("metadata ring exposes a resynchronization range without choosing policy") {
  using Ring = handoff::metadata::SequenceMetadataRing<4>;
  Ring ring;

  REQUIRE(ring.try_publish(metadata_for(1)) == 1);
  REQUIRE(ring.try_publish(metadata_for(2)) == 2);
  auto range = ring.available_range();
  CHECK(range.oldest == 1);
  CHECK(range.latest == 2);

  for (std::uint64_t sequence = 3; sequence <= 7; ++sequence) {
    REQUIRE(ring.try_publish(metadata_for(sequence)) == sequence);
  }
  range = ring.available_range();
  CHECK(range.oldest == 4);
  CHECK(range.latest == 7);

  Metadata output{};
  CHECK(ring.try_read(2, output) == Ring::ReadResult::overwritten);
  CHECK(ring.try_read(range.oldest, output) == Ring::ReadResult::success);
  CHECK(matches(output, range.oldest));
}

TEST_CASE("metadata ring rejects publication after its finite sequence range") {
  using Ring = handoff::metadata::SequenceMetadataRing<4, std::uint8_t>;
  Ring ring;

  for (unsigned int expected = 1; expected <= Ring::sequence_limit(); ++expected) {
    const auto published = ring.try_publish(metadata_for(expected));
    CHECK(published == std::optional<std::uint8_t>{static_cast<std::uint8_t>(expected)});
  }
  CHECK(ring.published_sequence() == Ring::sequence_limit());
  CHECK_FALSE(ring.try_publish(metadata_for(255)));

  Metadata output{};
  CHECK(ring.try_read(Ring::sequence_limit(), output) == Ring::ReadResult::success);
  CHECK(matches(output, Ring::sequence_limit()));
}

TEST_CASE("metadata ring concurrent observers never accept torn metadata") {
  using Ring = handoff::metadata::SequenceMetadataRing<64>;
  constexpr std::uint64_t publication_count = 200'000;
  Ring ring;
  std::atomic<bool> producer_done{false};
  std::atomic<bool> valid{true};

  std::thread producer([&] {
    for (std::uint64_t sequence = 1; sequence <= publication_count; ++sequence) {
      const auto published = ring.try_publish(metadata_for(sequence));
      if (published != std::optional<std::uint64_t>{sequence}) {
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
        Metadata output = metadata_for(0);
        const auto result = ring.try_read(requested, output);
        if (result != Ring::ReadResult::success && output != metadata_for(0)) {
          valid.store(false, std::memory_order_relaxed);
        }
        switch (result) {
        case Ring::ReadResult::success:
          if (!matches(output, requested)) {
            valid.store(false, std::memory_order_relaxed);
          }
          ++requested;
          break;
        case Ring::ReadResult::overwritten: {
          const auto range = ring.available_range();
          requested = std::max(requested + 1, static_cast<std::uint64_t>(range.oldest));
          break;
        }
        case Ring::ReadResult::not_yet_published:
        case Ring::ReadResult::retry:
          std::this_thread::yield();
          break;
        case Ring::ReadResult::invalid_sequence:
          valid.store(false, std::memory_order_relaxed);
          return;
        }
        if (observer == 1 && requested % 7 == 0) {
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
