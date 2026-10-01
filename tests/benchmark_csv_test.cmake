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
  if(NOT field_count EQUAL 26)
    message(
      FATAL_ERROR
        "expected 26 ${implementation} CSV fields, got ${field_count}: ${data_line}")
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
  if(NOT field_count EQUAL 26)
    message(FATAL_ERROR "expected 26 ${implementation} CSV fields, got ${field_count}: ${data_line}")
  endif()
  list(GET fields 5 batch_size)
  list(GET fields 6 iterations)
  list(GET fields 13 checksum)
  if(NOT batch_size STREQUAL "${expected_batch_size}" OR NOT iterations STREQUAL "1000" OR
     checksum STREQUAL "")
    message(FATAL_ERROR "unexpected ${implementation} ${benchmark} CSV row: ${data_line}")
  endif()
  if(implementation STREQUAL "fan-out" OR implementation STREQUAL "pipeline")
    file(READ "${output_path}" contents)
    foreach(metadata_pattern IN ITEMS
        "# consumer_count=2"
        "# consumer_0_cpu_requested=not-requested"
        "# consumer_0_cpu_effective=unavailable"
        "# consumer_0_affinity_outcome=not-requested"
        "# consumer_1_cpu_requested=not-requested"
        "# consumer_1_cpu_effective=unavailable"
        "# consumer_1_affinity_outcome=not-requested")
      if(NOT contents MATCHES "${metadata_pattern}")
        message(
          FATAL_ERROR
            "${implementation} metadata matching '${metadata_pattern}' is missing in:\n${contents}")
      endif()
    endforeach()
  endif()
  if(implementation MATCHES "^spmc-")
    file(READ "${output_path}" contents)
    foreach(metadata_pattern IN ITEMS
        "# consumer_count=2"
        "# consumer_0_cpu_requested=not-requested"
        "# consumer_1_cpu_requested=not-requested")
      if(NOT contents MATCHES "${metadata_pattern}")
        message(FATAL_ERROR "${implementation} metadata matching '${metadata_pattern}' is missing")
      endif()
    endforeach()
    list(GET fields 24 consumer_0_count)
    list(GET fields 25 consumer_1_count)
    math(EXPR total_count "${consumer_0_count} + ${consumer_1_count}")
    if(NOT total_count EQUAL 1000)
      message(FATAL_ERROR "${implementation} worker counts do not sum to 1000: ${data_line}")
    endif()
  endif()
  if(implementation STREQUAL "mpsc-count" OR implementation STREQUAL "mpsc-ordered" OR
     implementation STREQUAL "mpsc-serialized" OR implementation STREQUAL "mpsc-slot")
    file(READ "${output_path}" contents)
    foreach(metadata_pattern IN ITEMS
        "# producer_count=2"
        "# producer_0_cpu_requested=not-requested"
        "# producer_0_cpu_effective=unavailable"
        "# producer_0_affinity_outcome=not-requested"
        "# producer_1_cpu_requested=not-requested"
        "# producer_1_cpu_effective=unavailable"
        "# producer_1_affinity_outcome=not-requested"
        "# consumer_cpu_requested=not-requested")
      if(NOT contents MATCHES "${metadata_pattern}")
        message(FATAL_ERROR "${implementation} metadata matching '${metadata_pattern}' is missing")
      endif()
    endforeach()
  endif()
endfunction()

validate_sequence_mode(throughput sequence 1)
validate_sequence_mode(ping-pong sequence "")
validate_sequence_mode(throughput fan-out 1)
validate_sequence_mode(throughput pipeline 1)
validate_sequence_mode(throughput mpsc-ordered 1)
validate_sequence_mode(throughput mpsc-serialized 1)
validate_sequence_mode(throughput mpsc-count 1)
validate_sequence_mode(throughput mpsc-slot 1)
validate_sequence_mode(throughput spmc-serialized 1)
validate_sequence_mode(throughput spmc-ordered 1)
validate_sequence_mode(throughput spmc-slot 1)
validate_sequence_mode(throughput fixed-record 1)
validate_sequence_mode(ping-pong fixed-record "")

