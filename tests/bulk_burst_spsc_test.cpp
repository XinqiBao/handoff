#include "handoff/spsc/bulk_burst_bounded_ring.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>

#include <catch2/catch_test_macros.hpp>

namespace {

struct Message {
  std::uint64_t sequence{};
  std::uint64_t inverse{};
};

} // namespace

TEST_CASE("bulk SPSC operations are all-or-nothing and preserve wrapped FIFO order") {
  handoff::spsc::BulkBurstBoundedRing<std::uint64_t, 4> ring;
  std::array<std::uint64_t, 3> first{1, 2, 3};
  std::array<std::uint64_t, 2> second{4, 5};
  std::array<std::uint64_t, 2> prefix{};
  std::array<std::uint64_t, 3> wrapped{};
  std::array<std::uint64_t, 5> oversized{};

  CHECK(ring.try_push_bulk(std::span<const std::uint64_t>{}));
  CHECK(ring.try_pop_bulk(std::span<std::uint64_t>{}));
  CHECK_FALSE(ring.try_push_bulk(oversized));
  CHECK_FALSE(ring.try_pop_bulk(oversized));

  REQUIRE(ring.try_push_bulk(first));
  CHECK_FALSE(ring.try_push_bulk(second));
  CHECK(second == std::array<std::uint64_t, 2>{4, 5});
  REQUIRE(ring.try_pop_bulk(prefix));
  CHECK(prefix == std::array<std::uint64_t, 2>{1, 2});

  REQUIRE(ring.try_push_bulk(second));
  REQUIRE(ring.try_pop_bulk(wrapped));
  CHECK(wrapped == std::array<std::uint64_t, 3>{3, 4, 5});

  REQUIRE(ring.try_push(6));
  std::array<std::uint64_t, 2> unchanged{90, 91};
  CHECK_FALSE(ring.try_pop_bulk(unchanged));
  CHECK(unchanged == std::array<std::uint64_t, 2>{90, 91});
}

TEST_CASE("burst SPSC operations make exact partial progress and preserve suffixes") {
  handoff::spsc::BulkBurstBoundedRing<std::uint64_t, 4> ring;
  const std::array<std::uint64_t, 3> first{1, 2, 3};
  const std::array<std::uint64_t, 2> second{4, 5};

  CHECK(ring.try_push_burst(std::span<const std::uint64_t>{}) == 0);
  CHECK(ring.try_pop_burst(std::span<std::uint64_t>{}) == 0);
  CHECK(ring.try_push_burst(first) == 3);
  CHECK(ring.try_push_burst(second) == 1);
  CHECK(second == std::array<std::uint64_t, 2>{4, 5});
  CHECK(ring.try_push_burst(second) == 0);

  std::array<std::uint64_t, 2> prefix{90, 91};
  REQUIRE(ring.try_pop_burst(prefix) == 2);
  CHECK(prefix == std::array<std::uint64_t, 2>{1, 2});
  REQUIRE(ring.try_push_burst(std::span(second).subspan(1)) == 1);

  std::array<std::uint64_t, 6> oversized{90, 91, 92, 93, 94, 95};
  REQUIRE(ring.try_pop_burst(oversized) == 3);
  CHECK(oversized == std::array<std::uint64_t, 6>{3, 4, 5, 93, 94, 95});
  CHECK(ring.try_pop_burst(oversized) == 0);
  CHECK(oversized == std::array<std::uint64_t, 6>{3, 4, 5, 93, 94, 95});
}

TEST_CASE("bulk and burst SPSC operations transfer resource-owning values by assignment") {
  handoff::spsc::BulkBurstBoundedRing<std::shared_ptr<int>, 2> ring;
  const std::array inputs{std::make_shared<int>(10), std::make_shared<int>(20)};
  std::array<std::shared_ptr<int>, 3> outputs{nullptr, nullptr, std::make_shared<int>(30)};

  REQUIRE(ring.try_push_bulk(inputs));
  CHECK(inputs[0].use_count() == 2);
  CHECK(inputs[1].use_count() == 2);
  REQUIRE(ring.try_pop_burst(outputs) == 2);
  REQUIRE(outputs[0]);
  REQUIRE(outputs[1]);
  REQUIRE(outputs[2]);
  CHECK(*outputs[0] == 10);
  CHECK(*outputs[1] == 20);
  CHECK(*outputs[2] == 30);
  CHECK(inputs[0].use_count() == 2);
  CHECK(inputs[1].use_count() == 2);
}

TEST_CASE("bulk and burst SPSC operations preserve concurrent message integrity") {
  constexpr std::uint64_t message_count = 100'000;
  constexpr std::size_t group_size = 4;

  SECTION("bulk") {
    handoff::spsc::BulkBurstBoundedRing<Message, 64> ring;
    std::atomic<bool> valid{true};
    std::thread producer([&] {
      std::array<Message, group_size> messages{};
      for (std::uint64_t first = 0; first < message_count; first += group_size) {
        for (std::size_t offset = 0; offset < group_size; ++offset) {
          const auto sequence = first + offset;
          messages[offset] = {.sequence = sequence, .inverse = ~sequence};
        }
        while (!ring.try_push_bulk(messages)) {
          std::this_thread::yield();
        }
      }
    });
    std::thread consumer([&] {
      std::array<Message, group_size> messages{};
      for (std::uint64_t first = 0; first < message_count; first += group_size) {
        while (!ring.try_pop_bulk(messages)) {
          std::this_thread::yield();
        }
        for (std::size_t offset = 0; offset < group_size; ++offset) {
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

  SECTION("burst") {
    handoff::spsc::BulkBurstBoundedRing<Message, 3> ring;
    std::atomic<bool> valid{true};
    std::thread producer([&] {
      std::array<Message, group_size> messages{};
      for (std::uint64_t first = 0; first < message_count; first += group_size) {
        for (std::size_t offset = 0; offset < group_size; ++offset) {
          const auto sequence = first + offset;
          messages[offset] = {.sequence = sequence, .inverse = ~sequence};
        }
        std::size_t completed = 0;
        while (completed < messages.size()) {
          const auto count = ring.try_push_burst(std::span(messages).subspan(completed));
          completed += count;
          if (count == 0) {
            std::this_thread::yield();
          }
        }
      }
    });
    std::thread consumer([&] {
      std::array<Message, group_size> messages{};
      for (std::uint64_t first = 0; first < message_count; first += group_size) {
        std::size_t completed = 0;
        while (completed < messages.size()) {
          const auto count = ring.try_pop_burst(std::span(messages).subspan(completed));
          completed += count;
          if (count == 0) {
            std::this_thread::yield();
          }
        }
        for (std::size_t offset = 0; offset < group_size; ++offset) {
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
}
