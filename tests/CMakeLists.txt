function(atlas_add_test_suite target_name suite_label)
    cmake_parse_arguments(PARSE_ARGV 2 suite "" "TIMEOUT" "SOURCES")

    if(NOT suite_SOURCES)
        return()
    endif()

    add_executable(${target_name})
    target_sources(${target_name} PRIVATE ${suite_SOURCES})
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    set_target_properties(
        ${target_name}
        PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
    )

    target_link_libraries(${target_name} PRIVATE Atlas::Atlas Catch2::Catch2WithMain)
    target_include_directories(${target_name} SYSTEM PRIVATE "${catch2_SOURCE_DIR}/src")
    target_compile_definitions(${target_name} PRIVATE CATCH_CONFIG_NO_COUNTER)

    atlas_set_project_warnings(${target_name})
    atlas_enable_sanitizers(${target_name})
    atlas_enable_clang_tidy(${target_name})

    if(NOT suite_TIMEOUT)
        set(suite_TIMEOUT 60)
    endif()

    catch_discover_tests(
        ${target_name}
        ADD_TAGS_AS_LABELS
        PROPERTIES
            LABELS "${suite_label}"
            TIMEOUT ${suite_TIMEOUT}
    )
endfunction()

include(Catch)

add_subdirectory(unit)
add_subdirectory(feature)
add_subdirectory(benchmark)
add_subdirectory(tools)
