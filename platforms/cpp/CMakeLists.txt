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
target_compile_options(m3 PRIVATE -Dd_m3LogOutput=0)