function(validate_byte_record_mode benchmark expected_batch_size)
  file(REMOVE "${output_path}")
  execute_process(
    COMMAND
      "${BENCHMARK_EXECUTABLE}" run "${benchmark}" --implementation byte-record --payload-bytes 8
      --capacity-bytes 4096 --iterations 1000 --warmup 100 --trials 1 --output "${output_path}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(
      FATAL_ERROR
        "byte-record ${benchmark} failed with ${result}\nstdout: ${output}\nstderr: ${error}")
  endif()

  file(STRINGS "${output_path}" lines)
  set(data_line "")
  foreach(line IN LISTS lines)
    if(line MATCHES "^${benchmark},byte-record,")
      set(data_line "${line}")
    endif()
  endforeach()
  if(data_line STREQUAL "")
    message(FATAL_ERROR "byte-record ${benchmark} CSV data row is missing")
  endif()
  string(REPLACE "," ";" fields "${data_line}")
  list(LENGTH fields field_count)
  if(NOT field_count EQUAL 26)
    message(FATAL_ERROR "expected 26 byte-record CSV fields, got ${field_count}: ${data_line}")
  endif()
  list(GET fields 3 capacity_slots)
  list(GET fields 4 capacity_bytes)
  list(GET fields 5 batch_size)
  list(GET fields 13 checksum)
  if(NOT capacity_slots STREQUAL "" OR NOT capacity_bytes STREQUAL "4096" OR
     NOT batch_size STREQUAL "${expected_batch_size}" OR checksum STREQUAL "")
    message(FATAL_ERROR "unexpected byte-record ${benchmark} CSV row: ${data_line}")
  endif()
endfunction()

validate_byte_record_mode(throughput 1)
validate_byte_record_mode(ping-pong "")

function(validate_descriptor_record_mode benchmark expected_batch_size)
  file(REMOVE "${output_path}")
  execute_process(
    COMMAND
      "${BENCHMARK_EXECUTABLE}" run "${benchmark}" --implementation descriptor-record
      --payload-bytes 8 --capacity 64 --capacity-bytes 4096 --iterations 1000 --warmup 100
      --trials 1 --output "${output_path}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(
      FATAL_ERROR
        "descriptor-record ${benchmark} failed with ${result}\nstdout: ${output}\nstderr: ${error}")
  endif()

  file(STRINGS "${output_path}" lines)
  set(data_line "")
  foreach(line IN LISTS lines)
    if(line MATCHES "^${benchmark},descriptor-record,")
      set(data_line "${line}")
    endif()
  endforeach()
  if(data_line STREQUAL "")
    message(FATAL_ERROR "descriptor-record ${benchmark} CSV data row is missing")
  endif()
  string(REPLACE "," ";" fields "${data_line}")
  list(LENGTH fields field_count)
  if(NOT field_count EQUAL 26)
    message(
      FATAL_ERROR "expected 26 descriptor-record CSV fields, got ${field_count}: ${data_line}")
  endif()
  list(GET fields 3 capacity_slots)
  list(GET fields 4 capacity_bytes)
  list(GET fields 5 batch_size)
  list(GET fields 13 checksum)
  if(NOT capacity_slots STREQUAL "64" OR NOT capacity_bytes STREQUAL "4096" OR
     NOT batch_size STREQUAL "${expected_batch_size}" OR checksum STREQUAL "")
    message(FATAL_ERROR "unexpected descriptor-record ${benchmark} CSV row: ${data_line}")
  endif()
endfunction()

validate_descriptor_record_mode(throughput 1)
validate_descriptor_record_mode(ping-pong "")

file(REMOVE "${output_path}")

execute_process(
  COMMAND
    "${BENCHMARK_EXECUTABLE}" run throughput --implementation basic --payload-bytes 8
    --capacity 64 --iterations 1000 --warmup 100 --trials 1 --output "${output_path}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "spin throughput failed with ${result}\nstdout: ${output}\nstderr: ${error}")
endif()
file(READ "${output_path}" contents)
if(NOT contents MATCHES "# waiting_behavior=spin\n" OR
   NOT contents MATCHES "throughput,basic,8,64,,1,[0-9]+,1,[0-9]+,[0-9]+\\.[0-9]+,,")
  message(FATAL_ERROR "spin throughput CSV is missing metadata or rate: ${contents}")
endif()
file(REMOVE "${output_path}")

execute_process(
  COMMAND
    "${BENCHMARK_EXECUTABLE}" run ping-pong --implementation basic --payload-bytes 8
    --capacity 64 --iterations 1000 --warmup 100 --trials 1 --output "${output_path}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "spin ping-pong failed with ${result}\nstdout: ${output}\nstderr: ${error}")
endif()
file(READ "${output_path}" contents)
if(NOT contents MATCHES "# waiting_behavior=spin\n" OR
   NOT contents MATCHES "ping-pong,basic,8,64,,,[0-9]+,1,[0-9]+,,[0-9]+\\.[0-9]+,")
  message(FATAL_ERROR "spin ping-pong CSV is missing metadata or RTT: ${contents}")
endif()
file(REMOVE "${output_path}")
execute_process(
  COMMAND "${BENCHMARK_EXECUTABLE}" run publication-hole --payload-bytes 8 --capacity 64
          --output "${output_path}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
  TIMEOUT 30)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "publication-hole failed with ${result}\nstdout: ${output}\nstderr: ${error}")
endif()
file(READ "${output_path}" contents)
if(NOT contents MATCHES
   "benchmark,implementation,payload_bytes,capacity_slots,claims_before_release,payloads_completed_before_release,publication_attempts_rejected_before_release,publication_returns_before_release,visible_before_release,consumer_completions_before_release,further_claim_rejected,final_consumer_completions,checksum")
  message(FATAL_ERROR "publication-hole CSV header is missing: ${contents}")
endif()
if(NOT contents MATCHES "publication-hole,mpsc-ordered,8,64,64,63,63,0,0,0,true,64,[0-9]+")
  message(FATAL_ERROR "publication-hole CSV accounting is wrong: ${contents}")
endif()
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
    "# waiting_behavior=spin"
    "# control_waiting_behavior=atomic-wait"
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
    "benchmark,implementation,payload_bytes,capacity_slots,capacity_bytes,batch_size,iterations,trial,elapsed_ns,messages_per_second,latency_ns,latency_p95_ns,latency_p99_ns,checksum,producer_interval_ns,consumer_stall_every,consumer_stall_ns,offered_messages,observed_messages,overwritten_messages,retry_attempts,observed_payload_bytes,offered_messages_per_second,observed_messages_per_second,consumer_0_count,consumer_1_count")
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
if(NOT field_count EQUAL 26)
  message(FATAL_ERROR "expected 26 CSV fields, got ${field_count}: ${data_line}")
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

