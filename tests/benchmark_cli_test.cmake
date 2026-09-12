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
expect_failure(
  2
  "producer and consumer CPUs must be different"
  run throughput --producer-cpu 0 --consumer-cpu 0)
expect_failure(
  1
  "unable to open output file"
  run smoke --iterations 1 --warmup 0 --trials 1 --output "${CMAKE_CURRENT_LIST_DIR}")
