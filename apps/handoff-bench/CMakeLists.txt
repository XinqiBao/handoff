add_executable(
  handoff-bench
  command.cpp
  main.cpp
  output.cpp
  ping_pong.cpp
  throughput.cpp
  workload.cpp)
target_link_libraries(handoff-bench PRIVATE handoff::core)
handoff_configure_target(handoff-bench)
