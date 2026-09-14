if(NOT DEFINED BENCHMARK_EXECUTABLE)
  message(FATAL_ERROR "BENCHMARK_EXECUTABLE is required")
endif()

function(expect_failure expected_exit expected_error)
  execute_process(
    COMMAND "${BENCHMARK_EXECUTABLE}" ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(NOT result EQUAL expected_exit)
    message(FATAL_ERROR "expected exit ${expected_exit}, got ${result}\nstdout: ${output}\nstderr: ${error}")
  endif()
  string(FIND "${error}" "${expected_error}" error_position)
  if(error_position EQUAL -1)
    message(FATAL_ERROR "expected stderr to contain '${expected_error}', got: ${error}")
  endif()
endfunction()

expect_failure(2 "unknown option: --unknown" run throughput --unknown)
expect_failure(2 "option --implementation does not apply to smoke" run smoke --implementation basic)
expect_failure(2 "option --batch-size does not apply to ping-pong" run ping-pong --batch-size 4)
expect_failure(
  2 "option --batch-size does not apply to offered-load" run offered-load --batch-size 4)
expect_failure(
  2 "offered-load requires implementation sequence-payload"
  run offered-load --implementation basic)
expect_failure(
  2 "implementation sequence-payload applies only to offered-load"
  run throughput --implementation sequence-payload)
expect_failure(
  2 "option --producer-interval-ns applies only to offered-load"
  run throughput --producer-interval-ns 1)
expect_failure(
  2 "--producer-interval-ns must be in the range 0..1000000"
  run offered-load --producer-interval-ns 1000001)
expect_failure(
  2 "--consumer-stall-every must be in the range 0..1000000"
  run offered-load --consumer-stall-every 1000001)
expect_failure(
  2 "--consumer-stall-ns must be in the range 0..1000000000"
  run offered-load --consumer-stall-ns 1000000001)
expect_failure(
  2 "--consumer-stall-every and --consumer-stall-ns must both be zero or both be positive"
  run offered-load --consumer-stall-every 1)
expect_failure(
  2 "--iterations plus --warmup exceeds the sequence-payload range"
  run offered-load --iterations 18446744073709551615 --warmup 0)
expect_failure(
  2
  "--batch-size greater than 1 requires implementation basic, batch, bulk, burst, or staged"
  run throughput --implementation cached-index --batch-size 4)
expect_failure(
  2
  "--batch-size greater than 1 requires implementation basic, batch, bulk, burst, or staged"
  run throughput --implementation sequence --batch-size 4)
expect_failure(
  2
  "--batch-size greater than 1 requires implementation basic, batch, bulk, burst, or staged"
  run throughput --implementation fan-out --batch-size 4)
expect_failure(
  2
  "--batch-size greater than 1 requires implementation basic, batch, bulk, burst, or staged"
  run throughput --implementation fixed-record --batch-size 4)
expect_failure(
  2
  "--batch-size greater than 1 requires implementation basic, batch, bulk, burst, or staged"
  run throughput --implementation byte-record --batch-size 4)
expect_failure(
  2
  "--batch-size greater than 1 requires implementation basic, batch, bulk, burst, or staged"
  run throughput --implementation descriptor-record --batch-size 4)
expect_failure(
  2
  "--capacity does not apply to byte-record; use --capacity-bytes"
  run throughput --implementation byte-record --capacity 64)
expect_failure(
  2
  "--capacity-bytes requires implementation byte-record or descriptor-record"
  run throughput --implementation basic --capacity-bytes 4096)
expect_failure(
  2
  "--capacity-bytes must be one of: 4096, 65536"
  run throughput --implementation byte-record --capacity-bytes 8192)
expect_failure(
  2
  "descriptor-record requires --capacity and --capacity-bytes together"
  run throughput --implementation descriptor-record --capacity 64)
expect_failure(
  2
  "descriptor-record requires --capacity and --capacity-bytes together"
  run throughput --implementation descriptor-record --capacity-bytes 4096)
expect_failure(
  2
  "descriptor-record capacity pairs must be 64/4096 or 1024/65536"
  run throughput --implementation descriptor-record --capacity 64 --capacity-bytes 65536)
expect_failure(
  2
  "--batch-size greater than 1 requires implementation basic, batch, bulk, burst, or staged"
  run throughput --implementation pipeline --batch-size 4)
expect_failure(
  2
  "implementations bulk, burst, fan-out, pipeline, and staged apply only to throughput"
  run ping-pong --implementation bulk)
expect_failure(
  2
  "implementations bulk, burst, fan-out, pipeline, and staged apply only to throughput"
  run ping-pong --implementation burst)
expect_failure(
  2
  "implementations bulk, burst, fan-out, pipeline, and staged apply only to throughput"
  run ping-pong --implementation staged)
expect_failure(
  2
  "implementations bulk, burst, fan-out, pipeline, and staged apply only to throughput"
  run ping-pong --implementation fan-out)
expect_failure(
  2
  "implementations bulk, burst, fan-out, pipeline, and staged apply only to throughput"
  run ping-pong --implementation pipeline)
expect_failure(
  2
  "--iterations and --warmup must be divisible by --batch-size"
  run throughput --implementation batch --batch-size 4 --iterations 10 --warmup 4)
expect_failure(
  2
  "--iterations plus --warmup exceeds the sequence range"
  run throughput --implementation sequence --iterations 18446744073709551615 --warmup 1)
expect_failure(
  2
  "--iterations plus --warmup exceeds the sequence range"
  run throughput --implementation fan-out --iterations 18446744073709551615 --warmup 1)
expect_failure(
  2
  "--iterations plus --warmup exceeds the sequence range"
  run throughput --implementation pipeline --iterations 18446744073709551615 --warmup 1)
expect_failure(
  2
  "multi-consumer placement requires --producer-cpu and --consumer-cpus together"
  run throughput --implementation fan-out --producer-cpu 0)
expect_failure(
  2
  "--consumer-cpu does not apply to multi-consumer implementations; use --consumer-cpus"
  run throughput --implementation pipeline --consumer-cpu 1)
expect_failure(
  2
  "--consumer-cpus must contain exactly two non-negative integers separated by a comma"
  run throughput --implementation fan-out --consumer-cpus 1)
expect_failure(
  2
  "--consumer-cpus must contain exactly two non-negative integers separated by a comma"
  run throughput --implementation fan-out --consumer-cpus 1,2,3)
expect_failure(
  2
  "multi-consumer placement requires --producer-cpu and --consumer-cpus together"
  run throughput --implementation pipeline --consumer-cpus 1,2)
expect_failure(
  2
  "multi-consumer CPUs must be distinct"
  run throughput --implementation fan-out --producer-cpu 0 --consumer-cpus 1,1)
expect_failure(
  2
  "producer and consumer CPUs must be distinct"
  run throughput --implementation pipeline --producer-cpu 1 --consumer-cpus 1,2)
expect_failure(
  2
  "--consumer-cpus applies only to multi-consumer implementations"
  run throughput --implementation basic --consumer-cpus 1,2)
expect_failure(
  2
  "producer and consumer CPUs must be different"
  run throughput --producer-cpu 0 --consumer-cpu 0)
expect_failure(
  1
  "unable to open output file"
  run smoke --iterations 1 --warmup 0 --trials 1 --output "${CMAKE_CURRENT_LIST_DIR}")
