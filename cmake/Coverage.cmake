function(atlas_enable_coverage target)
    if(NOT ATLAS_ENABLE_COVERAGE)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR WIN32)
        message(FATAL_ERROR "Atlas coverage instrumentation requires GCC or Clang on a non-Windows platform.")
    endif()

    target_compile_options(${target} PRIVATE --coverage -O0 -g)
    target_link_options(${target} PRIVATE --coverage)
endfunction()
