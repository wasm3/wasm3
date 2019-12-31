set(srcs
    "m3_api_libc.c"
    "m3_api_meta_wasi.c"
    "m3_api_wasi.c"
    "m3_bind.c"
    "m3_code.c"
    "m3_compile.c"
    "m3_core.c"
    "m3_emit.c"
    "m3_env.c"
    "m3_exec.c"
    "m3_info.c"
    "m3_module.c"
    "m3_optimize.c"
    "m3_parse.c"
)

add_library(m3 STATIC ${srcs})

target_include_directories(m3 PUBLIC .)

if (CMAKE_C_COMPILER_ID MATCHES "MSVC")
    # add MSVC specific flags here
else()
    # Flags common for GCC and Clang

    # FIXME: cast to 'void *' from smaller integer type 'i32'
    set_source_files_properties(m3_emit.c PROPERTIES COMPILE_FLAGS -Wno-int-to-pointer-cast)

    # FIXME: comparison of integers of different signs: 'u32' and 'i32'
    set_source_files_properties(m3_env.c PROPERTIES COMPILE_FLAGS -Wno-sign-compare)

    if (WASIENV)
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
