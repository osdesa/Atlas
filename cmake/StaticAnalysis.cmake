if(ATLAS_ENABLE_CLANG_TIDY)
    set(_atlas_clang_tidy_hints)

    if(CMAKE_CXX_COMPILER)
        get_filename_component(_atlas_compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
        list(APPEND _atlas_clang_tidy_hints "${_atlas_compiler_directory}")
    endif()

    if(WIN32)
        if(CMAKE_GENERATOR_INSTANCE)
            list(
                APPEND
                _atlas_clang_tidy_hints
                "${CMAKE_GENERATOR_INSTANCE}/VC/Tools/Llvm/x64/bin"
                "${CMAKE_GENERATOR_INSTANCE}/VC/Tools/Llvm/bin"
            )
        endif()

        file(
            GLOB
            _atlas_visual_studio_llvm_directories
            LIST_DIRECTORIES TRUE
            "$ENV{ProgramFiles}/Microsoft Visual Studio/*/*/VC/Tools/Llvm/x64/bin"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/*/*/VC/Tools/Llvm/bin"
        )
        list(APPEND _atlas_clang_tidy_hints ${_atlas_visual_studio_llvm_directories})
    endif()

    find_program(
        ATLAS_CLANG_TIDY_EXECUTABLE
        NAMES clang-tidy
        HINTS ${_atlas_clang_tidy_hints}
    )

    if(NOT ATLAS_CLANG_TIDY_EXECUTABLE)
        message(
            WARNING
            "ATLAS_ENABLE_CLANG_TIDY is ON, but clang-tidy was not found. "
            "Atlas targets will build without static analysis."
        )
    else()
        message(STATUS "Atlas Clang-Tidy: ${ATLAS_CLANG_TIDY_EXECUTABLE}")
    endif()

    unset(_atlas_clang_tidy_hints)
    unset(_atlas_compiler_directory)
    unset(_atlas_visual_studio_llvm_directories)
endif()

function(atlas_enable_clang_tidy target)
    if(ATLAS_ENABLE_CLANG_TIDY AND ATLAS_CLANG_TIDY_EXECUTABLE)
        if(CMAKE_GENERATOR MATCHES "^Visual Studio")
            get_filename_component(
                _atlas_clang_tidy_directory
                "${ATLAS_CLANG_TIDY_EXECUTABLE}"
                DIRECTORY
            )
            get_filename_component(
                _atlas_clang_tidy_filename
                "${ATLAS_CLANG_TIDY_EXECUTABLE}"
                NAME
            )

            set_target_properties(
                ${target}
                PROPERTIES
                    VS_GLOBAL_RunCodeAnalysis TRUE
                    VS_GLOBAL_EnableMicrosoftCodeAnalysis FALSE
                    VS_GLOBAL_EnableClangTidyCodeAnalysis TRUE
                    VS_GLOBAL_ClangTidyToolPath "${_atlas_clang_tidy_directory}"
                    VS_GLOBAL_ClangTidyToolExe "${_atlas_clang_tidy_filename}"
                    VS_GLOBAL_ClangTidyToolExeAdditionalOptions
                        "--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy --fix=false"
            )

            unset(_atlas_clang_tidy_directory)
            unset(_atlas_clang_tidy_filename)
        else()
            set_property(
                TARGET ${target}
                PROPERTY CXX_CLANG_TIDY
                    "${ATLAS_CLANG_TIDY_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy"
            )
        endif()
    endif()
endfunction()
