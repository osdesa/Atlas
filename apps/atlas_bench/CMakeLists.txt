set(
    ATLAS_BENCHMARK_SUPPORT_SOURCES
    BaselineAnalysis.cpp
    BaselineAnalysis.h
    BaselineConfig.cpp
    BaselineConfig.h
    BaselineRunner.cpp
    BaselineRunner.h
    BaselineWriter.cpp
    BaselineWriter.h
    BenchmarkTypes.cpp
    BenchmarkTypes.h
    BenchmarkMetrics.cpp
    BenchmarkMetrics.h
    BenchmarkRunner.cpp
    BenchmarkRunner.h
    DirectBenchmarkRunner.cpp
    DirectBenchmarkRunner.h
    WorkloadGenerator.cpp
    WorkloadGenerator.h
)

add_library(atlas_benchmark_support STATIC)
target_sources(atlas_benchmark_support PRIVATE ${ATLAS_BENCHMARK_SUPPORT_SOURCES})
target_include_directories(atlas_benchmark_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(atlas_benchmark_support PUBLIC cxx_std_20)
target_link_libraries(atlas_benchmark_support PUBLIC Atlas::Atlas PRIVATE nlohmann_json::nlohmann_json)

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE ATLAS_BENCHMARK_GIT_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT ATLAS_BENCHMARK_GIT_REVISION)
    set(ATLAS_BENCHMARK_GIT_REVISION unknown)
endif()

target_compile_definitions(
    atlas_benchmark_support
    PRIVATE
        ATLAS_BENCHMARK_ATLAS_VERSION="${PROJECT_VERSION}"
        ATLAS_BENCHMARK_GIT_REVISION="${ATLAS_BENCHMARK_GIT_REVISION}"
        ATLAS_BENCHMARK_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
        ATLAS_BENCHMARK_BUILD_TYPE="$<CONFIG>"
        ATLAS_BENCHMARK_CMAKE_VERSION="${CMAKE_VERSION}"
        ATLAS_BENCHMARK_SYSTEM="${CMAKE_SYSTEM_NAME}"
        ATLAS_BENCHMARK_SYSTEM_PROCESSOR="${CMAKE_SYSTEM_PROCESSOR}"
        ATLAS_BENCHMARK_SPIRV_PATH="${ATLAS_BENCHMARK_SPIRV}"
)

add_dependencies(atlas_benchmark_support atlas_benchmark_shader)

atlas_set_project_warnings(atlas_benchmark_support)
atlas_enable_sanitizers(atlas_benchmark_support)
atlas_enable_clang_tidy(atlas_benchmark_support)

add_executable(atlas_bench main.cpp)
target_compile_features(atlas_bench PRIVATE cxx_std_20)
target_link_libraries(atlas_bench PRIVATE atlas_benchmark_support)
atlas_set_project_warnings(atlas_bench)
atlas_enable_sanitizers(atlas_bench)
atlas_enable_clang_tidy(atlas_bench)
