#include "handoff/spsc/basic_bounded_ring.hpp"
#include "handoff/spsc/batch_bounded_ring.hpp"
#include "handoff/spsc/bulk_burst_bounded_ring.hpp"
#include "handoff/spsc/cache_line_bounded_ring.hpp"
#include "handoff/spsc/cached_index_bounded_ring.hpp"

#include <array>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <utility>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

using BasicIntegerRing = handoff::spsc::BasicBoundedRing<std::uint64_t, 4>;
using BatchIntegerRing = handoff::spsc::BatchBoundedRing<std::uint64_t, 4>;
using BulkBurstIntegerRing = handoff::spsc::BulkBurstBoundedRing<std::uint64_t, 4>;
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
using BatchMessageRing = handoff::spsc::BatchBoundedRing<Message, 1'024>;
using BulkBurstMessageRing = handoff::spsc::BulkBurstBoundedRing<Message, 1'024>;
using CacheLineMessageRing = handoff::spsc::CacheLineBoundedRing<Message, 1'024>;
using CachedIndexMessageRing = handoff::spsc::CachedIndexBoundedRing<Message, 1'024>;

using BasicOwnedRing = handoff::spsc::BasicBoundedRing<MoveOnlyResource, 1>;
using BatchOwnedRing = handoff::spsc::BatchBoundedRing<MoveOnlyResource, 1>;
using BulkBurstOwnedRing = handoff::spsc::BulkBurstBoundedRing<MoveOnlyResource, 1>;
using CacheLineOwnedRing = handoff::spsc::CacheLineBoundedRing<MoveOnlyResource, 1>;
using CachedIndexOwnedRing = handoff::spsc::CachedIndexBoundedRing<MoveOnlyResource, 1>;

template <typename Ring>
concept SupportsConstLvaluePush =
    requires(Ring& ring, const typename Ring::value_type& value) { ring.try_push(value); };

} // namespace

TEMPLATE_TEST_CASE("bounded SPSC rings report empty and exact full capacity", "[spsc]",
                   BasicIntegerRing, BatchIntegerRing, CacheLineIntegerRing, CachedIndexIntegerRing,
                   BulkBurstIntegerRing) {
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
                   BasicIntegerRing, BatchIntegerRing, CacheLineIntegerRing, CachedIndexIntegerRing,
                   BulkBurstIntegerRing) {
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
                   BasicMessageRing, BatchMessageRing, CacheLineMessageRing, CachedIndexMessageRing,
                   BulkBurstMessageRing) {
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
                   BasicOwnedRing, BatchOwnedRing, CacheLineOwnedRing, CachedIndexOwnedRing,
                   BulkBurstOwnedRing) {
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

TEST_CASE("batch SPSC operations are all-or-nothing and preserve wrapped FIFO order") {
  handoff::spsc::BatchBoundedRing<std::uint64_t, 4> ring;
  std::array<std::uint64_t, 3> first{1, 2, 3};
  std::array<std::uint64_t, 2> second{4, 5};
  std::array<std::uint64_t, 2> prefix{};
  std::array<std::uint64_t, 3> wrapped{};
  std::array<std::uint64_t, 5> oversized{};

  CHECK(ring.try_push_batch(std::span<const std::uint64_t>{}));
  CHECK(ring.try_pop_batch(std::span<std::uint64_t>{}));
  CHECK_FALSE(ring.try_push_batch(oversized));
  CHECK_FALSE(ring.try_pop_batch(oversized));

  REQUIRE(ring.try_push_batch(first));
  CHECK_FALSE(ring.try_push_batch(second));
  CHECK(second == std::array<std::uint64_t, 2>{4, 5});
  REQUIRE(ring.try_pop_batch(prefix));
  CHECK(prefix == std::array<std::uint64_t, 2>{1, 2});

  REQUIRE(ring.try_push_batch(second));
  REQUIRE(ring.try_pop_batch(wrapped));
  CHECK(wrapped == std::array<std::uint64_t, 3>{3, 4, 5});

  REQUIRE(ring.try_push(6));
  std::array<std::uint64_t, 2> unchanged{90, 91};
  CHECK_FALSE(ring.try_pop_batch(unchanged));
  CHECK(unchanged == std::array<std::uint64_t, 2>{90, 91});
}

TEST_CASE("batch SPSC operations transfer resource-owning values by assignment") {
  handoff::spsc::BatchBoundedRing<std::shared_ptr<int>, 2> ring;
  const std::array inputs{std::make_shared<int>(10), std::make_shared<int>(20)};
  std::array<std::shared_ptr<int>, 2> outputs{};

  REQUIRE(ring.try_push_batch(inputs));
  CHECK(inputs[0].use_count() == 2);
  CHECK(inputs[1].use_count() == 2);
  REQUIRE(ring.try_pop_batch(outputs));
  REQUIRE(outputs[0]);
  REQUIRE(outputs[1]);
  CHECK(*outputs[0] == 10);
  CHECK(*outputs[1] == 20);
  CHECK(inputs[0].use_count() == 2);
  CHECK(inputs[1].use_count() == 2);
}

TEST_CASE("batch SPSC operations preserve concurrent message integrity") {
  constexpr std::uint64_t message_count = 100'000;
  constexpr std::size_t batch_size = 4;
  handoff::spsc::BatchBoundedRing<Message, 64> ring;
  std::atomic<bool> valid{true};

  std::thread producer([&] {
    std::array<Message, batch_size> messages{};
    for (std::uint64_t first = 0; first < message_count; first += batch_size) {
      for (std::size_t offset = 0; offset < batch_size; ++offset) {
        const auto sequence = first + offset;
        messages[offset] = {.sequence = sequence, .inverse = ~sequence};
      }
      while (!ring.try_push_batch(messages)) {
        std::this_thread::yield();
      }
    }
  });

  std::thread consumer([&] {
    std::array<Message, batch_size> messages{};
    for (std::uint64_t first = 0; first < message_count; first += batch_size) {
      while (!ring.try_pop_batch(messages)) {
        std::this_thread::yield();
      }
      for (std::size_t offset = 0; offset < batch_size; ++offset) {
        const auto expected = first + offset;
        if (messages[offset].sequence != expected || messages[offset].inverse != ~expected) {
          valid.store(false, std::memory_order_relaxed);
        }
      }
    }
  });

  producer.join();
  consumer.join();
  CHECK(valid.load(std::memory_order_relaxed));
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
