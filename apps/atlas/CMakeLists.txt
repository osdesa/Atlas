file(
    GLOB ATLAS_APP_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
list(SORT ATLAS_APP_SOURCES)

add_executable(atlas ${ATLAS_APP_SOURCES})
target_link_libraries(atlas PRIVATE Atlas::Atlas)
target_compile_features(atlas PRIVATE cxx_std_20)
target_compile_definitions(atlas PRIVATE ATLAS_VECTOR_ADD_SPIRV_PATH="${ATLAS_VECTOR_ADD_SPIRV}")
add_dependencies(atlas atlas_vector_add_shader)
atlas_set_project_warnings(atlas)
atlas_enable_sanitizers(atlas)
atlas_enable_clang_tidy(atlas)
atlas_enable_coverage(atlas)
