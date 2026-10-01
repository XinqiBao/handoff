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

template <typename Ring> void check_parallel_integrity(std::size_t total = 30'000) {
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

// Bounded observation model for the claim loops in OrderedReleaseRing and
// SlotCompletionRing. Publication and competing claim observations have separate
// modification orders: a relaxed cursor read does not import another consumer's
// publication acquire. We allow every independently stale prefix, including the
// cursor returned by a failed CAS. This models the bound, not the C++ memory model
// or payload synchronization; ordinary threaded tests cannot force these reads.
template <typename Reject> auto count_unauthorized_claims(Reject reject) {
  struct Counts {
    unsigned int initial{};
    unsigned int after_cas_failure{};
    unsigned int unpublished{};
    unsigned int terminal{};
  } counts;
  constexpr unsigned int limit = 3;
  const auto record = [&](unsigned int next, unsigned int observed_published,
                          unsigned int actual_published, bool after_failure) {
    if (reject(next, observed_published)) {
      return;
    }
    if (next >= observed_published) {
      ++(after_failure ? counts.after_cas_failure : counts.initial);
    }
    if (next == actual_published) {
      ++counts.unpublished;
    }
    if (next == limit) {
      ++counts.terminal;
    }
  };
  for (unsigned int published = 0; published <= limit; ++published) {
    for (unsigned int cursor = 0; cursor <= published; ++cursor) {
      for (unsigned int next = 0; next <= cursor; ++next) {
        for (unsigned int observed = 0; observed <= published; ++observed) {
          if (reject(next, observed)) {
            continue;
          }
          if (next == cursor) {
            record(next, observed, published, false); // Successful CAS.
          } else {
            // Failed CAS refreshes next to cursor. Publication read-read
            // coherence forbids an older second publication observation, but
            // still permits one behind the refreshed claim cursor.
            for (auto refreshed = observed; refreshed <= published; ++refreshed) {
              record(cursor, refreshed, published, true);
            }
          }
        }
      }
    }
  }
  return counts;
}
} // namespace

TEST_CASE("SPMC publication bounds reject independently stale observations", "[spmc][model]") {
  const auto equality =
      count_unauthorized_claims([](auto next, auto published) { return next == published; });
  CHECK(equality.initial > 0);
  CHECK(equality.after_cas_failure > 0);
  CHECK(equality.unpublished > 0);
  CHECK(equality.terminal > 0);

  const auto bounded =
      count_unauthorized_claims([](auto next, auto published) { return next >= published; });
  CHECK(bounded.initial == 0);
  CHECK(bounded.after_cas_failure == 0);
  CHECK(bounded.unpublished == 0);
  CHECK(bounded.terminal == 0);
}

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
  SECTION("ordered release at single-slot publication boundary") {
    check_parallel_integrity<handoff::spmc::OrderedReleaseRing<Message, 1>>(3'000);
  }
  SECTION("slot completion at single-slot publication boundary") {
    check_parallel_integrity<handoff::spmc::SlotCompletionRing<Message, 1>>(3'000);
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
