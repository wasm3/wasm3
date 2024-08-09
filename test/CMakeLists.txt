find_package(Python3 COMPONENTS Interpreter)

add_test(NAME spec
  COMMAND ${Python3_EXECUTABLE} run-spec-test.py --exec "${CMAKE_BINARY_DIR}/wasm3 --repl"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(spec PROPERTIES REQUIRED_FILES ${CMAKE_BINARY_DIR}/wasm3)

add_test(NAME wasi
  COMMAND ${Python3_EXECUTABLE} run-wasi-test.py --exec ${CMAKE_BINARY_DIR}/wasm3
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(wasi PROPERTIES REQUIRED_FILES ${CMAKE_BINARY_DIR}/wasm3)
