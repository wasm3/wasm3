cmake_minimum_required(VERSION 3.4.1)

add_definitions(-DANDROID -Wno-format-security -O3
                #-fno-optimize-sibling-calls
                -flto -fomit-frame-pointer -fno-stack-check -fno-stack-protector)

file(GLOB M3_SRC FOLLOW_SYMLINKS "m3/*.c" "*.c")

add_library(wasm3-jni SHARED ${M3_SRC})

set_target_properties(wasm3-jni PROPERTIES LINK_FLAGS "-flto -O3")

# Include libraries needed for wasm3-jni lib
target_link_libraries(wasm3-jni
                      android
                      log)
