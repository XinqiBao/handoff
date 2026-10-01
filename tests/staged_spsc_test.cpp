#include "handoff/spsc/staged_bounded_ring.hpp"

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {

struct Message {
  std::uint64_t sequence{};
  std::uint64_t inverse{};
};

template <typename Reservation, typename Function>
void for_each_slot(Reservation& reservation, Function function) {
  for (auto& slot : reservation.first()) {
    function(slot);
  }
  for (auto& slot : reservation.second()) {
    function(slot);
  }
}

template <typename Reservation>
Reservation require_reservation(std::optional<Reservation> reservation) {
  if (!reservation.has_value()) {
    throw std::logic_error("expected staged reservation");
  }
  return std::move(*reservation);
}

} // namespace

TEST_CASE("staged SPSC reservations reject invalid and unavailable requests") {
  using Ring = handoff::spsc::StagedBoundedRing<std::uint64_t, 4>;
  using ProducerReservation = Ring::ProducerReservation;
  using ConsumerReservation = Ring::ConsumerReservation;
  STATIC_CHECK(std::movable<ProducerReservation>);
  STATIC_CHECK_FALSE(std::copy_constructible<ProducerReservation>);
  STATIC_CHECK(std::movable<ConsumerReservation>);
  STATIC_CHECK_FALSE(std::copy_constructible<ConsumerReservation>);
  STATIC_CHECK(std::same_as<decltype(std::declval<const ConsumerReservation&>().first()),
                            std::span<const std::uint64_t>>);

  Ring ring;
  CHECK_FALSE(ring.try_reserve_push(0));
  CHECK_FALSE(ring.try_reserve_push(5));
  CHECK_FALSE(ring.try_reserve_pop(0));
  CHECK_FALSE(ring.try_reserve_pop(1));
  CHECK_FALSE(ring.try_reserve_pop(5));

  auto producer = require_reservation(ring.try_reserve_push(4));
  CHECK(producer.size() == 4);
  CHECK(producer.first().size() == 4);
  CHECK(producer.second().empty());
  CHECK_FALSE(ring.try_reserve_push(1));
  CHECK_FALSE(ring.try_reserve_pop(1));

  std::uint64_t value = 10;
  for_each_slot(producer, [&](auto& slot) { slot = value++; });
  CHECK_FALSE(ring.try_reserve_pop(1));
  producer.finish();
  CHECK_FALSE(producer.active());
  CHECK_FALSE(ring.try_reserve_push(1));

  auto consumer = require_reservation(ring.try_reserve_pop(4));
  CHECK(consumer.size() == 4);
  CHECK_FALSE(ring.try_reserve_pop(1));
  CHECK_FALSE(ring.try_reserve_push(1));
  consumer.finish();
  CHECK_FALSE(consumer.active());
  CHECK(ring.try_reserve_push(4));
}

TEST_CASE("staged SPSC reservations expose FIFO order through two wrap spans") {
  handoff::spsc::StagedBoundedRing<std::uint64_t, 4> ring;

  auto first_push = require_reservation(ring.try_reserve_push(3));
  std::uint64_t value = 1;
  for_each_slot(first_push, [&](auto& slot) { slot = value++; });
  first_push.finish();

  auto first_pop = require_reservation(ring.try_reserve_pop(2));
  CHECK(first_pop.first()[0] == 1);
  CHECK(first_pop.first()[1] == 2);
  first_pop.finish();

  auto wrapped_push = require_reservation(ring.try_reserve_push(3));
  CHECK(wrapped_push.first().size() == 1);
  CHECK(wrapped_push.second().size() == 2);
  for_each_slot(wrapped_push, [&](auto& slot) { slot = value++; });
  wrapped_push.finish();

  auto wrapped_pop = require_reservation(ring.try_reserve_pop(4));
  CHECK(wrapped_pop.first().size() == 2);
  CHECK(wrapped_pop.second().size() == 2);
  std::array<std::uint64_t, 4> observed{};
  std::size_t offset = 0;
  for_each_slot(wrapped_pop, [&](const auto& slot) { observed[offset++] = slot; });
  CHECK(observed == std::array<std::uint64_t, 4>{3, 4, 5, 6});
  wrapped_pop.finish();
}

