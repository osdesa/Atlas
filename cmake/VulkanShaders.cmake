function(atlas_compile_shader target_name shader_source output_variable)
    find_program(ATLAS_GLSLC_EXECUTABLE NAMES glslc REQUIRED)
    find_program(ATLAS_SPIRV_VAL_EXECUTABLE NAMES spirv-val REQUIRED)

    get_filename_component(shader_name "${shader_source}" NAME)
    set(shader_output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader_name}.spv")

    add_custom_command(
        OUTPUT "${shader_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND "${ATLAS_GLSLC_EXECUTABLE}" --target-env=vulkan1.0 "${shader_source}" -o "${shader_output}"
        COMMAND "${ATLAS_SPIRV_VAL_EXECUTABLE}" --target-env vulkan1.0 "${shader_output}"
        DEPENDS "${shader_source}"
        COMMENT "Compiling and validating ${shader_name}"
        VERBATIM
    )

    add_custom_target(${target_name} DEPENDS "${shader_output}")
    set(${output_variable} "${shader_output}" PARENT_SCOPE)
endfunction()
