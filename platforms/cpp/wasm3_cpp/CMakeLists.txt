cmake_minimum_required(VERSION 3.5)
add_library(wasm3_cpp INTERFACE)
target_include_directories(wasm3_cpp INTERFACE include)
target_compile_features(wasm3_cpp INTERFACE cxx_std_17)
