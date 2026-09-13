#include "offered_load_support.hpp"
#include "rate.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("benchmark rate is unavailable for a zero elapsed duration") {
  CHECK_FALSE(handoff::bench::rate_per_second(1, 0));
  CHECK_FALSE(handoff::bench::rate_per_second(1, -1));
}

TEST_CASE("benchmark rate uses the measured positive duration") {
  const auto rate = handoff::bench::rate_per_second(5, 1'000);
  REQUIRE(rate);
  CHECK(rate.value_or(0.0) == 5'000'000.0);
}

TEST_CASE("offered-load resynchronization counts the exact selected distance") {
  const auto oldest_advance = handoff::bench::resynchronize_after_overwrite(3, 7, 10);
  CHECK(oldest_advance.next_sequence == 7);
  CHECK(oldest_advance.skipped_sequences == 4);

  const auto minimum_advance = handoff::bench::resynchronize_after_overwrite(7, 4, 10);
  CHECK(minimum_advance.next_sequence == 8);
  CHECK(minimum_advance.skipped_sequences == 1);

  const auto final_drain = handoff::bench::resynchronize_after_overwrite(8, 20, 10);
  CHECK(final_drain.next_sequence == 11);
  CHECK(final_drain.skipped_sequences == 3);
}
