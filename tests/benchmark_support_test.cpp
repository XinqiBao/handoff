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
