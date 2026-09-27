#include "handoff/spmc/ordered_release_ring.hpp"
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
};
using Ring = handoff::spmc::OrderedReleaseRing<Message, 3>;
} // namespace

TEST_CASE("ordered SPMC releases wait across an early owner hole", "[spmc]") {
  Ring ring;
  for (std::uint64_t position = 0; position < 3; ++position) {
    auto claim = required_spmc_token(ring.try_claim());
    claim.value().position = position;
    claim.publish();
  }
  CHECK(!ring.try_claim());

  std::latch first_acquired{1};
  std::latch close_hole{1};
  std::latch later_attempted{2};
  std::atomic<unsigned int> later_returned{0};
  std::array<std::uint64_t, 3> owned{};
  std::thread first([&] {
    auto token = required_spmc_token(ring.try_acquire());
    owned[0] = token.position();
    first_acquired.count_down();
    close_hole.wait();
    token.release();
  });
  first_acquired.wait();
  std::array<std::thread, 2> later;
  for (std::size_t index = 0; index < later.size(); ++index) {
    later[index] = std::thread([&, index] {
      auto token = required_spmc_token(ring.try_acquire());
      owned[index + 1] = token.position();
      const bool returned = token.try_release();
      later_attempted.count_down();
      if (!returned) {
        token.release();
      }
      later_returned.fetch_add(1, std::memory_order_release);
    });
  }
  later_attempted.wait();
  CHECK(owned[0] == 0);
  CHECK((owned[1] == 1 && owned[2] == 2 || owned[1] == 2 && owned[2] == 1));
  CHECK(later_returned.load(std::memory_order_acquire) == 0);
  CHECK(ring.reusable_prefix() == 0);
  CHECK(!ring.try_claim());
  close_hole.count_down();
  first.join();
  for (auto& worker : later) {
    worker.join();
  }
  CHECK(later_returned.load(std::memory_order_acquire) == 2);
  CHECK(ring.reusable_prefix() == 3);
  auto wrapped = required_spmc_token(ring.try_claim());
  CHECK(wrapped.position() == 3);
  wrapped.cancel();
}

TEST_CASE("ordered SPMC cannot reclaim a ready prefix before its later release caller", "[spmc]") {
  Ring ring;
  for (std::uint64_t position = 0; position < 3; ++position) {
    auto claim = required_spmc_token(ring.try_claim());
    claim.value().position = position;
    claim.publish();
  }
  auto first = required_spmc_token(ring.try_acquire());
  auto second = required_spmc_token(ring.try_acquire());
  auto third = required_spmc_token(ring.try_acquire());
  CHECK(!ring.try_acquire());
  CHECK(!second.try_release());
  first.release();
  CHECK(ring.reusable_prefix() == 1);
  auto first_reuse = required_spmc_token(ring.try_claim());
  CHECK(first_reuse.position() == 3);
  first_reuse.value().position = 3;
  first_reuse.publish();
  second.release();
  CHECK(ring.reusable_prefix() == 2);
  auto wrapped = required_spmc_token(ring.try_claim());
  CHECK(wrapped.position() == 4);
  wrapped.value().position = 4;
  wrapped.publish();
  third.release();
  for (std::uint64_t position : {std::uint64_t{3}, std::uint64_t{4}}) {
    auto item = required_spmc_token(ring.try_acquire());
    CHECK(item.value().position == position);
    item.release();
  }
}

TEST_CASE("ordered SPMC hole near the finite limit cannot roll over", "[spmc]") {
  handoff::spmc::OrderedReleaseRing<Message, 3, std::uint8_t> ring;
  for (unsigned int position = 0; position < 252; ++position) {
    auto claim = required_spmc_token(ring.try_claim());
    claim.value().position = position;
    claim.publish();
    auto item = required_spmc_token(ring.try_acquire());
    item.release();
  }
  for (unsigned int position = 252; position < 255; ++position) {
    auto claim = required_spmc_token(ring.try_claim());
    claim.value().position = position;
    claim.publish();
  }
  auto first = required_spmc_token(ring.try_acquire());
  auto second = required_spmc_token(ring.try_acquire());
  auto third = required_spmc_token(ring.try_acquire());
  CHECK(!second.try_release());
  CHECK(ring.reusable_prefix() == 252);
  first.release();
  second.release();
  CHECK(ring.reusable_prefix() == 254);
  CHECK(third.value().position == 254);
  CHECK(!ring.try_claim());
  third.release();
  CHECK(ring.reusable_prefix() == 255);
  CHECK(!ring.try_claim());
}
