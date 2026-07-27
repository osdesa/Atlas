add_executable(atlas_cli)

target_sources(atlas_cli PRIVATE main.cpp)
target_compile_features(atlas_cli PRIVATE cxx_std_20)
set_target_properties(
    atlas_cli
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

target_link_libraries(atlas_cli PRIVATE Atlas::Atlas)

atlas_set_project_warnings(atlas_cli)
atlas_enable_sanitizers(atlas_cli)
atlas_enable_clang_tidy(atlas_cli)
