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
    PRIVATE
        ATLAS_BASELINE_CANONICAL_SUITE_PATH="${PROJECT_SOURCE_DIR}/benchmarks/manifests/baseline-canonical-v1.json"
        ATLAS_BASELINE_CPU_SUITE_PATH="${PROJECT_SOURCE_DIR}/benchmarks/manifests/smoke-v1.json"
        ATLAS_MILESTONE_13_INTEL_ENVIRONMENT_PATH="${PROJECT_SOURCE_DIR}/benchmarks/environments/milestone-13-intel-xe.json"
        ATLAS_MILESTONE_13_LAVAPIPE_ENVIRONMENT_PATH="${PROJECT_SOURCE_DIR}/benchmarks/environments/milestone-13-lavapipe.json"
        ATLAS_BENCHMARK_SCHEMA_DIRECTORY="${PROJECT_SOURCE_DIR}/benchmarks/schema"
)
