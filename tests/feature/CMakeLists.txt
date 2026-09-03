file(
    GLOB_RECURSE ATLAS_FEATURE_TEST_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

list(SORT ATLAS_FEATURE_TEST_SOURCES)

atlas_add_test_suite(
    atlas_feature_tests
    feature
    SOURCES ${ATLAS_FEATURE_TEST_SOURCES}
)

add_dependencies(atlas_feature_tests atlas_task_pack_mock)
target_compile_definitions(
    atlas_feature_tests
    PRIVATE
        ATLAS_TASK_PACK_MOCK_PATH="$<TARGET_FILE:atlas_task_pack_mock>"
        ATLAS_TASK_PACK_TEST_SPIRV_PATH="${ATLAS_VECTOR_ADD_SPIRV}"
)
