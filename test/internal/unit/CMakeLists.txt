cmake_minimum_required(VERSION 3.9)
project(wasm3_test_unit)

Include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.0.0-preview4
)
FetchContent_MakeAvailable(Catch2)

set(target ${CMAKE_PROJECT_NAME})

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../source ${CMAKE_BINARY_DIR}/m3)

file(GLOB src_tests "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
file(GLOB src_ext "${CMAKE_CURRENT_LIST_DIR}/../../../source/extensions/*.c")

add_executable(${target} ${src_ext} ${src_tests})
target_link_libraries(${target} PRIVATE m3 m Catch2::Catch2WithMain)

target_compile_options(${target} PUBLIC -g)
target_compile_options(m3 PUBLIC -g)
