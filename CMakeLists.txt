cmake_minimum_required(VERSION 3.9)

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

file(GLOB M3_SRC "source/*.c" "source/*.cpp")

if((CMAKE_C_COMPILER_ID MATCHES "GNU") OR
   (CMAKE_C_COMPILER_ID MATCHES "MSVC"))
  # Compile C files as C++
  set_source_files_properties(${M3_SRC} PROPERTIES LANGUAGE CXX)
  # Let m3.hpp file know
  add_definitions(-DM3_COMPILED_AS_CPP=1)
endif()


set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG=1 -ggdb")
if (CMAKE_C_COMPILER_ID MATCHES "GNU")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fpermissive") # TODO: remove
endif()

if(WIN32)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_MBCS) # -DM3_RUN_LOOPS=10

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /WX- /diagnostics:column")

  if (CMAKE_C_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Zi /Zo /Oxs /w /fp:fast /arch:AVX2")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/DEBUG:FULL")
  else()
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O3 /Oy /GS- /Qvec /w -Xclang -Oz /fp:fast") # /Gr
  endif()
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}") # /GR- /GX-
else()
  if (CMAKE_C_COMPILER_ID MATCHES "GNU")
    set(OPT_FLAGS "-O3")
  else()
    set(OPT_FLAGS "-Oz") # -Ofast
  endif()

  set(CMAKE_C_FLAGS_RELEASE "${OPT_FLAGS} -fomit-frame-pointer -march=native -w -Wfatal-errors") #-fno-inline
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}") # -fno-rtti -fno-exceptions
  set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-O0")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-O3")
endif()

add_executable(wasm3 ${M3_SRC})

check_ipo_supported(RESULT result)
if(result)
  set_property(TARGET wasm3 PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
  message("LTO:        ON")
else()
  message("LTO:        OFF")
endif()

message("----")
