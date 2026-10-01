if(NOT DEFINED SOURCE_ROOT OR NOT DEFINED TEST_OUTPUT_DIR OR NOT DEFINED CXX_COMPILER)
  message(FATAL_ERROR "SOURCE_ROOT, TEST_OUTPUT_DIR, and CXX_COMPILER are required")
endif()

find_program(fixture_git git REQUIRED)
set(identity_script "${SOURCE_ROOT}/cmake/HandoffBuildIdentity.cmake")
set(fixture_root "${TEST_OUTPUT_DIR}/provenance-fixture")
set(identity_header "${TEST_OUTPUT_DIR}/provenance-build-identity.hpp")
set(probe_source "${TEST_OUTPUT_DIR}/provenance-probe.cpp")
set(probe_binary "${TEST_OUTPUT_DIR}/provenance-probe")
file(REMOVE_RECURSE "${fixture_root}")
file(MAKE_DIRECTORY "${fixture_root}/include" "${fixture_root}/docs")
file(WRITE "${fixture_root}/include/value.hpp" "#pragma once\ninline constexpr int value = 1;\n")
file(WRITE "${fixture_root}/docs/note.md" "Initial knowledge.\n")

function(run_checked)
  execute_process(
    COMMAND ${ARGV}
    WORKING_DIRECTORY "${fixture_root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "fixture command failed: ${ARGV}\n${result}\n${output}\n${error}")
  endif()
endfunction()

function(read_identity prefix)
  file(READ "${identity_header}" header)
  foreach(field IN ITEMS GIT_REVISION GIT_DIRTY SOURCE_SHA256)
    if(NOT header MATCHES "#define HANDOFF_BUILD_${field} \"([^\"]*)\"")
      message(FATAL_ERROR "missing build identity ${field}: ${header}")
    endif()
    set("${prefix}_${field}" "${CMAKE_MATCH_1}" PARENT_SCOPE)
  endforeach()
endfunction()

function(generate_identity prefix)
  run_checked("${CMAKE_COMMAND}" "-DSOURCE_ROOT=${fixture_root}"
    "-DOUTPUT_FILE=${identity_header}" ${ARGN} -P "${identity_script}")
  read_identity("${prefix}")
  foreach(field IN ITEMS GIT_REVISION GIT_DIRTY SOURCE_SHA256)
    set("${prefix}_${field}" "${${prefix}_${field}}" PARENT_SCOPE)
  endforeach()
endfunction()

run_checked("${fixture_git}" init -q)
run_checked("${fixture_git}" config user.name "Provenance fixture")
run_checked("${fixture_git}" config user.email "fixture@example.invalid")
run_checked("${fixture_git}" add include/value.hpp docs/note.md)
run_checked("${fixture_git}" -c commit.gpgsign=false commit -q --no-verify -m "initial fixture")
generate_identity(clean)
if(NOT clean_GIT_REVISION MATCHES "^[0-9a-f]+$" OR NOT clean_GIT_DIRTY STREQUAL "false" OR
   NOT clean_SOURCE_SHA256 MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "clean fixture build identity is incomplete")
endif()

# Compile a real executable once. Later checkout changes and regenerated metadata
# must not retroactively change the identity embedded in that executable.
file(WRITE "${probe_source}" [=[
#include "provenance-build-identity.hpp"
#include <iostream>
int main() {
  std::cout << HANDOFF_BUILD_GIT_REVISION << '\n'
            << HANDOFF_BUILD_GIT_DIRTY << '\n'
            << HANDOFF_BUILD_SOURCE_SHA256 << '\n';
}
]=])
run_checked("${CXX_COMPILER}" -std=c++23 "${probe_source}" -o "${probe_binary}")
execute_process(COMMAND "${probe_binary}" RESULT_VARIABLE probe_result OUTPUT_VARIABLE original_identity)
if(NOT probe_result EQUAL 0 OR NOT original_identity STREQUAL
   "${clean_GIT_REVISION}\nfalse\n${clean_SOURCE_SHA256}\n")
  message(FATAL_ERROR "executable did not embed its build identity: ${original_identity}")
endif()

file(APPEND "${fixture_root}/include/value.hpp" "inline constexpr int newer_value = 2;\n")
generate_identity(modified)
if(NOT modified_GIT_DIRTY STREQUAL "true" OR
   NOT modified_GIT_REVISION STREQUAL clean_GIT_REVISION OR
   modified_SOURCE_SHA256 STREQUAL clean_SOURCE_SHA256)
  message(FATAL_ERROR "production edit did not refresh build identity without reconfiguration")
endif()
run_checked("${fixture_git}" checkout -- include/value.hpp)

file(APPEND "${fixture_root}/docs/note.md" "Distilled conclusion.\n")
generate_identity(docs)
if(NOT docs_GIT_DIRTY STREQUAL "true" OR NOT docs_SOURCE_SHA256 STREQUAL clean_SOURCE_SHA256)
  message(FATAL_ERROR "documentation edit changed the production-source hash or lost dirty state")
endif()
run_checked("${fixture_git}" add docs/note.md)
run_checked("${fixture_git}" -c commit.gpgsign=false commit -q --no-verify -m "update knowledge")
generate_identity(recommitted)
if(NOT recommitted_GIT_DIRTY STREQUAL "false" OR
   recommitted_GIT_REVISION STREQUAL clean_GIT_REVISION OR
   NOT recommitted_SOURCE_SHA256 STREQUAL clean_SOURCE_SHA256)
  message(FATAL_ERROR "metadata-only commit did not refresh revision independently of source hash")
endif()
execute_process(COMMAND "${probe_binary}" RESULT_VARIABLE probe_result OUTPUT_VARIABLE old_identity)
if(NOT probe_result EQUAL 0 OR NOT old_identity STREQUAL original_identity)
  message(FATAL_ERROR "old executable identity changed after checkout/header updates")
endif()

generate_identity(no_git "-Dbuild_git=OFF")
if(NOT no_git_GIT_REVISION STREQUAL "unavailable" OR
   NOT no_git_GIT_DIRTY STREQUAL "unavailable" OR
   NOT no_git_SOURCE_SHA256 STREQUAL clean_SOURCE_SHA256)
  message(FATAL_ERROR "unavailable Git did not retain an honest source identity")
endif()

# Confirm each production input surface participates, including newly added files.
foreach(input IN ITEMS apps/probe.cpp apps/handoff-bench/CMakeLists.txt
    src/probe.cpp src/CMakeLists.txt include/probe.hpp CMakeLists.txt
    CMakePresets.json cmake/probe.cmake)
  get_filename_component(parent "${fixture_root}/${input}" DIRECTORY)
  file(MAKE_DIRECTORY "${parent}")
  file(WRITE "${fixture_root}/${input}" "fixture input\n")
  generate_identity(added)
  if(added_SOURCE_SHA256 STREQUAL clean_SOURCE_SHA256)
    message(FATAL_ERROR "production hash excludes ${input}")
  endif()
  file(REMOVE "${fixture_root}/${input}")
endforeach()
file(MAKE_DIRECTORY "${fixture_root}/tests" "${fixture_root}/results")
file(WRITE "${fixture_root}/tests/probe.cpp" "disposable test fixture\n")
file(WRITE "${fixture_root}/results/probe.csv" "disposable measurements\n")
generate_identity(excluded)
if(NOT excluded_SOURCE_SHA256 STREQUAL clean_SOURCE_SHA256)
  message(FATAL_ERROR "tests/results unexpectedly participate in the production hash")
endif()

file(REMOVE_RECURSE "${fixture_root}/.git")
# A source export inside the parent checkout must not inherit its Git identity.
run_checked("${CMAKE_COMMAND}" "-DSOURCE_ROOT=${fixture_root}" "-DOUTPUT_FILE=${identity_header}"
  -P "${identity_script}")
read_identity(no_repository)
if(NOT no_repository_GIT_REVISION STREQUAL "unavailable" OR
   NOT no_repository_GIT_DIRTY STREQUAL "unavailable" OR
   NOT no_repository_SOURCE_SHA256 STREQUAL clean_SOURCE_SHA256)
  message(FATAL_ERROR "missing repository did not retain an honest source identity")
endif()

file(REMOVE_RECURSE "${fixture_root}")
file(REMOVE "${identity_header}" "${probe_source}" "${probe_binary}")
