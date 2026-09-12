add_executable(
  handoff-bench
  main.cpp
  src/command.cpp
  src/output.cpp
  src/ping_pong.cpp
  src/throughput.cpp)
target_include_directories(handoff-bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/internal")
target_link_libraries(handoff-bench PRIVATE handoff::core)
handoff_configure_target(handoff-bench)
