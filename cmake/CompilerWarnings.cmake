function(atlas_set_project_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
                -Wshadow
        )

        if(ATLAS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    else()
        message(STATUS "Atlas has no curated warning set for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()
