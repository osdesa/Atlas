file(
    GLOB_RECURSE ATLAS_UNIT_TEST_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

list(SORT ATLAS_UNIT_TEST_SOURCES)

atlas_add_test_suite(
    atlas_unit_tests
    unit
    SOURCES ${ATLAS_UNIT_TEST_SOURCES}
)
