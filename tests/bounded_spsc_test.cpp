#include "handoff/spsc/basic_bounded_ring.hpp"
#include "handoff/spsc/cache_line_bounded_ring.hpp"
#include "handoff/spsc/cached_index_bounded_ring.hpp"

#include <atomic>
#include <concepts>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

using BasicIntegerRing = handoff::spsc::BasicBoundedRing<std::uint64_t, 4>;
using CacheLineIntegerRing = handoff::spsc::CacheLineBoundedRing<std::uint64_t, 4>;
using CachedIndexIntegerRing = handoff::spsc::CachedIndexBoundedRing<std::uint64_t, 4>;

struct Message {
  std::uint64_t sequence{};
  std::uint64_t inverse{};
};

struct MoveOnlyResource {
  std::unique_ptr<int> value;
  unsigned int* move_count{};

  MoveOnlyResource() = default;
  MoveOnlyResource(int initial_value, unsigned int& moves)
      : value(std::make_unique<int>(initial_value)), move_count(&moves) {}
  MoveOnlyResource(const MoveOnlyResource&) = delete;
  MoveOnlyResource& operator=(const MoveOnlyResource&) = delete;
  MoveOnlyResource(MoveOnlyResource&&) noexcept = default;
  MoveOnlyResource& operator=(MoveOnlyResource&& other) noexcept {
    value = std::move(other.value);
    move_count = other.move_count;
    if (move_count != nullptr) {
      ++*move_count;
    }
    return *this;
  }
};

using BasicMessageRing = handoff::spsc::BasicBoundedRing<Message, 1'024>;
using CacheLineMessageRing = handoff::spsc::CacheLineBoundedRing<Message, 1'024>;
using CachedIndexMessageRing = handoff::spsc::CachedIndexBoundedRing<Message, 1'024>;

using BasicOwnedRing = handoff::spsc::BasicBoundedRing<MoveOnlyResource, 1>;
using CacheLineOwnedRing = handoff::spsc::CacheLineBoundedRing<MoveOnlyResource, 1>;
using CachedIndexOwnedRing = handoff::spsc::CachedIndexBoundedRing<MoveOnlyResource, 1>;

template <typename Ring>
concept SupportsConstLvaluePush =
    requires(Ring& ring, const typename Ring::value_type& value) { ring.try_push(value); };

} // namespace

TEMPLATE_TEST_CASE("bounded SPSC rings report empty and exact full capacity", "[spsc]",
                   BasicIntegerRing, CacheLineIntegerRing, CachedIndexIntegerRing) {
  TestType ring;
  std::uint64_t value = 99;

  CHECK_FALSE(ring.try_pop(value));
  CHECK(value == 99);
  CHECK(ring.capacity() == 4);

  CHECK(ring.try_push(10));
  CHECK(ring.try_push(11));
  CHECK(ring.try_push(12));
  CHECK(ring.try_push(13));
  CHECK_FALSE(ring.try_push(14));

  CHECK(ring.try_pop(value));
  CHECK(value == 10);
  CHECK(ring.try_push(14));
  CHECK_FALSE(ring.try_push(15));
}

TEMPLATE_TEST_CASE("bounded SPSC rings preserve FIFO order through wraparound", "[spsc]",
                   BasicIntegerRing, CacheLineIntegerRing, CachedIndexIntegerRing) {
  TestType ring;
  std::uint64_t value = 0;

  for (std::uint64_t cycle = 0; cycle < 1'000; ++cycle) {
    for (std::uint64_t offset = 0; offset < ring.capacity(); ++offset) {
      REQUIRE(ring.try_push(cycle * ring.capacity() + offset));
    }
    CHECK_FALSE(ring.try_push(0));

    for (std::uint64_t offset = 0; offset < ring.capacity(); ++offset) {
      REQUIRE(ring.try_pop(value));
      CHECK(value == cycle * ring.capacity() + offset);
    }
    CHECK_FALSE(ring.try_pop(value));
  }
}

