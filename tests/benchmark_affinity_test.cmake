if(NOT DEFINED BENCHMARK_EXECUTABLE)
  message(FATAL_ERROR "BENCHMARK_EXECUTABLE is required")
endif()

execute_process(
  COMMAND
    "${BENCHMARK_EXECUTABLE}" run throughput --producer-cpu 4294967295 --iterations 1 --warmup 0
    --trials 1
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 1)
  message(FATAL_ERROR "expected exit 1, got ${result}\nstdout: ${output}\nstderr: ${error}")
endif()
string(FIND "${error}" "producer affinity failed: CPU index exceeds CPU_SETSIZE" error_position)
if(error_position EQUAL -1)
  message(FATAL_ERROR "unexpected affinity error: ${error}")
endif()

execute_process(
  COMMAND
    "${BENCHMARK_EXECUTABLE}" run throughput --implementation fan-out --producer-cpu 4294967295
    --consumer-cpus 0,1 --iterations 1 --warmup 0 --trials 1
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 1)
  message(FATAL_ERROR "expected exit 1, got ${result}\nstdout: ${output}\nstderr: ${error}")
endif()
string(FIND "${error}" "producer affinity failed: CPU index exceeds CPU_SETSIZE" error_position)
if(error_position EQUAL -1)
  message(FATAL_ERROR "unexpected multi-consumer affinity error: ${error}")
endif()