TEST_CASE("staged SPSC cancellation and token moves preserve reservations") {
  using Ring = handoff::spsc::StagedBoundedRing<std::uint64_t, 2>;
  Ring first_ring;
  Ring second_ring;

  {
    auto cancelled = require_reservation(first_ring.try_reserve_push(1));
    cancelled.first()[0] = 99;
  }
  CHECK_FALSE(first_ring.try_reserve_pop(1));
  CHECK(first_ring.try_reserve_push(2));

  auto first = require_reservation(first_ring.try_reserve_push(1));
  auto second = require_reservation(second_ring.try_reserve_push(1));
  auto moved = std::move(second);
  CHECK(moved.active());
  first = std::move(moved);
  CHECK(first.active());
  CHECK(first_ring.try_reserve_push(1));
  first.cancel();
  CHECK(second_ring.try_reserve_push(1));

  auto publish = require_reservation(first_ring.try_reserve_push(1));
  publish.first()[0] = 7;
  publish.finish();
  {
    auto cancelled_pop = require_reservation(first_ring.try_reserve_pop(1));
    CHECK(cancelled_pop.first()[0] == 7);
  }
  CHECK_FALSE(first_ring.try_reserve_push(2));
  auto released = require_reservation(first_ring.try_reserve_pop(1));
  released.finish();
  CHECK(first_ring.try_reserve_push(2));
}

TEST_CASE("staged SPSC slots retain resources until overwritten") {
  handoff::spsc::StagedBoundedRing<std::shared_ptr<int>, 1> ring;
  auto first = std::make_shared<int>(10);

  auto producer = require_reservation(ring.try_reserve_push(1));
  producer.first()[0] = first;
  CHECK(first.use_count() == 2);
  producer.finish();

  auto consumer = require_reservation(ring.try_reserve_pop(1));
  REQUIRE(consumer.first()[0]);
  CHECK(*consumer.first()[0] == 10);
  consumer.finish();
  CHECK(first.use_count() == 2);

  auto second = std::make_shared<int>(20);
  producer = require_reservation(ring.try_reserve_push(1));
  producer.first()[0] = second;
  CHECK(first.use_count() == 1);
  CHECK(second.use_count() == 2);
  producer.finish();
}

TEST_CASE("staged SPSC reservations preserve concurrent message integrity") {
  constexpr std::uint64_t message_count = 100'000;
  constexpr std::size_t group_size = 5;
  handoff::spsc::StagedBoundedRing<Message, 64> ring;
  std::atomic<bool> valid{true};

  std::thread producer([&] {
    for (std::uint64_t first = 0; first < message_count; first += group_size) {
      auto reservation = ring.try_reserve_push(group_size);
      while (!reservation) {
        std::this_thread::yield();
        reservation = ring.try_reserve_push(group_size);
      }
      auto token = std::move(reservation).value();
      std::uint64_t sequence = first;
      for_each_slot(token, [&](auto& slot) {
        slot = {.sequence = sequence, .inverse = ~sequence};
        ++sequence;
      });
      token.finish();
    }
  });

  std::thread consumer([&] {
    for (std::uint64_t first = 0; first < message_count; first += group_size) {
      auto reservation = ring.try_reserve_pop(group_size);
      while (!reservation) {
        std::this_thread::yield();
        reservation = ring.try_reserve_pop(group_size);
      }
      auto token = std::move(reservation).value();
      std::uint64_t expected = first;
      for_each_slot(token, [&](const auto& slot) {
        if (slot.sequence != expected || slot.inverse != ~expected) {
          valid.store(false, std::memory_order_relaxed);
        }
        ++expected;
      });
      token.finish();
    }
  });

  producer.join();
  consumer.join();
  CHECK(valid.load(std::memory_order_relaxed));
}
