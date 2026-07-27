function(atlas_set_project_warnings target)
    if(MSVC)
        target_compile_options(
            ${target}
            PRIVATE
                /Wall
                /permissive-
                # Public headers intentionally contain inline API functions that are not used by
                # every translation unit.
                /wd4514
                # Natural class alignment can introduce tail padding without indicating a defect.
                /wd4820
                # MSVC reports its own incomplete braced-initializer evaluation-order support,
                # including through Catch2 macro expansions.
                /wd4868
        )

        if(ATLAS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
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
