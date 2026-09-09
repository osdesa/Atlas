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
    atlas_enable_coverage(${target_name})

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

add_library(atlas_task_pack_mock SHARED support/TaskPackMock.cpp)
target_include_directories(atlas_task_pack_mock PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_features(atlas_task_pack_mock PRIVATE cxx_std_20)
set_target_properties(atlas_task_pack_mock PROPERTIES CXX_EXTENSIONS OFF)
atlas_set_project_warnings(atlas_task_pack_mock)
atlas_enable_sanitizers(atlas_task_pack_mock)
atlas_enable_clang_tidy(atlas_task_pack_mock)
atlas_enable_coverage(atlas_task_pack_mock)

include(Catch)

add_executable(atlas_task_pack_contract support/TaskPackContract.cpp)
target_link_libraries(atlas_task_pack_contract PRIVATE Atlas::Atlas)
target_compile_features(atlas_task_pack_contract PRIVATE cxx_std_20)
target_compile_definitions(atlas_task_pack_contract PRIVATE
    ATLAS_TASK_PACK_MOCK_PATH="$<TARGET_FILE:atlas_task_pack_mock>"
    ATLAS_TASK_PACK_TEST_SPIRV_PATH="${ATLAS_VECTOR_ADD_SPIRV}")
add_dependencies(atlas_task_pack_contract atlas_task_pack_mock atlas_vector_add_shader)
atlas_set_project_warnings(atlas_task_pack_contract)
atlas_enable_sanitizers(atlas_task_pack_contract)

add_subdirectory(unit)
add_subdirectory(feature)
add_subdirectory(benchmark)
add_subdirectory(tools)
