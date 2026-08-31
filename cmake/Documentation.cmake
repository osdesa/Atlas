function(atlas_add_documentation)
    if(NOT ATLAS_BUILD_DOCUMENTATION)
        return()
    endif()

    find_package(Doxygen REQUIRED)
    find_package(Java REQUIRED COMPONENTS Runtime)
    find_program(
        ATLAS_CLANG_UML_EXECUTABLE
        NAMES clang-uml
        HINTS "C:/Program Files/clang-uml/bin"
        REQUIRED
    )
    find_program(ATLAS_NINJA_EXECUTABLE NAMES ninja REQUIRED)

    include(FetchContent)
    FetchContent_Declare(
        doxygen_awesome_css
        GIT_REPOSITORY https://github.com/jothepro/doxygen-awesome-css.git
        GIT_TAG d52eafe3e9303399fda15661f3d7bb8fe3d7eabc
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(doxygen_awesome_css)

    set(ATLAS_PLANTUML_JAR "${CMAKE_CURRENT_BINARY_DIR}/tools/plantuml-1.2026.3.jar")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tools")
    file(
        DOWNLOAD
        "https://github.com/plantuml/plantuml/releases/download/v1.2026.3/plantuml-1.2026.3.jar"
        "${ATLAS_PLANTUML_JAR}"
        EXPECTED_HASH SHA256=53af6760d96bb2737e5e4386e832b46339fc29dec74f412d7c12db7c30db8ec4
        TLS_VERIFY ON
        STATUS atlas_plantuml_download_status
    )
    list(GET atlas_plantuml_download_status 0 atlas_plantuml_download_code)
    if(NOT atlas_plantuml_download_code EQUAL 0)
        list(GET atlas_plantuml_download_status 1 atlas_plantuml_download_message)
        message(FATAL_ERROR "Unable to download PlantUML: ${atlas_plantuml_download_message}")
    endif()

    set(ATLAS_DOXYGEN_PROJECT_NAME "${PROJECT_NAME}")
    set(ATLAS_DOXYGEN_PROJECT_VERSION "${PROJECT_VERSION}")
    set(ATLAS_DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs")
    set(ATLAS_DOXYGEN_INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include")
    set(ATLAS_DOXYGEN_SOURCE_DIRECTORY "${PROJECT_SOURCE_DIR}/src")
    set(ATLAS_DOXYGEN_INDEX_PAGE "${PROJECT_SOURCE_DIR}/docs/index.md")
    set(ATLAS_DOXYGEN_USER_GUIDE_PAGE "${PROJECT_SOURCE_DIR}/docs/user-guide.md")
    set(ATLAS_DOXYGEN_DEVELOPMENT_PAGE "${PROJECT_SOURCE_DIR}/docs/development.md")
    set(ATLAS_DOXYGEN_LIFECYCLE_PAGE "${PROJECT_SOURCE_DIR}/docs/task-lifecycle.md")
    set(ATLAS_DOXYGEN_AWESOME_DIRECTORY "${doxygen_awesome_css_SOURCE_DIR}")
    set(ATLAS_UML_ANALYSIS_BUILD_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/uml-analysis")
    set(ATLAS_UML_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/uml")
    set(ATLAS_CLANG_UML_CONFIGURATION "${CMAKE_CURRENT_BINARY_DIR}/clang-uml.yml")
    set(ATLAS_DOXYGEN_PLANTUML_DIRECTORY "${ATLAS_UML_OUTPUT_DIRECTORY}")

    file(TO_CMAKE_PATH "${ATLAS_DOXYGEN_OUTPUT_DIRECTORY}" ATLAS_DOXYGEN_OUTPUT_DIRECTORY)
    file(TO_CMAKE_PATH "${ATLAS_DOXYGEN_INCLUDE_DIRECTORY}" ATLAS_DOXYGEN_INCLUDE_DIRECTORY)
    file(TO_CMAKE_PATH "${ATLAS_DOXYGEN_SOURCE_DIRECTORY}" ATLAS_DOXYGEN_SOURCE_DIRECTORY)
    file(TO_CMAKE_PATH "${ATLAS_DOXYGEN_INDEX_PAGE}" ATLAS_DOXYGEN_INDEX_PAGE)
    file(TO_CMAKE_PATH "${ATLAS_DOXYGEN_USER_GUIDE_PAGE}" ATLAS_DOXYGEN_USER_GUIDE_PAGE)
    file(TO_CMAKE_PATH "${ATLAS_DOXYGEN_DEVELOPMENT_PAGE}" ATLAS_DOXYGEN_DEVELOPMENT_PAGE)
    file(TO_CMAKE_PATH "${ATLAS_DOXYGEN_LIFECYCLE_PAGE}" ATLAS_DOXYGEN_LIFECYCLE_PAGE)
    file(TO_CMAKE_PATH "${ATLAS_DOXYGEN_AWESOME_DIRECTORY}" ATLAS_DOXYGEN_AWESOME_DIRECTORY)
    file(TO_CMAKE_PATH "${ATLAS_UML_ANALYSIS_BUILD_DIRECTORY}" ATLAS_UML_ANALYSIS_BUILD_DIRECTORY)
    file(TO_CMAKE_PATH "${ATLAS_UML_OUTPUT_DIRECTORY}" ATLAS_UML_OUTPUT_DIRECTORY)
    file(TO_CMAKE_PATH "${ATLAS_DOXYGEN_PLANTUML_DIRECTORY}" ATLAS_DOXYGEN_PLANTUML_DIRECTORY)
    file(TO_CMAKE_PATH "${ATLAS_PLANTUML_JAR}" ATLAS_PLANTUML_JAR)

    if(DOXYGEN_DOT_FOUND)
        set(ATLAS_DOXYGEN_HAVE_DOT YES)
        get_filename_component(
            ATLAS_DOXYGEN_DOT_DIRECTORY
            "${DOXYGEN_DOT_EXECUTABLE}"
            DIRECTORY
        )
        file(TO_CMAKE_PATH "${ATLAS_DOXYGEN_DOT_DIRECTORY}" ATLAS_DOXYGEN_DOT_DIRECTORY)
    else()
        set(ATLAS_DOXYGEN_HAVE_DOT NO)
        set(ATLAS_DOXYGEN_DOT_DIRECTORY "")
        message(WARNING "Graphviz dot was not found; Atlas documentation will omit diagrams.")
    endif()

    configure_file(
        "${PROJECT_SOURCE_DIR}/docs/clang-uml.yml.in"
        "${ATLAS_CLANG_UML_CONFIGURATION}"
        @ONLY
    )

    file(
        GLOB_RECURSE ATLAS_DOCUMENTATION_INPUTS
        CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/include/atlas/*.h"
        "${PROJECT_SOURCE_DIR}/include/atlas/*.hpp"
        "${PROJECT_SOURCE_DIR}/src/*.cpp"
    )

    set(
        ATLAS_UML_DIAGRAMS
        "${ATLAS_UML_OUTPUT_DIRECTORY}/task.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/task_handle.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/task_id_generator.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/task_options.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/task_graph.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/cpu_executor.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/vulkan_dispatch_executor.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/completion_channel.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/task_completion.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/synchronous_cpu_executor.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/workerpool_executor.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/vulkan_compute.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/vulkan_runtime.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/vulkan_executor.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/base_scheduler.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/kahn_scheduler.puml"
        "${ATLAS_UML_OUTPUT_DIRECTORY}/scheduling_policy.puml"
    )

    add_custom_command(
        OUTPUT ${ATLAS_UML_DIAGRAMS}
        COMMAND
            "${CMAKE_COMMAND}"
            -S "${PROJECT_SOURCE_DIR}"
            -B "${ATLAS_UML_ANALYSIS_BUILD_DIRECTORY}"
            -G Ninja
            "-DCMAKE_MAKE_PROGRAM:FILEPATH=${ATLAS_NINJA_EXECUTABLE}"
            "-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}"
            -DCMAKE_BUILD_TYPE:STRING=Debug
            -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON
            -DATLAS_BUILD_TESTS:BOOL=OFF
            -DATLAS_BUILD_DOCUMENTATION:BOOL=OFF
            -DATLAS_ENABLE_CLANG_TIDY:BOOL=OFF
            -DATLAS_ENABLE_SANITIZERS:BOOL=OFF
            -DATLAS_WARNINGS_AS_ERRORS:BOOL=OFF
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${ATLAS_UML_OUTPUT_DIRECTORY}"
        COMMAND
            "${ATLAS_CLANG_UML_EXECUTABLE}"
            --config "${ATLAS_CLANG_UML_CONFIGURATION}"
            --paths-relative-to-pwd
            --quiet
        DEPENDS
            "${ATLAS_CLANG_UML_CONFIGURATION}"
            "${PROJECT_SOURCE_DIR}/CMakeLists.txt"
            ${ATLAS_DOCUMENTATION_INPUTS}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Generating Atlas UML class diagrams"
        VERBATIM
    )

    add_custom_target(atlas_uml_diagrams DEPENDS ${ATLAS_UML_DIAGRAMS})
    set_target_properties(atlas_uml_diagrams PROPERTIES FOLDER "Documentation")

    configure_file(
        "${PROJECT_SOURCE_DIR}/docs/Doxyfile.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        @ONLY
    )

    add_custom_target(
        atlas_docs
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${ATLAS_DOXYGEN_OUTPUT_DIRECTORY}"
        COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        BYPRODUCTS "${ATLAS_DOXYGEN_OUTPUT_DIRECTORY}/html/index.html"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Generating Atlas API documentation"
        VERBATIM
    )
    add_dependencies(atlas_docs atlas_uml_diagrams)
    set_target_properties(atlas_docs PROPERTIES FOLDER "Documentation")

    add_custom_target(docs DEPENDS atlas_docs)
    set_target_properties(docs PROPERTIES FOLDER "Documentation")
endfunction()
