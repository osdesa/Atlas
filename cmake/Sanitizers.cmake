if(ATLAS_ENABLE_SANITIZERS AND ATLAS_ENABLE_THREAD_SANITIZER)
    message(
        FATAL_ERROR
        "ATLAS_ENABLE_SANITIZERS and ATLAS_ENABLE_THREAD_SANITIZER cannot be enabled together."
    )
endif()

function(atlas_enable_sanitizers target)
    if(NOT ATLAS_ENABLE_SANITIZERS AND NOT ATLAS_ENABLE_THREAD_SANITIZER)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR MSVC OR WIN32)
        message(
            FATAL_ERROR
            "The requested Atlas sanitizer configuration is unsupported for "
            "${CMAKE_CXX_COMPILER_ID} on this platform."
        )
    endif()

    if(ATLAS_ENABLE_THREAD_SANITIZER)
        target_compile_options(
            ${target}
            PRIVATE
                -fsanitize=thread
                -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE -fsanitize=thread)
    else()
        target_compile_options(
            ${target}
            PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()
