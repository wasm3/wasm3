cmake_minimum_required(VERSION 3.11)

# Detect a cross-build to Wasm itself. The WASI SDK's toolchain file is only read
# at project(), which is too late for the choices below, so this looks for the
# prefix that CMAKE_TOOLCHAIN_FILE is passed alongside:
#   cmake -DCMAKE_TOOLCHAIN_FILE=$WASI_SDK_PATH/share/cmake/wasi-sdk-p1.cmake \
#         -DWASI_SDK_PREFIX=$WASI_SDK_PATH ..
if(DEFINED WASI_SDK_PREFIX)
  set(WASI_SDK          1)
endif()

# Detect MinGW
if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "GNU")
  set(MINGW             1)
endif()

# Set options

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "set build type to Release")
endif()

if(WASI_SDK)
  set(BUILD_WASI "metawasi" CACHE STRING "WASI implementation")
elseif(EMSCRIPTEN OR EMSCRIPTEN_LIB)
  set(BUILD_WASI "none" CACHE STRING "WASI implementation")
else()
  set(BUILD_WASI "uvwasi" CACHE STRING "WASI implementation")
endif()
set_property(CACHE BUILD_WASI PROPERTY STRINGS none simple uvwasi metawasi)

option(BUILD_NATIVE "Build with machine-specific optimisations" ON)

# The tests under test/internal reach for engine internals and for API corners a
# .wasm module cannot get at from the outside, so they are not part of a normal
# build. The CI linux job turns them on.
option(BUILD_TESTS  "Build the internal API tests" OFF)

# Wasm3 is meant to build clean, so CI asks for this and a new warning fails the
# job rather than scrolling past in a log nobody reads. It applies to Wasm3's own
# targets only - libuv and uvwasi are fetched and built here too, and their
# warnings are not ours to fix.
option(BUILD_WERROR "Treat compiler warnings as errors" OFF)

set(OUT_FILE "wasm3")

if(NOT APP_DIR)
  set(APP_DIR  "platforms/app")
endif()

# Configure the toolchain

if(CLANG OR CLANG_SUFFIX)
  set(CMAKE_C_COMPILER   "clang${CLANG_SUFFIX}")
  set(CMAKE_CXX_COMPILER "clang++${CLANG_SUFFIX}")

  if(BUILD_FUZZ)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")
    set(OUT_FILE         "wasm3-fuzzer")
    set(APP_DIR          "platforms/app_fuzz")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "set build type to Debug")
    set(CMAKE_C_FLAGS    "${CMAKE_C_FLAGS} -fsanitize=fuzzer,address")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=fuzzer,address")
  endif()
endif()

if(WASI_SDK)
  set(OUT_FILE           "wasm3.wasm")
endif()

if(CLANG_CL)
  set(CMAKE_C_COMPILER   "clang-cl")
  set(CMAKE_CXX_COMPILER "clang-cl")
  set(CMAKE_LINKER       "lld-link")
endif()

if(EMSCRIPTEN OR EMSCRIPTEN_LIB)
  set(CMAKE_C_COMPILER   "emcc")
  set(CMAKE_CXX_COMPILER "em++")
  # naming the compiler is not enough once anything here is archived: the system
  # ar cannot read the bitcode emcc emits, and reports it as an unknown attribute
  set(CMAKE_AR           "emar")
  set(CMAKE_RANLIB       "emranlib")

  if (EMSCRIPTEN_LIB)
    set(APP_DIR          "platforms/emscripten_lib")
    set(OUT_FILE         "wasm3.wasm")
    set(CMAKE_C_FLAGS    "${CMAKE_C_FLAGS} -s STANDALONE_WASM")
  else()
    set(APP_DIR          "platforms/emscripten")
    set(OUT_FILE         "wasm3.html")
  endif()
endif()

if(BUILD_32BIT)
  set(CMAKE_C_FLAGS      "${CMAKE_C_FLAGS}   -m32")
endif()



project(wasm3)

message("----")
message("Generator:  ${CMAKE_GENERATOR}")
message("Compiler:   ${CMAKE_C_COMPILER_ID}")
message("Build Type: ${CMAKE_BUILD_TYPE}")


include(CheckIPOSupported)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED YES)
set(CMAKE_C_EXTENSIONS YES)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS YES)


