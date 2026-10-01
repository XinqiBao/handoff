if(NOT DEFINED BENCHMARK_EXECUTABLE OR NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "BENCHMARK_EXECUTABLE and SOURCE_ROOT are required")
endif()

execute_process(
  COMMAND "${BENCHMARK_EXECUTABLE}" list
  RESULT_VARIABLE list_result
  OUTPUT_VARIABLE catalog_output
  ERROR_VARIABLE list_error)
if(NOT list_result EQUAL 0)
  message(FATAL_ERROR "mechanism catalog is unavailable: ${list_error}")
endif()

file(GLOB mechanism_notes "${SOURCE_ROOT}/docs/mechanisms/*.md")
list(REMOVE_ITEM mechanism_notes "${SOURCE_ROOT}/docs/mechanisms/README.md")
list(LENGTH mechanism_notes note_count)
if(NOT catalog_output MATCHES "Mechanism assets \\(([0-9]+)\\):")
  message(FATAL_ERROR "catalog inventory heading is unavailable")
endif()
if(NOT CMAKE_MATCH_1 EQUAL note_count)
  message(FATAL_ERROR "catalog inventory does not match mechanism notes")
endif()
string(REGEX MATCHALL "\n  [a-z0-9-]+[	](routes:[^\n]*|tests only)" entries "${catalog_output}")
list(LENGTH entries entry_count)
if(NOT entry_count EQUAL note_count)
  message(FATAL_ERROR "catalog classifications do not match mechanism inventory")
endif()
foreach(note IN LISTS mechanism_notes)
  get_filename_component(asset "${note}" NAME_WE)
  if(NOT catalog_output MATCHES "\n  ${asset}[	](routes:|tests only)")
    message(FATAL_ERROR "catalog does not classify ${asset}")
  endif()
  execute_process(
    COMMAND "${BENCHMARK_EXECUTABLE}" describe "${asset}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  string(FIND "${output}" "note: docs/mechanisms/${asset}.md" note_position)
  if(NOT result EQUAL 0 OR note_position EQUAL -1)
    message(FATAL_ERROR "description does not resolve ${asset}: ${output}\n${error}")
  endif()
endforeach()
string(FIND "${catalog_output}" "Benchmark controls: mpsc-serialized" control_heading)
if(control_heading EQUAL -1)
  message(FATAL_ERROR "benchmark control is missing from discovery")
endif()
