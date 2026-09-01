set(sources
    "m3_api_libc.c"
    "m3_api_wasi.c"
    "m3_api_uvwasi.c"
    "m3_api_meta_wasi.c"
    "m3_api_tracer.c"
    "m3_bind.c"
    "m3_code.c"
    "m3_compile.c"
    "m3_core.c"
    "m3_env.c"
    "m3_exec.c"
    "m3_function.c"
    "m3_info.c"
    "m3_module.c"
    "m3_parse.c"
    "m3_validate.c"
)

add_library(m3 STATIC ${sources})

target_include_directories(m3 PUBLIC .)

# set by the top-level CMakeLists for BUILD_WERROR, and applied per target so
# that a fetched dependency does not have to build clean under our flags
if(M3_WERROR)
    target_compile_options(m3 PRIVATE ${M3_WERROR})
endif()

# Compilers CMake has no feature table for (TinyCC, for one) can't be asked for
# c_std_99; the C_STANDARD set at the top level covers them
if(CMAKE_C_COMPILE_FEATURES)
    target_compile_features(m3 PRIVATE c_std_99)
endif()

if (CMAKE_C_COMPILER_ID MATCHES "MSVC")
    # add MSVC specific flags here
else()
    # Flags common for GCC and Clang


    # FIXME: comparison of integers of different signs: 'u32' and 'i32'
    set_source_files_properties(m3_env.c PROPERTIES COMPILE_FLAGS -Wno-sign-compare)

    if (WASI_SDK)
        # FIXME: declaration of 'struct sigaction' will not be visible outside of this function
        target_compile_options(m3 PUBLIC -Wno-visibility)
        # FIXME incompatible pointer types passing 'u32 *' to parameter of type 'char **'
        set_source_files_properties(m3_api_meta_wasi.c PROPERTIES COMPILE_FLAGS -Wno-incompatible-pointer-types)
    endif()

    if (CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Clang specific flags here
    else()
        # GCC specific flags here
    endif()
endif()

if(BUILD_WASI MATCHES "simple")
    target_compile_definitions(m3 PUBLIC d_m3HasWASI)
elseif(BUILD_WASI MATCHES "metawasi")
    target_compile_definitions(m3 PUBLIC d_m3HasMetaWASI)
elseif(BUILD_WASI MATCHES "uvwasi")
    target_compile_definitions(m3 PUBLIC d_m3HasUVWASI)
    include_directories("${libuv_SOURCE_DIR}/include")
endif()
