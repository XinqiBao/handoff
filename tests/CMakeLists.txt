include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG 317ac1ed4c0bb6e6b91eafc817e05c488feffcb3)
FetchContent_MakeAvailable(Catch2)
set_property(TARGET Catch2 PROPERTY SYSTEM ON)

add_executable(
  handoff_tests
  benchmark_support_test.cpp
  bounded_spsc_test.cpp
  bulk_burst_spsc_test.cpp
  completion_mpsc_test.cpp
  core_test.cpp
  descriptor_payload_ring_test.cpp
  fixed_record_ring_test.cpp
  ordered_mpsc_test.cpp
  ordered_spmc_test.cpp
  ordered_worker_stage_test.cpp
  sequence_fan_out_test.cpp
  sequence_metadata_ring_test.cpp
  sequence_payload_ring_test.cpp
  sequence_pipeline_test.cpp
  sequence_ring_test.cpp
  serialized_spmc_test.cpp
  slot_spmc_test.cpp
  spmc_shared_test.cpp
  staged_spsc_test.cpp
  variable_record_ring_test.cpp)
target_include_directories(handoff_tests PRIVATE "${PROJECT_SOURCE_DIR}/apps/handoff-bench/internal")
target_link_libraries(handoff_tests PRIVATE handoff::core Catch2::Catch2WithMain)
handoff_configure_target(handoff_tests)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(handoff_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 120)

add_test(NAME benchmark.list COMMAND handoff-bench list)
set_tests_properties(benchmark.list PROPERTIES PASS_REGULAR_EXPRESSION
                                               "throughput.*Steady-state completed handoffs")
add_test(NAME benchmark.list-offered-load COMMAND handoff-bench list)
set_tests_properties(benchmark.list-offered-load PROPERTIES PASS_REGULAR_EXPRESSION
                                                            "offered-load.*Lossy offered and observed")
add_test(
  NAME benchmark.cli
  COMMAND
    "${CMAKE_COMMAND}" -DBENCHMARK_EXECUTABLE=$<TARGET_FILE:handoff-bench> -P
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmark_cli_test.cmake")
add_test(
  NAME benchmark.csv
  COMMAND
    "${CMAKE_COMMAND}" -DBENCHMARK_EXECUTABLE=$<TARGET_FILE:handoff-bench>
    -DTEST_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR} -DEXPECTED_BUILD_MODE=$<CONFIG> -P
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmark_csv_test.cmake")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(
    NAME benchmark.invalid-linux-affinity
    COMMAND
      "${CMAKE_COMMAND}" -DBENCHMARK_EXECUTABLE=$<TARGET_FILE:handoff-bench> -P
      "${CMAKE_CURRENT_SOURCE_DIR}/benchmark_affinity_test.cmake")
endif()
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
  NAME benchmark.offered-load-smoke
  COMMAND handoff-bench run offered-load --implementation sequence-payload --payload-bytes 8
          --capacity 64 --producer-interval-ns 1000 --consumer-stall-every 16
          --consumer-stall-ns 10000 --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.publication-hole
  COMMAND handoff-bench run publication-hole --payload-bytes 8 --capacity 64)
set_tests_properties(benchmark.publication-hole PROPERTIES TIMEOUT 30)
add_test(
  NAME benchmark.count-publication-hole
  COMMAND handoff-bench run publication-hole --implementation mpsc-count --payload-bytes 8
          --capacity 64)
add_test(
  NAME benchmark.slot-publication-hole
  COMMAND handoff-bench run publication-hole --implementation mpsc-slot --payload-bytes 8
          --capacity 64)
add_test(
  NAME benchmark.count-throughput-smoke
  COMMAND handoff-bench run throughput --implementation mpsc-count --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.slot-throughput-smoke
  COMMAND handoff-bench run throughput --implementation mpsc-slot --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
set_tests_properties(benchmark.count-throughput-smoke benchmark.slot-throughput-smoke
                     PROPERTIES PASS_REGULAR_EXPRESSION "batch 1 / 2 producers")
foreach(route IN ITEMS spmc-serialized spmc-ordered spmc-slot)
  add_test(
    NAME benchmark.${route}-throughput-smoke
    COMMAND handoff-bench run throughput --implementation ${route} --payload-bytes 8 --capacity 64
            --iterations 1000 --warmup 100 --trials 2)
  set_tests_properties(benchmark.${route}-throughput-smoke
                       PROPERTIES PASS_REGULAR_EXPRESSION "batch 1 / 2 workers")
endforeach()
add_test(
  NAME benchmark.cache-line-throughput-smoke
  COMMAND handoff-bench run throughput --implementation cache-line --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.cache-line-ping-pong-smoke
  COMMAND handoff-bench run ping-pong --implementation cache-line --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.cached-index-throughput-smoke
  COMMAND handoff-bench run throughput --implementation cached-index --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.cached-index-ping-pong-smoke
  COMMAND handoff-bench run ping-pong --implementation cached-index --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.sequence-throughput-smoke
  COMMAND handoff-bench run throughput --implementation sequence --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.fan-out-throughput-smoke
  COMMAND handoff-bench run throughput --implementation fan-out --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.pipeline-throughput-smoke
  COMMAND handoff-bench run throughput --implementation pipeline --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.fixed-record-throughput-smoke
  COMMAND handoff-bench run throughput --implementation fixed-record --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.fixed-record-ping-pong-smoke
  COMMAND handoff-bench run ping-pong --implementation fixed-record --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.byte-record-throughput-smoke
  COMMAND handoff-bench run throughput --implementation byte-record --payload-bytes 8
          --capacity-bytes 4096 --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.byte-record-ping-pong-smoke
  COMMAND handoff-bench run ping-pong --implementation byte-record --payload-bytes 8
          --capacity-bytes 4096 --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.descriptor-record-throughput-smoke
  COMMAND handoff-bench run throughput --implementation descriptor-record --payload-bytes 8
          --capacity 64 --capacity-bytes 4096 --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.descriptor-record-ping-pong-smoke
  COMMAND handoff-bench run ping-pong --implementation descriptor-record --payload-bytes 8
          --capacity 64 --capacity-bytes 4096 --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.sequence-ping-pong-smoke
  COMMAND handoff-bench run ping-pong --implementation sequence --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.basic-batch-throughput-smoke
  COMMAND handoff-bench run throughput --implementation basic --payload-bytes 8 --capacity 64
          --batch-size 4 --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.batch-throughput-smoke
  COMMAND handoff-bench run throughput --implementation batch --payload-bytes 8 --capacity 64
          --batch-size 4 --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.bulk-throughput-smoke
  COMMAND handoff-bench run throughput --implementation bulk --payload-bytes 8 --capacity 64
          --batch-size 4 --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.burst-throughput-smoke
  COMMAND handoff-bench run throughput --implementation burst --payload-bytes 8 --capacity 64
          --batch-size 4 --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.staged-throughput-smoke
  COMMAND handoff-bench run throughput --implementation staged --payload-bytes 8 --capacity 64
          --batch-size 4 --iterations 1000 --warmup 100 --trials 2)
add_test(
  NAME benchmark.batch-ping-pong-smoke
  COMMAND handoff-bench run ping-pong --implementation batch --payload-bytes 8 --capacity 64
          --iterations 1000 --warmup 100 --trials 2)
add_test(NAME benchmark.invalid-capacity
         COMMAND handoff-bench run throughput --capacity 63 --iterations 1 --warmup 0 --trials 1)
set_tests_properties(benchmark.invalid-capacity PROPERTIES WILL_FAIL TRUE)
