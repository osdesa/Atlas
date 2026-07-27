function(atlas_enable_sanitizers target)
    if(NOT ATLAS_ENABLE_SANITIZERS)
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC AND NOT WIN32)
        target_compile_options(
            ${target}
            PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    else()
        get_property(_atlas_sanitizer_warning_issued GLOBAL PROPERTY ATLAS_SANITIZER_WARNING_ISSUED)
        if(NOT _atlas_sanitizer_warning_issued)
            message(
                WARNING
                "ATLAS_ENABLE_SANITIZERS is ON, but AddressSanitizer and "
                "UndefinedBehaviorSanitizer are not configured for "
                "${CMAKE_CXX_COMPILER_ID} with this toolchain."
            )
            set_property(GLOBAL PROPERTY ATLAS_SANITIZER_WARNING_ISSUED TRUE)
        endif()
    endif()
endfunction()
