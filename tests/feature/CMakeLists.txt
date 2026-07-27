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