foreach(index RANGE 14 25)
  list(GET fields ${index} non_applicable)
  if(NOT non_applicable STREQUAL "")
    message(FATAL_ERROR "throughput field ${index} must be empty: ${data_line}")
  endif()
endforeach()

set(expected_checksum "${checksum}")
file(REMOVE "${output_path}")
execute_process(
  COMMAND
    "${BENCHMARK_EXECUTABLE}" run offered-load --implementation sequence-payload
    --payload-bytes 8 --capacity 1024 --iterations 1000 --warmup 0 --trials 1
    --output "${output_path}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "offered-load failed with ${result}\nstdout: ${output}\nstderr: ${error}")
endif()

file(READ "${output_path}" contents)
foreach(metadata_pattern IN ITEMS
    "# producer_interval_ns=0"
    "# consumer_stall_every=0"
    "# consumer_stall_ns=0")
  if(NOT contents MATCHES "${metadata_pattern}")
    message(FATAL_ERROR "missing offered-load metadata '${metadata_pattern}' in:\n${contents}")
  endif()
endforeach()

file(STRINGS "${output_path}" lines)
set(data_line "")
foreach(line IN LISTS lines)
  if(line MATCHES "^offered-load,sequence-payload,")
    set(data_line "${line}")
  endif()
endforeach()
if(data_line STREQUAL "")
  message(FATAL_ERROR "offered-load CSV data row is missing")
endif()
string(REPLACE "," ";" fields "${data_line}")
list(LENGTH fields field_count)
if(NOT field_count EQUAL 26)
  message(FATAL_ERROR "expected 26 offered-load CSV fields, got ${field_count}: ${data_line}")
endif()
list(GET fields 5 batch_size)
list(GET fields 9 messages_per_second)
list(GET fields 10 latency_ns)
list(GET fields 11 latency_p95_ns)
list(GET fields 12 latency_p99_ns)
list(GET fields 13 checksum)
list(GET fields 14 producer_interval_ns)
list(GET fields 15 consumer_stall_every)
list(GET fields 16 consumer_stall_ns)
list(GET fields 17 offered_messages)
list(GET fields 18 observed_messages)
list(GET fields 19 overwritten_messages)
list(GET fields 20 retry_attempts)
list(GET fields 21 observed_payload_bytes)
list(GET fields 22 offered_rate)
list(GET fields 23 observed_rate)
if(NOT batch_size STREQUAL "" OR NOT messages_per_second STREQUAL "" OR
   NOT latency_ns STREQUAL "" OR NOT latency_p95_ns STREQUAL "" OR
   NOT latency_p99_ns STREQUAL "" OR NOT checksum STREQUAL expected_checksum OR
   NOT producer_interval_ns STREQUAL "0" OR NOT consumer_stall_every STREQUAL "0" OR
   NOT consumer_stall_ns STREQUAL "0" OR NOT offered_messages STREQUAL "1000" OR
   NOT observed_messages STREQUAL "1000" OR NOT overwritten_messages STREQUAL "0" OR
   retry_attempts STREQUAL "" OR NOT observed_payload_bytes STREQUAL "8000" OR
   offered_rate STREQUAL "" OR observed_rate STREQUAL "")
  message(FATAL_ERROR "unexpected offered-load CSV row: ${data_line}")
endif()

file(REMOVE "${output_path}")
