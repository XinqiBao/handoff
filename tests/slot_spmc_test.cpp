#include "handoff/spmc/slot_completion_ring.hpp"
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
using Ring = handoff::spmc::SlotCompletionRing<Message, 3>;
} // namespace

TEST_CASE("slot SPMC later releases return while the first owner is stalled", "[spmc]") {
  Ring ring;
  for (std::uint64_t position = 0; position < 3; ++position) {
    auto claim = required_spmc_token(ring.try_claim());
    claim.value().position = position;
    claim.publish();
  }
  std::latch first_acquired{1};
  std::latch close_hole{1};
  std::latch later_done{2};
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
      token.release();
      later_done.count_down();
    });
  }
  later_done.wait();
  CHECK(owned[0] == 0);
  CHECK((owned[1] == 1 && owned[2] == 2 || owned[1] == 2 && owned[2] == 1));
  CHECK(ring.reusable_prefix() == 0);
  CHECK(!ring.try_claim());
  close_hole.count_down();
  first.join();
  for (auto& worker : later) {
    worker.join();
  }
  CHECK(ring.reusable_prefix() == 3);
  auto wrapped = required_spmc_token(ring.try_claim());
  CHECK(wrapped.position() == 3);
  wrapped.cancel();
}

TEST_CASE("slot SPMC producer discovers completed prefix with a newer owner outstanding",
          "[spmc]") {
  Ring ring;
  for (std::uint64_t position = 0; position < 3; ++position) {
    auto claim = required_spmc_token(ring.try_claim());
    claim.value().position = position;
    claim.publish();
  }
  auto first = required_spmc_token(ring.try_acquire());
  auto second = required_spmc_token(ring.try_acquire());
  auto third = required_spmc_token(ring.try_acquire());
  second.release();
  CHECK(ring.reusable_prefix() == 0);
  CHECK(!ring.try_claim());
  first.release();
  CHECK(ring.reusable_prefix() == 2);
  auto wrapped = required_spmc_token(ring.try_claim());
  CHECK(wrapped.position() == 3);
  wrapped.value().position = 3;
  wrapped.publish();
  CHECK(third.value().position == 2);
  CHECK(ring.reusable_prefix() == 2);
  third.release();
  auto next = required_spmc_token(ring.try_acquire());
  CHECK(next.value().position == 3);
  next.release();
  CHECK(ring.reusable_prefix() == 4);
}

TEST_CASE("slot SPMC hole near the finite limit keeps exact generations", "[spmc]") {
  handoff::spmc::SlotCompletionRing<Message, 3, std::uint8_t> ring;
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
  second.release();
  CHECK(ring.reusable_prefix() == 252);
  first.release();
  CHECK(ring.reusable_prefix() == 254);
  CHECK(third.value().position == 254);
  CHECK(!ring.try_claim());
  third.release();
  CHECK(ring.reusable_prefix() == 255);
  CHECK(!ring.try_claim());
}
