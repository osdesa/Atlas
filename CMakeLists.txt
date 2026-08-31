cmake_minimum_required(VERSION 3.24)

project(
    Atlas
    VERSION 0.2.0
    DESCRIPTION "A heterogeneous CPU/GPU task scheduler"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ standard used to build Atlas (20 or newer)")
if(CMAKE_CXX_STANDARD LESS 20)
    message(FATAL_ERROR "Atlas requires C++20 or newer; std::span is not available before C++20")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ATLAS_BUILD_TESTS "Build Atlas tests" ON)
option(ATLAS_BUILD_DOCUMENTATION "Build Atlas API documentation with Doxygen" OFF)
option(ATLAS_BUILD_VULKAN_INTEGRATION_TESTS "Build tests that execute Vulkan compute work" OFF)
option(ATLAS_BUILD_BENCHMARKS "Build the reproducible Atlas benchmark runner" OFF)
option(ATLAS_BENCHMARK_ENABLE_VULKAN "Enable real Vulkan workloads in atlas_bench" OFF)
option(ATLAS_WARNINGS_AS_ERRORS "Treat compiler warnings in Atlas targets as errors on non-MSVC toolchains" OFF)
option(ATLAS_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer for Atlas targets" OFF)
option(ATLAS_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer for Atlas targets" OFF)
option(ATLAS_ENABLE_CLANG_TIDY "Enable Clang-Tidy for Atlas targets when available" OFF)
option(ATLAS_REQUIRE_CLANG_TIDY "Fail configuration when Clang-Tidy is enabled but unavailable" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(Documentation)
include(Sanitizers)
include(StaticAnalysis)

find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)

if(ATLAS_BENCHMARK_ENABLE_VULKAN AND NOT ATLAS_BUILD_BENCHMARKS)
    message(FATAL_ERROR "ATLAS_BENCHMARK_ENABLE_VULKAN requires ATLAS_BUILD_BENCHMARKS")
endif()

if(ATLAS_BUILD_BENCHMARKS)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)

    if(ATLAS_BENCHMARK_ENABLE_VULKAN)
        include(VulkanShaders)
        atlas_compile_shader(
            atlas_benchmark_shader
            "${CMAKE_CURRENT_SOURCE_DIR}/apps/atlas_bench/shaders/benchmark.comp"
            ATLAS_BENCHMARK_SPIRV
        )
    endif()
endif()

file(
    GLOB_RECURSE ATLAS_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(
    GLOB_RECURSE ATLAS_PUBLIC_HEADERS
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/atlas/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/atlas/*.hpp"
)

list(SORT ATLAS_SOURCES)
list(SORT ATLAS_PUBLIC_HEADERS)

add_library(atlas STATIC)
add_library(Atlas::Atlas ALIAS atlas)

target_sources(
    atlas
    PRIVATE
        ${ATLAS_SOURCES}
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
        FILES ${ATLAS_PUBLIC_HEADERS}
)

target_include_directories(
    atlas
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_compile_features(atlas PUBLIC cxx_std_20)

target_link_libraries(atlas PUBLIC Vulkan::Vulkan PRIVATE Threads::Threads)

atlas_set_project_warnings(atlas)
atlas_enable_sanitizers(atlas)
atlas_enable_clang_tidy(atlas)

atlas_add_documentation()

if(ATLAS_BUILD_VULKAN_INTEGRATION_TESTS)
    include(VulkanShaders)
    atlas_compile_shader(
        atlas_vector_add_shader
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/vector_add.comp"
        ATLAS_VECTOR_ADD_SPIRV
    )
endif()

add_subdirectory(apps/atlas_cli)
if(ATLAS_BUILD_BENCHMARKS)
    add_subdirectory(apps/atlas_bench)
endif()
if(ATLAS_BUILD_VULKAN_INTEGRATION_TESTS)
    add_subdirectory(apps/atlas_vulkan_example)
    add_subdirectory(apps/atlas_mixed_example)
    add_subdirectory(apps/atlas_all_example)
endif()

if(ATLAS_BUILD_TESTS)
    include(FetchContent)

    FetchContent_Declare(
        Catch2
        URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.8.1.tar.gz
        URL_HASH SHA256=18b3f70ac80fccc340d8c6ff0f339b2ae64944782f8d2fca2bd705cf47cadb79
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")

    enable_testing()
    add_subdirectory(tests)

    if(ATLAS_BUILD_VULKAN_INTEGRATION_TESTS)
        add_dependencies(atlas_feature_tests atlas_vector_add_shader)
        target_compile_definitions(
            atlas_feature_tests
            PRIVATE ATLAS_VECTOR_ADD_SPIRV_PATH="${ATLAS_VECTOR_ADD_SPIRV}"
        )
    endif()
endif()
