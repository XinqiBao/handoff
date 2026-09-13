#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace handoff::metadata {

struct Metadata {
  std::uint64_t signature{};
  std::uint64_t chunk{};
  std::uint32_t length{};
  std::uint32_t control{};

  friend constexpr bool operator==(const Metadata&, const Metadata&) = default;
};

static_assert(std::is_standard_layout_v<Metadata>);

template <std::size_t Capacity, std::unsigned_integral Sequence = std::uint64_t>
  requires(!std::same_as<Sequence, bool>)
class SequenceMetadataRing {
  static_assert(Capacity > 0, "a metadata ring needs at least one slot");
  static_assert(std::has_single_bit(Capacity), "metadata ring capacity must be a power of two");
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

  enum class ReadResult { success, not_yet_published, overwritten, retry, invalid_sequence };

  struct AvailableRange {
    sequence_type oldest{};
    sequence_type latest{};

    [[nodiscard]] constexpr bool empty() const noexcept { return latest == empty_sequence(); }
  };

  static constexpr std::size_t capacity() noexcept { return Capacity; }
  static constexpr sequence_type first_sequence() noexcept { return sequence_type{1}; }
  static constexpr sequence_type sequence_limit() noexcept {
    return std::numeric_limits<sequence_type>::max() - sequence_type{1};
  }

  SequenceMetadataRing() = default;
  SequenceMetadataRing(const SequenceMetadataRing&) = delete;
  SequenceMetadataRing& operator=(const SequenceMetadataRing&) = delete;
  SequenceMetadataRing(SequenceMetadataRing&&) = delete;
  SequenceMetadataRing& operator=(SequenceMetadataRing&&) = delete;

  [[nodiscard]] std::optional<sequence_type> try_publish(const Metadata& metadata) noexcept {
    if (exhausted_) {
      return std::nullopt;
    }

    const auto sequence = next_sequence_;
    auto& target = slot(sequence);
    target.sequence.store(in_progress_sequence());
    target.signature.store(metadata.signature);
    target.chunk.store(metadata.chunk);
    target.length.store(metadata.length);
    target.control.store(metadata.control);
    target.sequence.store(sequence);
    published_sequence_.store(sequence);

    if (sequence == sequence_limit()) {
      exhausted_ = true;
    } else {
      ++next_sequence_;
    }
    return sequence;
  }

  [[nodiscard]] ReadResult try_read(sequence_type requested, Metadata& output) const noexcept {
    if (!is_publication_sequence(requested)) {
      return ReadResult::invalid_sequence;
    }

    const auto& source = slot(requested);
    const auto before = source.sequence.load();
    if (before != requested) {
      return classify(before, requested);
    }

    const Metadata snapshot{.signature = source.signature.load(),
                            .chunk = source.chunk.load(),
                            .length = source.length.load(),
                            .control = source.control.load()};
    const auto after = source.sequence.load();
    if (after != requested) {
      return classify(after, requested);
    }

    output = snapshot;
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

  [[nodiscard]] Slot& slot(sequence_type sequence) noexcept {
    const auto index = static_cast<std::size_t>(sequence - first_sequence()) & (Capacity - 1);
    return slots_[index];
  }

  [[nodiscard]] const Slot& slot(sequence_type sequence) const noexcept {
    const auto index = static_cast<std::size_t>(sequence - first_sequence()) & (Capacity - 1);
    return slots_[index];
  }

  std::array<Slot, Capacity> slots_{};
  std::atomic<sequence_type> published_sequence_{empty_sequence()};
  sequence_type next_sequence_{first_sequence()};
  bool exhausted_{false};
};

} // namespace handoff::metadata
