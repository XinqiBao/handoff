function(handoff_define_project_options)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang)$")
    message(FATAL_ERROR "handoff currently supports Clang only")
  endif()

  if(HANDOFF_ENABLE_ASAN_UBSAN AND HANDOFF_ENABLE_TSAN)
    message(FATAL_ERROR "ASan/UBSan and TSan cannot be enabled together")
  endif()

  add_library(handoff_project_options INTERFACE)
  target_compile_features(handoff_project_options INTERFACE cxx_std_23)

  add_library(handoff_project_warnings INTERFACE)
  target_compile_options(
    handoff_project_warnings
    INTERFACE
      $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wall>
      $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wextra>
      $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wpedantic>
      $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wconversion>
      $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wsign-conversion>
      $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wshadow>
      $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wundef>
      $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wnon-virtual-dtor>
      $<$<AND:$<BOOL:${HANDOFF_WARNINGS_AS_ERRORS}>,$<CXX_COMPILER_ID:AppleClang,Clang>>:-Werror>)

  if(HANDOFF_ENABLE_ASAN_UBSAN)
    target_compile_options(handoff_project_options INTERFACE -fsanitize=address,undefined
                                                              -fno-omit-frame-pointer)
    target_link_options(handoff_project_options INTERFACE -fsanitize=address,undefined)
  elseif(HANDOFF_ENABLE_TSAN)
    target_compile_options(handoff_project_options INTERFACE -fsanitize=thread
                                                              -fno-omit-frame-pointer)
    target_link_options(handoff_project_options INTERFACE -fsanitize=thread)
  endif()

  if(HANDOFF_ENABLE_CLANG_TIDY)
    find_program(HANDOFF_CLANG_TIDY NAMES clang-tidy REQUIRED)
  endif()
endfunction()

function(handoff_configure_target target)
  target_link_libraries(${target} PRIVATE handoff_project_options handoff_project_warnings)

  if(HANDOFF_ENABLE_CLANG_TIDY)
    set_property(TARGET ${target} PROPERTY CXX_CLANG_TIDY "${HANDOFF_CLANG_TIDY}")
  endif()
endfunction()
