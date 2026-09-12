add_executable(handoff-bench main.cpp)
target_link_libraries(handoff-bench PRIVATE handoff::core)
handoff_configure_target(handoff-bench)
