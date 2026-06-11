cmake_minimum_required(VERSION 3.9)
project(wasm3_cpp_example)

set(target ${CMAKE_PROJECT_NAME})

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../source ${CMAKE_BINARY_DIR}/m3)

add_executable(${target} main.cpp)
target_link_libraries(${target} PRIVATE m3)

add_subdirectory(wasm3_cpp)
target_link_libraries(${target} PRIVATE wasm3_cpp)

target_compile_options(${target} PUBLIC -g)
target_compile_options(m3 PUBLIC -g)

# Copy the 'wasm' directory into the build directory, so that
# wasm/test_prog.wasm is found even if wasm3_cpp_example is executed
# from the build directory.
add_custom_target(copy_wasm ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_LIST_DIR}/wasm
    ${CMAKE_BINARY_DIR}/wasm
)
add_dependencies(${CMAKE_PROJECT_NAME} copy_wasm)