file(GLOB app_srcs "${APP_DIR}/*.c")
add_executable(${OUT_FILE} ${app_srcs})

#-fno-optimize-sibling-calls

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG=1")

if(EMSCRIPTEN OR EMSCRIPTEN_LIB)

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s GLOBAL_BASE=1024 -s TOTAL_STACK=2MB -s INITIAL_MEMORY=4MB -s ALLOW_MEMORY_GROWTH")

  # The stack above is the one the guest recursion runs on, and the default budget
  # is larger than all of it: the low-water mark would land below address zero,
  # where every call compares as past the limit and traps on the spot
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Dd_m3MaxNativeStack=1048576")

  if(EMSCRIPTEN_LIB)
    # a library has no main to enter through - the interface is the functions
    # main.c marks EMSCRIPTEN_KEEPALIVE, plus the allocator the caller needs
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s EXPORTED_FUNCTIONS=\"[\\\"_malloc\\\",\\\"_free\\\"]\" --no-entry")
  else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s EXPORTED_FUNCTIONS=\"[\\\"_malloc\\\",\\\"_free\\\",\\\"_main\\\"]\"")
  endif()

  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -flto -Wfatal-errors -s ASSERTIONS=0")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} --strip-all --gc-sections")

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mbulk-memory -mnontrapping-fptoint -msign-ext -mtail-call -mmultivalue -mmutable-globals")

elseif(WASI_SDK)

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Dd_m3HasTracer")
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -Wfatal-errors -fomit-frame-pointer -fno-stack-check -fno-stack-protector -fno-asynchronous-unwind-tables")

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

  # WASI has no process-associated clock, so wasi-libc only offers clock() (used by
  # m3_libc_clock_ms) as an opt-in emulation over the wall clock
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_WASI_EMULATED_PROCESS_CLOCKS")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lwasi-emulated-process-clocks")

  # The interpreter dispatches, calls and returns through tail calls, so the Wasm build
  # needs the tail-call feature: without it Clang rejects every M3_MUSTTAIL, and
  # return_call would consume a stack frame per call instead of reusing one.
  # Requires a tail-call capable host (Wasm3 >= 0.9.0, Wasmtime >= 14, Node >= 22).
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mbulk-memory -mnontrapping-fptoint -msign-ext -mtail-call -mmultivalue -mmutable-globals")

  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,stack-size=8388608 -flto -Wl,--lto-O3")

elseif(MSVC OR CMAKE_C_COMPILER_FRONTEND_VARIANT MATCHES "MSVC")

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Dd_m3HasTracer -D_CRT_SECURE_NO_WARNINGS /WX- /diagnostics:column")

  string(REGEX REPLACE "/W[0-4]" "/W3" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

  if (CMAKE_C_COMPILER_ID MATCHES "MSVC")

    # The traditional preprocessor does not drop the comma before an empty
    # __VA_ARGS__, so every op built through d_m3OpMacro is passed one argument
    # too many. It ignores the extra one, but produces unnecessary warnings.
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Zc:preprocessor")

    if("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /d2noftol3")
    endif()

    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oxs /Oy /GS- /Zi /Zo /arch:AVX2")

    # Uncomment this if you want to disassemble the release build,
    # for example: dumpbin /DISASM wasm3.exe /out:wasm3.S
    #set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG:FULL")

  else()
    # /W3 is Clang's own -Wall here, so it needs the same answer the GCC and Clang
    # builds below give it: a WASI entry point has to declare every argument it is
    # handed, including the ones its implementation ignores
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-variable -Wno-unused-function")

    # /Oxs on its own is -Os to clang-cl, and /clang: is how a driver option gets
    # through. The "-Clang -O3" that used to be here was dropped as unknown, so
    # these builds have been optimising for size
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oxs /Oy /GS- /Qvec /clang:-O3")
  endif()

  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:8388608") # stack size

else()

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Dd_m3HasTracer") #-Dd_m3FixedHeap=1048576
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wparentheses -Wundef -Wpointer-arith -Wstrict-aliasing=2")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=implicit-function-declaration") # -Werror=cast-align
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-missing-field-initializers")
  if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    # TODO: Place clang-specific options here
  elseif(CMAKE_C_COMPILER_ID MATCHES "Intel")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fp-model precise")
  elseif(CMAKE_C_COMPILER_ID MATCHES "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wjump-misses-init")
  endif()
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb -O0")

  set(CMAKE_C_FLAGS_RELEASE "-O3 -Wfatal-errors -fomit-frame-pointer -fno-stack-check -fno-stack-protector -fno-asynchronous-unwind-tables") #-fno-inline

  if(BUILD_NATIVE)
    if(APPLE AND CMAKE_C_COMPILER_ID MATCHES "Clang" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
      set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -mcpu=native")
    elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64")
      set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -march=native")
      # Asking for the host's own feature set means inheriting whatever the compiler
      # makes of it. I.e. avx10.1-256 is compiled for avx10.1-512.
      # Thus -Werror fails on some CI runners and not others.
      if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 18)
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Wno-invalid-feature-combination")
      endif()
    endif()
  endif()

  set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-O0")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-O3")

  # Cygwin and MinGW produce a PE, whose stack is whatever its header asks for
  if(WIN32 OR CYGWIN)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--stack,8388608")
  endif()

  target_link_libraries(${OUT_FILE} m)
  set(M3_LINK_LIBM 1)       # the internal tests link the same way, see BUILD_TESTS

