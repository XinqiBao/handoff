#include "handoff/spsc/basic_bounded_ring.hpp"
#include "handoff/spsc/cache_line_bounded_ring.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

using BasicIntegerRing = handoff::spsc::BasicBoundedRing<std::uint64_t, 4>;
using CacheLineIntegerRing = handoff::spsc::CacheLineBoundedRing<std::uint64_t, 4>;

struct Message {
  std::uint64_t sequence{};
  std::uint64_t inverse{};
};

using BasicMessageRing = handoff::spsc::BasicBoundedRing<Message, 1'024>;
using CacheLineMessageRing = handoff::spsc::CacheLineBoundedRing<Message, 1'024>;

} // namespace

TEMPLATE_TEST_CASE("bounded SPSC rings report empty and exact full capacity", "[spsc]",
                   BasicIntegerRing, CacheLineIntegerRing) {
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
                   BasicIntegerRing, CacheLineIntegerRing) {
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
                   BasicMessageRing, CacheLineMessageRing) {
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

TEST_CASE("the cache-line SPSC ring isolates its shared state blocks") {
  using Ring = handoff::spsc::CacheLineBoundedRing<std::uint64_t, 4>;
  STATIC_CHECK(Ring::state_alignment() == 128);
  STATIC_CHECK(alignof(Ring) >= Ring::state_alignment());
}
