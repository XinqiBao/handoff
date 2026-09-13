cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED BENCHMARK_EXECUTABLE OR NOT DEFINED TEST_OUTPUT_DIR OR
   NOT DEFINED EXPECTED_BUILD_MODE)
  message(FATAL_ERROR "BENCHMARK_EXECUTABLE, TEST_OUTPUT_DIR, and EXPECTED_BUILD_MODE are required")
endif()

set(output_path "${TEST_OUTPUT_DIR}/benchmark-schema.csv")

function(validate_group_mode implementation)
  file(REMOVE "${output_path}")
  execute_process(
    COMMAND
      "${BENCHMARK_EXECUTABLE}" run throughput --implementation "${implementation}"
      --payload-bytes 8 --capacity 64 --batch-size 4 --iterations 1000 --warmup 100 --trials 1
      --output "${output_path}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(
      FATAL_ERROR
        "${implementation} benchmark failed with ${result}\nstdout: ${output}\nstderr: ${error}")
  endif()

  file(STRINGS "${output_path}" lines)
  set(data_line "")
  foreach(line IN LISTS lines)
    if(line MATCHES "^throughput,${implementation},")
      set(data_line "${line}")
    endif()
  endforeach()
  if(data_line STREQUAL "")
    message(FATAL_ERROR "${implementation} throughput CSV data row is missing")
  endif()
  string(REPLACE "," ";" fields "${data_line}")
  list(LENGTH fields field_count)
  if(NOT field_count EQUAL 14)
    message(
      FATAL_ERROR
        "expected 14 ${implementation} CSV fields, got ${field_count}: ${data_line}")
  endif()
  list(GET fields 5 batch_size)
  list(GET fields 6 iterations)
  list(GET fields 13 checksum)
  if(NOT batch_size STREQUAL "4" OR NOT iterations STREQUAL "1000" OR checksum STREQUAL "")
    message(FATAL_ERROR "unexpected ${implementation} throughput CSV row: ${data_line}")
  endif()
endfunction()

validate_group_mode(burst)
validate_group_mode(staged)

function(validate_sequence_mode benchmark implementation expected_batch_size)
  file(REMOVE "${output_path}")
  execute_process(
    COMMAND
      "${BENCHMARK_EXECUTABLE}" run "${benchmark}" --implementation "${implementation}"
      --payload-bytes 8
      --capacity 64 --iterations 1000 --warmup 100 --trials 1 --output "${output_path}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(
      FATAL_ERROR
        "${implementation} ${benchmark} failed with ${result}\nstdout: ${output}\nstderr: ${error}")
  endif()

  file(STRINGS "${output_path}" lines)
  set(data_line "")
  foreach(line IN LISTS lines)
    if(line MATCHES "^${benchmark},${implementation},")
      set(data_line "${line}")
    endif()
  endforeach()
  if(data_line STREQUAL "")
    message(FATAL_ERROR "${implementation} ${benchmark} CSV data row is missing")
  endif()
  string(REPLACE "," ";" fields "${data_line}")
  list(LENGTH fields field_count)
  if(NOT field_count EQUAL 14)
    message(FATAL_ERROR "expected 14 ${implementation} CSV fields, got ${field_count}: ${data_line}")
  endif()
  list(GET fields 5 batch_size)
  list(GET fields 6 iterations)
  list(GET fields 13 checksum)
  if(NOT batch_size STREQUAL "${expected_batch_size}" OR NOT iterations STREQUAL "1000" OR
     checksum STREQUAL "")
    message(FATAL_ERROR "unexpected ${implementation} ${benchmark} CSV row: ${data_line}")
  endif()
  if(implementation STREQUAL "fan-out")
    file(READ "${output_path}" contents)
    foreach(metadata_pattern IN ITEMS
        "# consumer_count=2"
        "# consumer_cpus_requested=not-requested"
        "# consumer_cpus_effective=unavailable"
        "# consumer_affinity_outcome=not-requested")
      if(NOT contents MATCHES "${metadata_pattern}")
        message(FATAL_ERROR "fan-out metadata matching '${metadata_pattern}' is missing in:\n${contents}")
      endif()
    endforeach()
  endif()
