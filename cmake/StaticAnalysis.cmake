if(ATLAS_ENABLE_CLANG_TIDY)
    find_program(ATLAS_CLANG_TIDY_EXECUTABLE NAMES clang-tidy)

    if(NOT ATLAS_CLANG_TIDY_EXECUTABLE)
        message(
            WARNING
            "ATLAS_ENABLE_CLANG_TIDY is ON, but clang-tidy was not found. "
            "Atlas targets will build without static analysis."
        )
    else()
        message(STATUS "Atlas Clang-Tidy: ${ATLAS_CLANG_TIDY_EXECUTABLE}")
    endif()
endif()

function(atlas_enable_clang_tidy target)
    if(ATLAS_ENABLE_CLANG_TIDY AND ATLAS_CLANG_TIDY_EXECUTABLE)
        set_property(
            TARGET ${target}
            PROPERTY CXX_CLANG_TIDY
                "${ATLAS_CLANG_TIDY_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy"
        )
    endif()
endfunction()
