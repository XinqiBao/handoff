#include "handoff/stage/ordered_worker_ring.hpp"

#include <array>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <latch>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {
struct Message {
  std::uint64_t position{};
  std::uint64_t transformed{};
};
using Ring = handoff::stage::OrderedWorkerRing<Message, 3>;

template <typename Token> Token required(std::optional<Token> token) {
  if (!token) {
    throw std::logic_error("expected stage token");
  }
  return std::move(*token);
}

template <typename Stage> void publish(Stage& ring, std::uint64_t position) {
  auto token = required(ring.try_claim());
  CHECK(token.position() == position);
  token.value() = {position, 0};
  token.publish();
}

template <typename Stage> void complete(Stage& ring, std::uint64_t position) {
  auto token = required(ring.try_acquire_worker());
  CHECK(token.position() == position);
  CHECK(token.value().position == position);
  token.value().transformed = position ^ 0x5a5a5a5aULL;
  token.complete();
}

template <typename Stage> void consume(Stage& ring, std::uint64_t position) {
  auto token = required(ring.try_acquire_downstream());
  CHECK(token.position() == position);
  CHECK(token.value().position == position);
  CHECK(token.value().transformed == (position ^ 0x5a5a5a5aULL));
  token.release();
}
} // namespace

TEST_CASE("worker stage has distinct mutable worker and ordered downstream ownership", "[stage]") {
  STATIC_CHECK(std::same_as<decltype(std::declval<Ring::WorkerClaim&>().value()), Message&>);
  STATIC_CHECK(
      std::same_as<decltype(std::declval<Ring::DownstreamClaim&>().value()), const Message&>);
  STATIC_CHECK_FALSE(std::copy_constructible<Ring::WorkerClaim>);
  Ring ring;
  CHECK(ring.completed_prefix() == 0);
  CHECK(!ring.try_acquire_worker());
  CHECK(!ring.try_acquire_downstream());
  auto cancelled = required(ring.try_claim());
  cancelled.value().position = 99;
  cancelled.cancel();
  publish(ring, 0);
  CHECK(ring.published_prefix() == 1);
  CHECK(!ring.try_acquire_downstream());
  complete(ring, 0);
  CHECK(ring.completed_prefix() == 1);
  consume(ring, 0);
  CHECK(ring.released_prefix() == 1);
}

TEST_CASE("later completion returns across a held worker hole", "[stage]") {
  Ring ring;
  for (std::uint64_t position = 0; position < 3; ++position) {
    publish(ring, position);
  }
  std::latch first_acquired{1};
  std::latch close_hole{1};
  std::latch later_complete{1};
  std::latch third_acquired{1};
  std::latch finish_third{1};
  std::atomic<bool> owned_expected{true};
  std::thread first([&] {
    auto token = required(ring.try_acquire_worker());
    if (token.position() != 0) {
      owned_expected.store(false, std::memory_order_relaxed);
    }
    first_acquired.count_down();
    close_hole.wait();
    token.value().transformed = 0x5a5a5a5aULL;
    token.complete();
  });
  first_acquired.wait();
  std::thread second([&] {
    auto token = required(ring.try_acquire_worker());
    if (token.position() != 1) {
      owned_expected.store(false, std::memory_order_relaxed);
    }
    token.value().transformed = 1 ^ 0x5a5a5a5aULL;
    token.complete();
    later_complete.count_down();
    auto third = required(ring.try_acquire_worker());
    if (third.position() != 2) {
      owned_expected.store(false, std::memory_order_relaxed);
    }
    third_acquired.count_down();
    finish_third.wait();
    third.value().transformed = 2 ^ 0x5a5a5a5aULL;
    third.complete();
  });
  later_complete.wait();
  third_acquired.wait();
  CHECK(ring.completed_prefix() == 0);
  CHECK(!ring.try_acquire_downstream());
  CHECK(!ring.try_claim());
  close_hole.count_down();
  first.join();
  CHECK(ring.completed_prefix() == 2);
  consume(ring, 0);
  consume(ring, 1);
  CHECK(!ring.try_acquire_downstream());
  publish(ring, 3);
  publish(ring, 4);
  CHECK(ring.completed_prefix() == 2);
  finish_third.count_down();
  second.join();
  CHECK(owned_expected.load(std::memory_order_relaxed));
  CHECK(ring.completed_prefix() == 3);
  auto held = required(ring.try_acquire_downstream());
  CHECK(held.position() == 2);
  CHECK(!ring.try_claim());
  held.release();
  auto reusable = required(ring.try_claim());
  CHECK(reusable.position() == 5);
  reusable.cancel();
  CHECK(ring.completed_prefix() == 3); // The prior physical generation does not complete 3.
  CHECK(!ring.try_acquire_downstream());
  complete(ring, 3);
  consume(ring, 3);
  complete(ring, 4);
  consume(ring, 4);
}

