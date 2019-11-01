cmake_minimum_required(VERSION 3.9)

set(OUT_FILE "wasm3")
set(APP_DIR  "platforms/app")

if(CLANG OR CLANG_SUFFIX)
  set(CMAKE_C_COMPILER   "clang${CLANG_SUFFIX}")
  set(CMAKE_CXX_COMPILER "clang++${CLANG_SUFFIX}")
  set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -fuse-ld=lld")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fuse-ld=lld")
endif()

if(CLANG_CL)
  set(CMAKE_C_COMPILER   "clang-cl")
  set(CMAKE_CXX_COMPILER "clang-cl")
  set(CMAKE_LINKER       "lld-link")
endif()

if(EMSCRIPTEN)
  set(CMAKE_C_COMPILER   "emcc")
  set(CMAKE_CXX_COMPILER "em++")

  set(OUT_FILE           "wasm3.html")
  set(APP_DIR            "platforms/emscripten")
endif()

if(M EQUAL 32)
  set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -m32")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "set build type to Release")
endif()

project(wasm3)

message("----")
message("Generator:  ${CMAKE_GENERATOR}")
message("Compiler:   ${CMAKE_C_COMPILER_ID}")
message("Build Type: ${CMAKE_BUILD_TYPE}")


include(CheckIPOSupported)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED YES)
set(CMAKE_C_EXTENSIONS NO)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

file(GLOB M3_SRC "source/*.c" "source/*.cpp" "${APP_DIR}/*.c")

include_directories("./source/")

if((CMAKE_C_COMPILER_ID MATCHES "GNU") OR
   (CMAKE_C_COMPILER_ID MATCHES "MSVC"))
  # Compile C files as C++
  set_source_files_properties(${M3_SRC} PROPERTIES LANGUAGE CXX)
  # Let m3.hpp file know
  add_definitions(-DM3_COMPILED_AS_CPP=1)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wstrict-aliasing -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-missing-field-initializers")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG=1 -ggdb -O0")

#-fno-optimize-sibling-calls

if(EMSCRIPTEN)
  set(CMAKE_C_FLAGS_RELEASE "-O2 -flto -Wfatal-errors")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 --lto-O3 --closure 1 --strip-all --gc-sections") # --emrun
  if(WASM_TCO)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Xclang -target-feature -Xclang +tail-call")
  endif()
elseif(WIN32)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DWIN32 /WX- /diagnostics:column")

  if (CMAKE_C_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oxs /Oy /W0 /Zi /Zo /arch:AVX2")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/DEBUG:FULL")
  else()
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oxs /Oy /W0 /GS- /Qvec -Clang -Oz")
  endif()
else()
  if (CMAKE_C_COMPILER_ID MATCHES "GNU")
    set(OPT_FLAGS "-O3")
  else()
    set(OPT_FLAGS "-Ofast")
  endif()

  set(CMAKE_C_FLAGS_RELEASE "${OPT_FLAGS} -march=native -Wfatal-errors -fstrict-aliasing") #-fno-inline
  set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-O0")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-O3")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

add_executable(${OUT_FILE} ${M3_SRC})
target_link_libraries(${OUT_FILE} m)

check_ipo_supported(RESULT result)
if(result)
  set_property(TARGET ${OUT_FILE} PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
  message("LTO:        ON")
else()
  message("LTO:        OFF")
endif()

message("----")
