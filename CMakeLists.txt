cmake_minimum_required(VERSION 3.28)

project(
  handoff
  VERSION 0.1.0
  DESCRIPTION "A laboratory for bounded in-memory message handoff mechanisms"
  LANGUAGES CXX)

option(HANDOFF_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(HANDOFF_ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(HANDOFF_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(HANDOFF_ENABLE_CLANG_TIDY "Run clang-tidy while compiling project targets" OFF)
option(HANDOFF_BUILD_BENCHMARK "Build benchmark executable and harness checks" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(HandoffProjectOptions)

handoff_define_project_options()

add_subdirectory(src)
if(HANDOFF_BUILD_BENCHMARK)
  add_subdirectory(apps/handoff-bench)
endif()

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

file(
  GLOB_RECURSE HANDOFF_FORMAT_SOURCES
  CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/apps/*.cpp"
  "${PROJECT_SOURCE_DIR}/apps/*.hpp"
  "${PROJECT_SOURCE_DIR}/include/*.hpp"
  "${PROJECT_SOURCE_DIR}/src/*.cpp"
  "${PROJECT_SOURCE_DIR}/tests/*.cpp"
  "${PROJECT_SOURCE_DIR}/tests/*.hpp")

find_program(HANDOFF_CLANG_FORMAT NAMES clang-format)
if(HANDOFF_CLANG_FORMAT)
  add_custom_target(format
    COMMAND "${HANDOFF_CLANG_FORMAT}" -i ${HANDOFF_FORMAT_SOURCES}
    COMMENT "Formatting project C++ sources"
    VERBATIM)
  add_custom_target(format-check
    COMMAND "${HANDOFF_CLANG_FORMAT}" --dry-run --Werror ${HANDOFF_FORMAT_SOURCES}
    COMMENT "Checking project C++ formatting"
    VERBATIM)
else()
  message(STATUS "clang-format not found; format targets are unavailable")
endif()
