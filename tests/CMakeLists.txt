add_executable(atlas_tests)

target_sources(atlas_tests PRIVATE smoke_tests.cpp)
target_compile_features(atlas_tests PRIVATE cxx_std_20)
set_target_properties(
    atlas_tests
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

target_link_libraries(atlas_tests PRIVATE Atlas::Atlas Catch2::Catch2WithMain)
target_include_directories(atlas_tests SYSTEM PRIVATE "${catch2_SOURCE_DIR}/src")
target_compile_definitions(atlas_tests PRIVATE CATCH_CONFIG_NO_COUNTER)

atlas_set_project_warnings(atlas_tests)
atlas_enable_sanitizers(atlas_tests)
atlas_enable_clang_tidy(atlas_tests)

include(Catch)
catch_discover_tests(atlas_tests)
