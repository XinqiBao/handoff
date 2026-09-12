find_package(Threads REQUIRED)

add_library(handoff_core platform.cpp version.cpp)
add_library(handoff::core ALIAS handoff_core)

target_include_directories(
  handoff_core
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)

handoff_configure_target(handoff_core)
target_link_libraries(handoff_core PRIVATE Threads::Threads)
