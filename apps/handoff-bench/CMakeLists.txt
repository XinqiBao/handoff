add_executable(
  handoff-bench
  main.cpp
  src/catalog.cpp
  src/command.cpp
  src/fan_out.cpp
  src/mpsc.cpp
  src/offered_load.cpp
  src/output.cpp
  src/pipeline.cpp
  src/ping_pong.cpp
  src/publication_hole.cpp
  src/run_metadata.cpp
  src/spmc.cpp
  src/throughput.cpp)
target_include_directories(handoff-bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/internal")
set(identity_header "${CMAKE_CURRENT_BINARY_DIR}/generated/build_identity.hpp")
add_custom_target(
  handoff_build_identity
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
          "-DOUTPUT_FILE=${identity_header}"
          -P "${PROJECT_SOURCE_DIR}/cmake/HandoffBuildIdentity.cmake"
  BYPRODUCTS "${identity_header}"
  COMMENT "Refreshing benchmark build source identity"
  VERBATIM)
add_dependencies(handoff-bench handoff_build_identity)
target_include_directories(handoff-bench PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"
                                                "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>")
set(build_flags "${CMAKE_CXX_FLAGS}")
foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${config}" upper_config)
  string(APPEND build_flags " $<$<CONFIG:${config}>:${CMAKE_CXX_FLAGS_${upper_config}}>")
endforeach()
string(APPEND build_flags " -std=c++23 $<JOIN:$<TARGET_PROPERTY:handoff-bench,COMPILE_OPTIONS>, >")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/build_config.hpp"
     CONTENT "#pragma once\n#define HANDOFF_BUILD_FLAGS R\"flags(${build_flags})flags\"\n")
target_compile_definitions(
  handoff-bench
  PRIVATE HANDOFF_BUILD_MODE="$<CONFIG>" HANDOFF_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(handoff-bench PRIVATE handoff::core)
handoff_configure_target(handoff-bench)