endif()

target_link_libraries(${OUT_FILE} m3)

if(BUILD_WERROR)
  if(MSVC OR CMAKE_C_COMPILER_FRONTEND_VARIANT MATCHES "MSVC")
    set(M3_WERROR /WX)
  else()
    set(M3_WERROR -Werror)
  endif()
endif()

if(BUILD_WASI MATCHES "simple")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Dd_m3HasWASI")
elseif(BUILD_WASI MATCHES "metawasi")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Dd_m3HasMetaWASI")
elseif(BUILD_WASI MATCHES "uvwasi")
  include(FetchContent)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)
  set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(UVWASI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(INSTALL_UVWASI OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    libuv
    GIT_REPOSITORY https://github.com/libuv/libuv.git
    GIT_TAG        v1.52.1
  )
  FetchContent_Declare(
    uvwasi
    GIT_REPOSITORY  https://github.com/nodejs/uvwasi.git
    GIT_TAG         main
  )
  FetchContent_MakeAvailable(libuv)
  FetchContent_MakeAvailable(uvwasi)
  include_directories("${uvwasi_SOURCE_DIR}/include")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Dd_m3HasUVWASI")
  target_link_libraries(${OUT_FILE} uvwasi_a uv_a)
  set(M3_WASI_LIBS uvwasi_a uv_a)   # ...and so do the internal tests
endif()

check_ipo_supported(RESULT result)
if(result)
  set_property(TARGET ${OUT_FILE} PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
  message("LTO:        ON")
else()
  message("LTO:        OFF")
endif()

add_subdirectory(source)

if(M3_WERROR)
  target_compile_options(${OUT_FILE} PRIVATE ${M3_WERROR})
endif()

# Each of these is a standalone program that exercises the embedding API and
# reports failures through its exit code. They link the same way the app does -
# m3 carries source/ as a public include directory, so only the extension
# headers have to be named here.
if(BUILD_TESTS)
  # m3_test also drives the extension API - m3_NewModule, m3_InjectFunction -
  # which is not part of libm3 because it is opt-in, so it is compiled in here
  add_executable(m3_test "test/internal/m3_test.c" "source/extensions/m3_extensions.c")
  target_include_directories(m3_test PRIVATE "source/extensions")

  add_executable(m3_test_reftypes "test/internal/m3_test_reftypes.c")

  foreach(m3_test_name m3_test m3_test_reftypes)
    target_link_libraries(${m3_test_name} m3 ${M3_WASI_LIBS})
    if(M3_LINK_LIBM)
      target_link_libraries(${m3_test_name} m)
    endif()
    if(M3_WERROR)
      target_compile_options(${m3_test_name} PRIVATE ${M3_WERROR})
    endif()
  endforeach()

  message("Tests:      ON")
endif()

message("Flags:         ${CMAKE_C_FLAGS}")
message("Debug flags:   ${CMAKE_C_FLAGS_DEBUG}")
message("Release flags: ${CMAKE_C_FLAGS_RELEASE}")

message("----")

# Install

include(GNUInstallDirs)

install(TARGETS ${OUT_FILE} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
