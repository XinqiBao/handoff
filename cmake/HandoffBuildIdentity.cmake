# Run on every build, not only configuration. Source files must be quiescent
# while the compiler runs; this is an identity stamp, not a source snapshot.
if(NOT DEFINED SOURCE_ROOT OR NOT DEFINED OUTPUT_FILE)
  message(FATAL_ERROR "SOURCE_ROOT and OUTPUT_FILE are required")
endif()

set(build_revision "unavailable")
set(build_dirty "unavailable")
find_program(build_git NAMES git)
if(build_git AND EXISTS "${SOURCE_ROOT}/.git")
  execute_process(COMMAND "${build_git}" -C "${SOURCE_ROOT}" rev-parse --verify HEAD
                  RESULT_VARIABLE revision_result OUTPUT_VARIABLE revision
                  ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(revision_result EQUAL 0)
    set(build_revision "${revision}")
    execute_process(
      COMMAND "${build_git}" -C "${SOURCE_ROOT}" status --porcelain=v1 --untracked-files=normal
      RESULT_VARIABLE status_result OUTPUT_VARIABLE status ERROR_QUIET)
    if(status_result EQUAL 0)
      set(build_dirty "false")
      if(NOT status STREQUAL "")
        set(build_dirty "true")
      endif()
    endif()
  endif()
endif()

file(GLOB_RECURSE inputs RELATIVE "${SOURCE_ROOT}"
     "${SOURCE_ROOT}/apps/*.cpp" "${SOURCE_ROOT}/apps/*.hpp"
     "${SOURCE_ROOT}/apps/CMakeLists.txt"
     "${SOURCE_ROOT}/include/*.hpp" "${SOURCE_ROOT}/src/*.cpp"
     "${SOURCE_ROOT}/src/CMakeLists.txt" "${SOURCE_ROOT}/cmake/*.cmake")
foreach(input IN ITEMS CMakeLists.txt CMakePresets.json)
  if(EXISTS "${SOURCE_ROOT}/${input}")
    list(APPEND inputs "${input}")
  endif()
endforeach()
list(SORT inputs)
set(identity "")
foreach(input IN LISTS inputs)
  file(SHA256 "${SOURCE_ROOT}/${input}" digest)
  string(APPEND identity "${input}:${digest}\n")
endforeach()
string(SHA256 source_digest "${identity}")

set(contents "// Generated at build time. Do not edit.\n#pragma once\n")
string(APPEND contents "#define HANDOFF_BUILD_GIT_REVISION \"${build_revision}\"\n")
string(APPEND contents "#define HANDOFF_BUILD_GIT_DIRTY \"${build_dirty}\"\n")
string(APPEND contents "#define HANDOFF_BUILD_SOURCE_SHA256 \"${source_digest}\"\n")
if(EXISTS "${OUTPUT_FILE}")
  file(READ "${OUTPUT_FILE}" previous)
  if(previous STREQUAL contents)
    return()
  endif()
endif()
get_filename_component(output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
file(WRITE "${OUTPUT_FILE}" "${contents}")