TEMPLATE_TEST_CASE("bounded SPSC rings preserve messages in a long concurrent run", "[spsc]",
                   BasicMessageRing, CacheLineMessageRing, CachedIndexMessageRing) {
  constexpr std::uint64_t message_count = 1'000'000;
  TestType ring;
  std::atomic<bool> producer_done{false};
  std::atomic<bool> valid{true};

  std::thread producer([&] {
    for (std::uint64_t sequence = 0; sequence < message_count; ++sequence) {
      const Message message{.sequence = sequence, .inverse = ~sequence};
      while (!ring.try_push(message)) {
        std::this_thread::yield();
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  std::thread consumer([&] {
    std::uint64_t expected = 0;
    Message message;
    while (expected < message_count) {
      if (!ring.try_pop(message)) {
        std::this_thread::yield();
        continue;
      }
      if (message.sequence != expected || message.inverse != ~message.sequence) {
        valid.store(false, std::memory_order_relaxed);
      }
      ++expected;
    }
  });

  producer.join();
  consumer.join();
  CHECK(producer_done.load(std::memory_order_acquire));
  CHECK(valid.load(std::memory_order_relaxed));
}

TEMPLATE_TEST_CASE("bounded SPSC rings support move-only resource-owning payloads", "[spsc]",
                   BasicOwnedRing, CacheLineOwnedRing, CachedIndexOwnedRing) {
  STATIC_CHECK(std::default_initializable<typename TestType::value_type>);
  STATIC_CHECK(std::assignable_from<typename TestType::value_type&, typename TestType::value_type>);
  STATIC_CHECK_FALSE(SupportsConstLvaluePush<TestType>);

  TestType ring;
  unsigned int first_moves = 0;
  MoveOnlyResource first(10, first_moves);
  const bool first_pushed = ring.try_push(std::move(first));
  REQUIRE(first_pushed);
  CHECK(first_moves == 1);

  unsigned int rejected_moves = 0;
  MoveOnlyResource rejected(20, rejected_moves);
  const bool rejected_pushed = ring.try_push(std::move(rejected));
  CHECK_FALSE(rejected_pushed);
  CHECK(rejected_moves == 0);

  MoveOnlyResource output;
  REQUIRE(ring.try_pop(output));
  REQUIRE(output.value);
  CHECK(*output.value == 10);
  CHECK(first_moves == 2);

  unsigned int replacement_moves = 0;
  MoveOnlyResource replacement(30, replacement_moves);
  const bool replacement_pushed = ring.try_push(std::move(replacement));
  REQUIRE(replacement_pushed);
  REQUIRE(ring.try_pop(output));
  REQUIRE(output.value);
  CHECK(*output.value == 30);
  CHECK(replacement_moves == 2);

  const auto* output_address = output.value.get();
  CHECK_FALSE(ring.try_pop(output));
  REQUIRE(output.value.get() == output_address);
  CHECK(*output.value == 30);
}

TEST_CASE("the cached-index SPSC ring refreshes stale remote progress across slot wraparound") {
  handoff::spsc::CachedIndexBoundedRing<std::uint64_t, 2> ring;
  std::uint64_t value = 0;

  for (std::uint64_t cycle = 0; cycle < 4; ++cycle) {
    REQUIRE(ring.try_push(cycle * 2));
    REQUIRE(ring.try_push(cycle * 2 + 1));
    CHECK_FALSE(ring.try_push(99));

    REQUIRE(ring.try_pop(value));
    CHECK(value == cycle * 2);
    REQUIRE(ring.try_pop(value));
    CHECK(value == cycle * 2 + 1);
    CHECK_FALSE(ring.try_pop(value));
  }
}

TEST_CASE("the cache-line SPSC ring isolates its shared state blocks") {
  using Ring = handoff::spsc::CacheLineBoundedRing<std::uint64_t, 4>;
  STATIC_CHECK(Ring::state_alignment() == 128);
  STATIC_CHECK(alignof(Ring) >= Ring::state_alignment());
}
