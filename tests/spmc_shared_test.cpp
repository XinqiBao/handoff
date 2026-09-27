#include "handoff/spmc/ordered_release_ring.hpp"
#include "handoff/spmc/serialized_consumer_ring.hpp"
#include "handoff/spmc/slot_completion_ring.hpp"
#include "spmc_test_support.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {
struct Message {
  std::uint64_t position{};
  std::uint64_t inverse{};
};

template <typename Ring> void check_finite_boundary() {
  Ring ring;
  CHECK(!ring.try_acquire());
  for (unsigned int position = 0; position < 255; ++position) {
    auto claim = required_spmc_token(ring.try_claim());
    CHECK(claim.position() == position);
    claim.value() = {position, ~static_cast<std::uint64_t>(position)};
    claim.publish();
    auto item = required_spmc_token(ring.try_acquire());
    CHECK(item.position() == position);
    CHECK(item.value().position == position);
    CHECK(item.value().inverse == ~static_cast<std::uint64_t>(position));
    item.release();
    CHECK(ring.reusable_prefix() == position + 1);
  }
  CHECK(!ring.try_claim());
  CHECK(!ring.try_acquire());
}

template <typename Ring> void check_parallel_integrity() {
  constexpr std::size_t total = 30'000;
  Ring ring;
  std::vector<std::atomic<unsigned int>> seen(total);
  std::atomic<std::size_t> completed{0};
  std::atomic<bool> valid{true};
  std::thread producer([&] {
    for (std::size_t position = 0; position < total; ++position) {
      auto token = [&]() -> typename Ring::ProducerClaim {
        for (;;) {
          auto attempt = ring.try_claim();
          if (attempt) {
            return std::move(*attempt);
          }
          std::this_thread::yield();
        }
      }();
      if (token.position() != position) {
        valid.store(false, std::memory_order_relaxed);
      }
      token.value() = {position, ~static_cast<std::uint64_t>(position)};
      token.publish();
    }
    while (ring.reusable_prefix() != total) {
      std::this_thread::yield();
    }
  });
  std::array<std::thread, 3> consumers;
  for (auto& consumer : consumers) {
    consumer = std::thread([&] {
      while (completed.load(std::memory_order_acquire) < total) {
        auto item = ring.try_acquire();
        if (!item) {
          std::this_thread::yield();
          continue;
        }
        auto token = std::move(item.value());
        const auto position = static_cast<std::size_t>(token.position());
        if (position >= total || token.value().position != position ||
            token.value().inverse != ~static_cast<std::uint64_t>(position)) {
          valid.store(false, std::memory_order_relaxed);
        }
        if (position < total) {
          seen[position].fetch_add(1, std::memory_order_relaxed);
        }
        token.release();
        completed.fetch_add(1, std::memory_order_release);
      }
    });
  }
  producer.join();
  for (auto& consumer : consumers) {
    consumer.join();
  }
  CHECK(valid.load(std::memory_order_relaxed));
  CHECK(ring.reusable_prefix() == total);
  for (const auto& count : seen) {
    CHECK(count.load(std::memory_order_relaxed) == 1);
  }
}
} // namespace

TEST_CASE("SPMC routes preserve unique ownership through repeated physical wrap", "[spmc]") {
  SECTION("serialized") {
    check_parallel_integrity<handoff::spmc::SerializedConsumerRing<Message, 64>>();
  }
  SECTION("ordered release") {
    check_parallel_integrity<handoff::spmc::OrderedReleaseRing<Message, 64>>();
  }
  SECTION("slot completion") {
    check_parallel_integrity<handoff::spmc::SlotCompletionRing<Message, 64>>();
  }
}

TEST_CASE("SPMC routes stop before finite logical rollover", "[spmc]") {
  SECTION("serialized") {
    check_finite_boundary<handoff::spmc::SerializedConsumerRing<Message, 3, std::uint8_t>>();
  }
  SECTION("ordered release") {
    check_finite_boundary<handoff::spmc::OrderedReleaseRing<Message, 3, std::uint8_t>>();
  }
  SECTION("slot completion") {
    check_finite_boundary<handoff::spmc::SlotCompletionRing<Message, 3, std::uint8_t>>();
  }
}
