include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG 317ac1ed4c0bb6e6b91eafc817e05c488feffcb3)
FetchContent_MakeAvailable(Catch2)
set_property(TARGET Catch2 PROPERTY SYSTEM ON)

add_executable(handoff_tests basic_bounded_ring_test.cpp core_test.cpp)
target_link_libraries(handoff_tests PRIVATE handoff::core Catch2::Catch2WithMain)
handoff_configure_target(handoff_tests)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(handoff_tests DISCOVERY_MODE PRE_TEST)

add_test(NAME benchmark.list COMMAND handoff-bench list)
add_test(
  NAME benchmark.smoke
  COMMAND handoff-bench run smoke --iterations 1000 --warmup 100 --trials 1)
add_test(
  NAME benchmark.throughput-smoke
  COMMAND handoff-bench run throughput --implementation basic --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.ping-pong-smoke
  COMMAND handoff-bench run ping-pong --implementation basic --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.cache-line-throughput-smoke
  COMMAND handoff-bench run throughput --implementation cache-line --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.cache-line-ping-pong-smoke
  COMMAND handoff-bench run ping-pong --implementation cache-line --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(NAME benchmark.invalid-capacity
         COMMAND handoff-bench run throughput --capacity 63 --iterations 1 --warmup 0 --trials 1)
set_tests_properties(benchmark.invalid-capacity PROPERTIES WILL_FAIL TRUE)
