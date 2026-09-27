#include "handoff/spmc/serialized_consumer_ring.hpp"
#include "spmc_test_support.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <latch>
#include <thread>

#include <catch2/catch_test_macros.hpp>

namespace {
struct Message {
  std::uint64_t position{};
  std::uint64_t inverse{};
};
using Ring = handoff::spmc::SerializedConsumerRing<Message, 3>;

void publish(Ring& ring, std::uint64_t expected) {
  auto claim = required_spmc_token(ring.try_claim());
  CHECK(claim.position() == expected);
  claim.value() = {expected, ~expected};
  claim.publish();
}
} // namespace

TEST_CASE("serialized SPMC holds the consumer lock through release", "[spmc]") {
  Ring ring;
  CHECK(!ring.try_acquire());
  auto cancelled = required_spmc_token(ring.try_claim());
  cancelled.value() = {99, 0};
  cancelled.cancel();
  for (std::uint64_t position = 0; position < 3; ++position) {
    publish(ring, position);
  }
  CHECK(!ring.try_claim());

  std::latch first_acquired{1};
  std::latch later_started{2};
  std::latch close_hole{1};
  std::atomic<unsigned int> later_returned{0};
  std::array<std::uint64_t, 3> positions{};
  std::thread first([&] {
    auto item = ring.try_acquire();
    if (item) {
      positions[0] = item->position();
      CHECK(item->value().inverse == ~item->value().position);
      first_acquired.count_down();
      close_hole.wait();
      item->release();
    } else {
      first_acquired.count_down();
    }
  });
  first_acquired.wait();
  std::array<std::thread, 2> later;
  for (std::size_t index = 0; index < later.size(); ++index) {
    later[index] = std::thread([&, index] {
      later_started.count_down();
      auto item = ring.try_acquire();
      if (item) {
        positions[index + 1] = item->position();
        item->release();
        later_returned.fetch_add(1, std::memory_order_release);
      }
    });
  }
  later_started.wait();
  CHECK(ring.reusable_prefix() == 0);
  CHECK(!ring.try_claim());
  CHECK(later_returned.load(std::memory_order_acquire) == 0);
  close_hole.count_down();
  first.join();
  for (auto& worker : later) {
    worker.join();
  }
  CHECK(positions[0] == 0);
  CHECK((positions[1] == 1 && positions[2] == 2 || positions[1] == 2 && positions[2] == 1));
  CHECK(later_returned.load(std::memory_order_acquire) == 2);
  CHECK(ring.reusable_prefix() == 3);
  publish(ring, 3); // Physical slot zero is reused only after its owner releases.
  for (std::uint64_t expected : {std::uint64_t{3}}) {
    auto item = required_spmc_token(ring.try_acquire());
    CHECK(item.position() == expected);
    CHECK(item.value().position == expected);
    CHECK(item.value().inverse == ~expected);
    item.release();
  }
  CHECK(ring.reusable_prefix() == 4);
}

TEST_CASE("serialized SPMC stops before finite sequence rollover", "[spmc]") {
  handoff::spmc::SerializedConsumerRing<Message, 3, std::uint8_t> ring;
  for (unsigned int position = 0; position < 255; ++position) {
    auto claim = required_spmc_token(ring.try_claim());
    claim.value() = {position, ~static_cast<std::uint64_t>(position)};
    claim.publish();
    auto item = required_spmc_token(ring.try_acquire());
    CHECK(item.position() == position);
    CHECK(item.value().position == position);
    item.release();
  }
  CHECK(ring.reusable_prefix() == 255);
  CHECK(!ring.try_claim());
  CHECK(!ring.try_acquire());
}
