if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "ATLAS_BUILD_FUZZERS requires Clang libFuzzer support.")
endif()

file(
    GLOB ATLAS_FUZZ_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(SORT ATLAS_FUZZ_SOURCES)

foreach(fuzz_source IN LISTS ATLAS_FUZZ_SOURCES)
    get_filename_component(fuzz_name "${fuzz_source}" NAME_WE)
    add_executable(${fuzz_name} "${fuzz_source}")
    target_link_libraries(${fuzz_name} PRIVATE Atlas::Atlas)
    target_compile_features(${fuzz_name} PRIVATE cxx_std_20)
    target_compile_options(${fuzz_name} PRIVATE -fsanitize=fuzzer)
    target_link_options(${fuzz_name} PRIVATE -fsanitize=fuzzer)
    atlas_set_project_warnings(${fuzz_name})
    atlas_enable_sanitizers(${fuzz_name})
endforeach()
