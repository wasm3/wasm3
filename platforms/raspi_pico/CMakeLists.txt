cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(wasm3)

pico_sdk_init()

add_executable(main src/main.c)

pico_enable_stdio_usb(main 1)
pico_enable_stdio_uart(main 0)
pico_add_extra_outputs(main)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib/m3 ${CMAKE_BINARY_DIR}/m3)

target_link_libraries(main PRIVATE pico_stdlib m3)

target_compile_options(main PUBLIC -O3)
target_compile_options(m3 PUBLIC -O3)