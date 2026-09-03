file(
    GLOB_RECURSE ATLAS_UNIT_TEST_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

list(SORT ATLAS_UNIT_TEST_SOURCES)

set(ATLAS_STRESS_TEST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/scheduler/KahnSchedulerStress_tests.cpp")
list(REMOVE_ITEM ATLAS_UNIT_TEST_SOURCES "${ATLAS_STRESS_TEST_SOURCE}")

atlas_add_test_suite(
    atlas_unit_tests
    unit
    SOURCES ${ATLAS_UNIT_TEST_SOURCES}
)

atlas_add_test_suite(
    atlas_stress_tests
    stress
    TIMEOUT 1800
    SOURCES ${ATLAS_STRESS_TEST_SOURCE}
)

add_dependencies(atlas_unit_tests atlas_task_pack_mock atlas_vector_add_shader)
target_compile_definitions(
    atlas_unit_tests
    PRIVATE
        ATLAS_TASK_PACK_MOCK_PATH="$<TARGET_FILE:atlas_task_pack_mock>"
        ATLAS_TASK_PACK_TEST_SPIRV_PATH="${ATLAS_VECTOR_ADD_SPIRV}"
)
