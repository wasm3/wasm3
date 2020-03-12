cmake_minimum_required(VERSION 3.9)

option(BUILD_WASI_SUPPORT "Build with WASI support" ON)

set(OUT_FILE "wasm3")
set(APP_DIR  "platforms/app")

if(HARDCODED) # Bundle a wasm binary and run a hard-coded func
  set(APP_DIR          "platforms/emscripten")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Dd_m3LogOutput=0")
endif()

if(CLANG OR CLANG_SUFFIX)
  set(CMAKE_C_COMPILER   "clang${CLANG_SUFFIX}")
  set(CMAKE_CXX_COMPILER "clang++${CLANG_SUFFIX}")
  set(CMAKE_C_FLAGS      "${CMAKE_C_FLAGS} -fuse-ld=lld")

  if(BUILD_FUZZ)
    set(OUT_FILE         "wasm3-fuzzer")
    set(APP_DIR          "platforms/app_fuzz")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "set build type to Debug")
    set(CMAKE_C_FLAGS    "${CMAKE_C_FLAGS} -Dd_m3LogOutput=0 -fsanitize=fuzzer,address")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=fuzzer,address")
  endif()
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

# Detect WasiEnv
if(DEFINED ENV{WASI_CC})
  set(WASIENV            1)
endif()

if(BUILD_32BIT)
  set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -m32")
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


file(GLOB app_srcs "${APP_DIR}/*.c")
add_executable(${OUT_FILE} ${app_srcs})

#-fno-optimize-sibling-calls

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG=1")

if(EMSCRIPTEN)
  set(CMAKE_C_FLAGS_RELEASE "-O2 -flto -Wfatal-errors")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 --lto-O3 --closure 1 --strip-all --gc-sections") # --emrun
  if(WASM_TCO)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Xclang -target-feature -Xclang +tail-call")
  endif()
elseif(WASIENV)

  if(BUILD_WASI_SUPPORT)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Dd_m3HasMetaWASI -Dd_m3HasTracer")
  endif()

  set(CMAKE_C_FLAGS_RELEASE "-O3 -Wfatal-errors -fomit-frame-pointer -fno-stack-check -fno-stack-protector")

  # TODO: LTO breaks wasm imports currently:
  # https://www.mail-archive.com/llvm-bugs@lists.llvm.org/msg36273.html

  #-flto -Wl,--lto-O3
  set(CMAKE_EXE_LINKER_FLAGS      "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,stack-size=8388608")
elseif(WIN32)

  if(BUILD_WASI_SUPPORT)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Dd_m3HasWASI")
  endif()

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_CRT_SECURE_NO_WARNINGS /WX- /diagnostics:column")

  string(REGEX REPLACE "/W[0-4]" "/W0" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

  if (CMAKE_C_COMPILER_ID MATCHES "MSVC")

    if("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /d2noftol3")
    endif()

    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oxs /Oy /GS- /Zi /Zo /arch:AVX2")

    # Uncomment this if you want to disassemble the release build,
    # for example: dumpbin /DISASM wasm3.exe /out:wasm3.S
    #set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG:FULL")

  else()
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oxs /Oy /GS- /Qvec -Clang -O3")
  endif()

  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:8388608") # stack size

else()

  if(BUILD_WASI_SUPPORT)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Dd_m3HasWASI -Dd_m3HasTracer") #-Dd_m3FixedHeap=1048576
  endif()

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wparentheses -Wundef -Wpointer-arith -Wstrict-aliasing=2")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=shadow -Werror=implicit-function-declaration") # -Werror=cast-align
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-missing-field-initializers")
  if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    # TODO: Place clang-specific options here
  elseif(CMAKE_C_COMPILER_ID MATCHES "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wjump-misses-init")
  endif()
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb -O0")

  set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -Wfatal-errors -fomit-frame-pointer -fno-stack-check -fno-stack-protector") #-fno-inline
  set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-O0")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-O3")

  target_link_libraries(${OUT_FILE} m)
endif()

check_ipo_supported(RESULT result)
if(result AND NOT WASIENV) # TODO: LTO breaks wasm imports
  set_property(TARGET ${OUT_FILE} PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
  message("LTO:        ON")
else()
  message("LTO:        OFF")
endif()

add_subdirectory(source)
target_link_libraries(${OUT_FILE} m3)

message("Flags:         ${CMAKE_C_FLAGS}")
message("Debug flags:   ${CMAKE_C_FLAGS_DEBUG}")
message("Release flags: ${CMAKE_C_FLAGS_RELEASE}")

message("----")
