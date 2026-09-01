find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

add_test(
    NAME atlas_evaluation_tests
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}" -p "test_*.py"
)
set_tests_properties(atlas_evaluation_tests PROPERTIES LABELS "benchmark" TIMEOUT 60)
