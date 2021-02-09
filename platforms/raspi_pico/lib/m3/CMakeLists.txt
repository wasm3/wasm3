# FIXME: should be including source/CMakeLists.txt, but Pico SDK 
#        compiler doesn't like -std=c99 defined there

set(path "${CMAKE_CURRENT_LIST_DIR}/../../../../source/")

set(sources
    "${path}m3_api_libc.c"
    "${path}m3_api_wasi.c"
    "${path}m3_api_uvwasi.c"
    "${path}m3_api_meta_wasi.c"
    "${path}m3_api_tracer.c"
    "${path}m3_bind.c"
    "${path}m3_code.c"
    "${path}m3_compile.c"
    "${path}m3_core.c"
    "${path}m3_emit.c"
    "${path}m3_env.c"
    "${path}m3_exec.c"
    "${path}m3_info.c"
    "${path}m3_module.c"
    "${path}m3_parse.c"
)

add_library(m3 STATIC ${sources})
target_include_directories(m3 PUBLIC ${path})

# FIXME: cast to 'void *' from smaller integer type 'i32'
set_source_files_properties(${path}m3_emit.c PROPERTIES COMPILE_FLAGS -Wno-int-to-pointer-cast)

# FIXME: comparison of integers of different signs: 'u32' and 'i32'
set_source_files_properties(${path}m3_env.c PROPERTIES COMPILE_FLAGS -Wno-sign-compare)

