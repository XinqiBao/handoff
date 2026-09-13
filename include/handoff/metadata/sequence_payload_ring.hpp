#pragma once

#include "handoff/metadata/metadata.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace handoff::metadata {

template <std::size_t Capacity, std::size_t ChunkSize,
          std::unsigned_integral Sequence = std::uint64_t>
  requires(!std::same_as<Sequence, bool>)
class SequencePayloadRing {
  static_assert(Capacity > 0, "a payload ring needs at least one publication slot");
  static_assert(std::has_single_bit(Capacity), "payload ring capacity must be a power of two");
  static_assert(ChunkSize > 0, "a payload chunk needs at least one byte");
  static_assert(ChunkSize <= std::numeric_limits<std::uint32_t>::max(),
                "chunk size must fit in metadata length");
  static_assert(ChunkSize <= std::numeric_limits<std::size_t>::max() / Capacity,
                "total payload byte capacity must fit in size_t");
  static_assert(Capacity <= static_cast<std::size_t>(std::numeric_limits<Sequence>::max() - 1),
                "capacity must fit below the reserved sequence values");

  struct Slot {
    std::atomic<Sequence> sequence{Sequence{0}};
    std::atomic<std::uint64_t> signature{0};
    std::atomic<std::uint64_t> chunk{0};
    std::atomic<std::uint32_t> length{0};
    std::atomic<std::uint32_t> control{0};
  };

public:
  using sequence_type = Sequence;

  enum class PublishResult { success, payload_too_large, sequence_exhausted };
  enum class ReadResult {
    success,
    not_yet_published,
    overwritten,
    retry,
    invalid_sequence,
    output_too_small
  };

  struct AvailableRange {
    sequence_type oldest{};
    sequence_type latest{};

    [[nodiscard]] constexpr bool empty() const noexcept { return latest == empty_sequence(); }
  };

  static constexpr std::size_t capacity() noexcept { return Capacity; }
  static constexpr std::size_t chunk_size() noexcept { return ChunkSize; }
  static constexpr std::size_t payload_byte_capacity() noexcept { return Capacity * ChunkSize; }
  static constexpr sequence_type first_sequence() noexcept { return sequence_type{1}; }
  static constexpr sequence_type sequence_limit() noexcept {
    return std::numeric_limits<sequence_type>::max() - sequence_type{1};
  }

  SequencePayloadRing() = default;
  SequencePayloadRing(const SequencePayloadRing&) = delete;
  SequencePayloadRing& operator=(const SequencePayloadRing&) = delete;
  SequencePayloadRing(SequencePayloadRing&&) = delete;
  SequencePayloadRing& operator=(SequencePayloadRing&&) = delete;

  [[nodiscard]] PublishResult try_publish(std::uint64_t signature, std::uint32_t control,
                                          std::span<const std::byte> payload) noexcept {
    if (payload.size() > ChunkSize) {
      return PublishResult::payload_too_large;
    }
    if (exhausted_) {
      return PublishResult::sequence_exhausted;
    }

    const auto sequence = next_sequence_;
    const auto chunk = chunk_for(sequence);
    auto& target = slot(sequence);
    target.sequence.store(in_progress_sequence());
    const auto payload_offset = chunk * ChunkSize;
    for (std::size_t index = 0; index < payload.size(); ++index) {
      payload_[payload_offset + index].store(payload[index]);
    }
    target.signature.store(signature);
    target.chunk.store(static_cast<std::uint64_t>(chunk));
    target.length.store(static_cast<std::uint32_t>(payload.size()));
    target.control.store(control);
    target.sequence.store(sequence);
    published_sequence_.store(sequence);

    if (sequence == sequence_limit()) {
      exhausted_ = true;
    } else {
      ++next_sequence_;
    }
    return PublishResult::success;
  }

  [[nodiscard]] ReadResult try_read(sequence_type requested, Metadata& metadata,
                                    std::span<std::byte> payload) const noexcept {
    if (!is_publication_sequence(requested)) {
      return ReadResult::invalid_sequence;
    }

    const auto& source = slot(requested);
    const auto before = source.sequence.load();
    if (before != requested) {
      return classify(before, requested);
    }

    const Metadata metadata_snapshot{.signature = source.signature.load(),
                                     .chunk = source.chunk.load(),
                                     .length = source.length.load(),
                                     .control = source.control.load()};
    if (payload.size() < metadata_snapshot.length) {
      const auto after = source.sequence.load();
      if (after != requested) {
        return classify(after, requested);
      }
      return ReadResult::output_too_small;
    }

    std::array<std::byte, ChunkSize> payload_snapshot{};
    const auto payload_offset = chunk_for(requested) * ChunkSize;
    for (std::size_t index = 0; index < metadata_snapshot.length; ++index) {
      payload_snapshot[index] = payload_[payload_offset + index].load();
    }
    const auto after = source.sequence.load();
    if (after != requested) {
      return classify(after, requested);
    }

    metadata = metadata_snapshot;
    std::copy_n(payload_snapshot.begin(), metadata_snapshot.length, payload.begin());
    return ReadResult::success;
  }

  [[nodiscard]] sequence_type published_sequence() const noexcept {
    return published_sequence_.load();
  }

  [[nodiscard]] AvailableRange available_range() const noexcept {
    const auto latest = published_sequence();
    if (latest == empty_sequence()) {
      return {};
    }
    if (latest <= static_cast<sequence_type>(Capacity)) {
      return {.oldest = first_sequence(), .latest = latest};
    }
    return {.oldest = static_cast<sequence_type>(latest - static_cast<sequence_type>(Capacity) +
                                                 first_sequence()),
            .latest = latest};
  }

private:
  static constexpr sequence_type empty_sequence() noexcept { return sequence_type{0}; }
  static constexpr sequence_type in_progress_sequence() noexcept {
    return std::numeric_limits<sequence_type>::max();
  }

  static constexpr bool is_publication_sequence(sequence_type sequence) noexcept {
    return sequence >= first_sequence() && sequence <= sequence_limit();
  }

  static constexpr ReadResult classify(sequence_type observed, sequence_type requested) noexcept {
    if (observed == in_progress_sequence()) {
      return ReadResult::retry;
    }
    if (observed > requested) {
      return ReadResult::overwritten;
    }
    return ReadResult::not_yet_published;
  }

  static constexpr std::size_t chunk_for(sequence_type sequence) noexcept {
    return static_cast<std::size_t>(sequence - first_sequence()) & (Capacity - 1);
  }

  [[nodiscard]] Slot& slot(sequence_type sequence) noexcept { return slots_[chunk_for(sequence)]; }
  [[nodiscard]] const Slot& slot(sequence_type sequence) const noexcept {
    return slots_[chunk_for(sequence)];
  }

  std::array<Slot, Capacity> slots_{};
  std::array<std::atomic<std::byte>, Capacity * ChunkSize> payload_{};
  std::atomic<sequence_type> published_sequence_{empty_sequence()};
  sequence_type next_sequence_{first_sequence()};
  bool exhausted_{false};
};

} // namespace handoff::metadata