TEST_CASE("worker completion does not release full-capacity slots", "[stage]") {
  handoff::stage::OrderedWorkerRing<Message, 2> ring;
  publish(ring, 0);
  publish(ring, 1);
  complete(ring, 0);
  complete(ring, 1);
  CHECK(ring.completed_prefix() == 2);
  CHECK(!ring.try_claim());
  auto first = required(ring.try_acquire_downstream());
  CHECK(!ring.try_acquire_downstream());
  CHECK(!ring.try_claim());
  first.release();
  publish(ring, 2);
  CHECK(ring.completed_prefix() == 2);
  consume(ring, 1);
  complete(ring, 2);
  consume(ring, 2);
}

TEST_CASE("worker stage stops before finite logical rollover", "[stage]") {
  handoff::stage::OrderedWorkerRing<Message, 3, std::uint8_t> ring;
  for (unsigned int position = 0; position < 255; ++position) {
    publish(ring, position);
    complete(ring, position);
    consume(ring, position);
  }
  CHECK(ring.completed_prefix() == 255);
  CHECK(ring.released_prefix() == 255);
  CHECK(!ring.try_claim());
  CHECK(!ring.try_acquire_worker());
  CHECK(!ring.try_acquire_downstream());
}

TEST_CASE("worker stage preserves concurrent integrity across physical wrap", "[stage]") {
  constexpr std::size_t total = 30'000;
  handoff::stage::OrderedWorkerRing<Message, 31> ring;
  std::vector<std::atomic<unsigned int>> seen(total);
  std::atomic<std::size_t> completed{0};
  std::atomic<bool> valid{true};
  std::thread producer([&] {
    for (std::size_t position = 0; position < total; ++position) {
      auto token = [&]() -> decltype(required(ring.try_claim())) {
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
      token.value() = {position, 0};
      token.publish();
    }
  });
  std::array<std::thread, 2> workers;
  for (auto& worker : workers) {
    worker = std::thread([&] {
      while (completed.load(std::memory_order_acquire) < total) {
        auto attempt = ring.try_acquire_worker();
        if (!attempt) {
          std::this_thread::yield();
          continue;
        }
        auto token = std::move(*attempt);
        const auto position = static_cast<std::size_t>(token.position());
        if (position >= total || token.value().position != position) {
          valid.store(false, std::memory_order_relaxed);
        }
        if (position < total) {
          seen[position].fetch_add(1, std::memory_order_relaxed);
        }
        token.value().transformed = position ^ 0x5a5a5a5aULL;
        token.complete();
        completed.fetch_add(1, std::memory_order_release);
      }
    });
  }
  std::thread downstream([&] {
    for (std::size_t position = 0; position < total; ++position) {
      auto token = [&]() -> decltype(required(ring.try_acquire_downstream())) {
        for (;;) {
          auto attempt = ring.try_acquire_downstream();
          if (attempt) {
            return std::move(*attempt);
          }
          std::this_thread::yield();
        }
      }();
      if (token.position() != position || token.value().position != position ||
          token.value().transformed != (position ^ 0x5a5a5a5aULL)) {
        valid.store(false, std::memory_order_relaxed);
      }
      token.release();
    }
  });
  producer.join();
  for (auto& worker : workers) {
    worker.join();
  }
  downstream.join();
  CHECK(valid.load(std::memory_order_relaxed));
  CHECK(completed.load(std::memory_order_relaxed) == total);
  CHECK(ring.completed_prefix() == total);
  CHECK(ring.released_prefix() == total);
  for (const auto& count : seen) {
    CHECK(count.load(std::memory_order_relaxed) == 1);
  }
}
