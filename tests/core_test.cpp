#include "handoff/platform/system_info.hpp"
#include "handoff/platform/thread_affinity.hpp"
#include "handoff/version.hpp"

#include <catch2/catch_test_macros.hpp>

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
  CHECK_FALSE(result.message.empty());
}
#endif