endfunction()

validate_sequence_mode(throughput sequence 1)
validate_sequence_mode(ping-pong sequence "")
validate_sequence_mode(throughput fan-out 1)

file(REMOVE "${output_path}")
execute_process(
  COMMAND
    "${BENCHMARK_EXECUTABLE}" run throughput --implementation basic --payload-bytes 8 --capacity 64
    --iterations 1000 --warmup 100 --trials 1 --output "${output_path}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "benchmark failed with ${result}\nstdout: ${output}\nstderr: ${error}")
endif()

file(READ "${output_path}" contents)
foreach(metadata_pattern IN ITEMS
    "# git_revision=[0-9a-f]+"
    "# git_dirty=(true|false)"
    "# compiler=Clang"
    "# compiler_version=[^\n]+"
    "# build_mode=${EXPECTED_BUILD_MODE}"
    "# operating_system=[^\n]+"
    "# architecture=[^\n]+"
    "# cpu_model=[^\n]+"
    "# waiting_behavior=yield"
    "# producer_cpu_requested=not-requested"
    "# producer_cpu_effective=unavailable"
    "# producer_affinity_outcome=not-requested"
    "# consumer_cpu_requested=not-requested"
    "# consumer_cpu_effective=unavailable"
    "# consumer_affinity_outcome=not-requested")
  if(NOT contents MATCHES "${metadata_pattern}")
    message(FATAL_ERROR "missing metadata matching '${metadata_pattern}' in:\n${contents}")
  endif()
endforeach()

set(expected_header
    "benchmark,implementation,payload_bytes,capacity_slots,capacity_bytes,batch_size,iterations,trial,elapsed_ns,messages_per_second,latency_ns,latency_p95_ns,latency_p99_ns,checksum")
file(STRINGS "${output_path}" lines)
set(data_line "")
foreach(line IN LISTS lines)
  if(line MATCHES "^benchmark,")
    if(NOT line STREQUAL expected_header)
      message(FATAL_ERROR "unexpected CSV header: ${line}")
    endif()
  elseif(line MATCHES "^throughput,")
    set(data_line "${line}")
  endif()
endforeach()
if(data_line STREQUAL "")
  message(FATAL_ERROR "throughput CSV data row is missing")
endif()

string(REPLACE "," ";" fields "${data_line}")
list(LENGTH fields field_count)
if(NOT field_count EQUAL 14)
  message(FATAL_ERROR "expected 14 CSV fields, got ${field_count}: ${data_line}")
endif()

list(GET fields 0 benchmark)
list(GET fields 1 implementation)
list(GET fields 2 payload_bytes)
list(GET fields 3 capacity_slots)
list(GET fields 4 capacity_bytes)
list(GET fields 5 batch_size)
list(GET fields 6 iterations)
list(GET fields 7 trial)
list(GET fields 10 latency_ns)
list(GET fields 11 latency_p95_ns)
list(GET fields 12 latency_p99_ns)
list(GET fields 13 checksum)
if(NOT benchmark STREQUAL "throughput" OR NOT implementation STREQUAL "basic" OR
   NOT payload_bytes STREQUAL "8" OR NOT capacity_slots STREQUAL "64" OR
   NOT capacity_bytes STREQUAL "" OR NOT batch_size STREQUAL "1" OR
   NOT iterations STREQUAL "1000" OR NOT trial STREQUAL "1" OR
   NOT latency_ns STREQUAL "" OR NOT latency_p95_ns STREQUAL "" OR
   NOT latency_p99_ns STREQUAL "" OR checksum STREQUAL "")
  message(FATAL_ERROR "unexpected throughput CSV row: ${data_line}")
endif()

file(REMOVE "${output_path}")
