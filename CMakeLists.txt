cmake_minimum_required(VERSION 3.24)

project(
    Atlas
    VERSION 0.1.0
    DESCRIPTION "Scaffolding for a heterogeneous CPU/GPU task scheduler"
    LANGUAGES CXX
)

option(ATLAS_BUILD_TESTS "Build Atlas tests" ON)
option(ATLAS_WARNINGS_AS_ERRORS "Treat compiler warnings in Atlas targets as errors" OFF)
option(ATLAS_ENABLE_SANITIZERS "Enable supported sanitizers for Atlas targets" OFF)
option(ATLAS_ENABLE_CLANG_TIDY "Enable Clang-Tidy for Atlas targets when available" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(Sanitizers)
include(StaticAnalysis)

find_package(Vulkan REQUIRED)

add_library(atlas STATIC)
add_library(Atlas::Atlas ALIAS atlas)

target_sources(
    atlas
    PRIVATE
        src/atlas.cpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS include
        FILES include/atlas/atlas.hpp
)

target_include_directories(
    atlas
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_compile_features(atlas PUBLIC cxx_std_20)
set_target_properties(
    atlas
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

target_link_libraries(atlas PRIVATE Vulkan::Vulkan)

atlas_set_project_warnings(atlas)
atlas_enable_sanitizers(atlas)
atlas_enable_clang_tidy(atlas)

add_subdirectory(apps/atlas_cli)

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
endif()
