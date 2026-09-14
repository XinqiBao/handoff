#include "offered_load_support.hpp"
#include "rate.hpp"
#include "workload_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <thread>

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

TEST_CASE("benchmark control notifications release blocked participants") {
  handoff::bench::TrialControl control;
  bool phase_released = false;
  std::thread participant([&] {
    handoff::bench::signal_count(control.ready);
    phase_released = handoff::bench::wait_for_phase(control.begin_warmup, control.cancel);
    handoff::bench::signal_done(control.done);
  });

  handoff::bench::wait_for_count(control.ready, 1);
  handoff::bench::release_phase(control.begin_warmup);
  handoff::bench::wait_for_done(control.done);
  participant.join();
  CHECK(phase_released);
}

TEST_CASE("benchmark cancellation wakes blocked participants") {
  handoff::bench::TrialControl control;
  bool phase_released = true;
  std::thread participant([&] {
    handoff::bench::signal_count(control.ready);
    phase_released = handoff::bench::wait_for_phase(control.begin_timed, control.cancel);
  });

  handoff::bench::wait_for_count(control.ready, 1);
  handoff::bench::cancel_trial(control);
  participant.join();
  CHECK_FALSE(phase_released);
}
