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
