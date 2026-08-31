file(
    GLOB_RECURSE ATLAS_BENCHMARK_TEST_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(SORT ATLAS_BENCHMARK_TEST_SOURCES)

atlas_add_test_suite(
    atlas_benchmark_tests
    benchmark
    SOURCES ${ATLAS_BENCHMARK_TEST_SOURCES}
)
target_link_libraries(atlas_benchmark_tests PRIVATE atlas_benchmark_support nlohmann_json::nlohmann_json)
target_compile_definitions(
    atlas_benchmark_tests
    PRIVATE ATLAS_CPU_BENCHMARK_MANIFEST_PATH="${PROJECT_SOURCE_DIR}/benchmarks/manifests/cpu-smoke-v1.json"
)
