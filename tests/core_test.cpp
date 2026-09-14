#include "handoff/platform/system_info.hpp"
#include "handoff/platform/thread_affinity.hpp"
#include "handoff/version.hpp"

#include <catch2/catch_test_macros.hpp>

#if defined(__linux__)
#include <sched.h>

#include <optional>
#include <thread>
#endif

TEST_CASE("the core reports its version") { CHECK(handoff::version().compare("0.1.0") == 0); }

TEST_CASE("system information identifies the build environment") {
  const auto info = handoff::platform::current_system_info();
  CHECK_FALSE(info.operating_system.empty());
  CHECK_FALSE(info.architecture.empty());
  CHECK_FALSE(info.compiler.empty());
  CHECK_FALSE(info.compiler_version.empty());
  CHECK_FALSE(info.cpu_model.empty());
}

#if defined(__APPLE__)
TEST_CASE("macOS reports thread affinity as unsupported") {
  const auto result = handoff::platform::pin_current_thread(0);
  CHECK(result.status == handoff::platform::AffinityStatus::unsupported);
  CHECK_FALSE(result.effective_cpu);
  CHECK_FALSE(result.message.empty());
}
#endif

#if defined(__linux__)
TEST_CASE("Linux verifies the effective thread affinity mask") {
  int current_cpu = -1;
  std::optional<handoff::platform::AffinityResult> result;
  std::thread worker([&] {
    current_cpu = sched_getcpu();
    if (current_cpu >= 0) {
      result = handoff::platform::pin_current_thread(static_cast<unsigned int>(current_cpu));
    }
  });
  worker.join();

  REQUIRE(current_cpu >= 0);
  if (!result) {
    FAIL("worker did not report an affinity result");
    return;
  }
  const auto& affinity = *result;
  CHECK(affinity.status == handoff::platform::AffinityStatus::applied);
  if (!affinity.effective_cpu) {
    FAIL("applied affinity did not report an effective CPU");
    return;
  }
  CHECK(*affinity.effective_cpu == static_cast<unsigned int>(current_cpu));
  CHECK_FALSE(affinity.message.empty());
}
#endif
