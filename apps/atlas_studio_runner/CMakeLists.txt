file(GLOB ATLAS_STUDIO_RUNNER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(SORT ATLAS_STUDIO_RUNNER_SOURCES)

add_executable(atlas_studio_runner ${ATLAS_STUDIO_RUNNER_SOURCES})
target_compile_features(atlas_studio_runner PRIVATE cxx_std_20)
target_link_libraries(atlas_studio_runner PRIVATE Atlas::Atlas nlohmann_json::nlohmann_json)
target_compile_definitions(
    atlas_studio_runner
    PRIVATE
        ATLAS_STUDIO_VECTOR_ADD_SPIRV_PATH="${ATLAS_VECTOR_ADD_SPIRV}"
        ATLAS_STUDIO_BENCHMARK_SPIRV_PATH="${ATLAS_BENCHMARK_SPIRV}"
)
add_dependencies(atlas_studio_runner atlas_vector_add_shader atlas_benchmark_shader)
atlas_set_project_warnings(atlas_studio_runner)
atlas_enable_sanitizers(atlas_studio_runner)
atlas_enable_clang_tidy(atlas_studio_runner)
atlas_enable_coverage(atlas_studio_runner)
