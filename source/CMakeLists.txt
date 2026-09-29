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
    "m3_deterministic.c"
    "m3_env.c"
    "m3_exec.c"
    "m3_function.c"
    "m3_info.c"
    "m3_module.c"
    "m3_parse.c"
    "m3_snapshot.c"
    "m3_validate.c"
    "m3_xxh64.c"
)

add_library(m3 STATIC ${sources})

target_include_directories(m3 PUBLIC .)

# Runtimes that share a memory run on threads of their own, which the posix host starts
# with pthreads, and the atomic accesses of 64-bit cells compile to a library call on a
# target without an instruction for them (32-bit ARM before v7, MIPS, RISC-V 32, PowerPC
# 32), whose library is not linked by default. A libc that has pthreads built in makes the
# first a no-op, and a target that has the instruction needs nothing for the second.
# Windows and hosts with no threads ask for neither.
#
# What is found is linked, and then told to the sources as d_m3LinksThreadLibs, which is
# what lets them turn threads on by default - see d_m3ThreadLibsAvailable.
set(M3_THREAD_LIBS_LINKED FALSE)

if(WIN32)
    set(M3_THREAD_LIBS_LINKED TRUE)
elseif(NOT MSVC)
    find_package(Threads)

    include(CheckCSourceCompiles)
    include(CMakePushCheckState)

    set(M3_ATOMIC64_TEST "
        #include <stdint.h>
        int main (void)
        {
            static uint64_t cell = 0;
            uint64_t expected = 0;
            __atomic_store_n (&cell, 1, __ATOMIC_SEQ_CST);
            return (int) __atomic_load_n (&cell, __ATOMIC_SEQ_CST)
                 + __atomic_compare_exchange_n (&cell, &expected, 2, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        }")

    check_c_source_compiles("${M3_ATOMIC64_TEST}" M3_HAVE_ATOMIC64)

    if(NOT M3_HAVE_ATOMIC64)
        # a project that embeds this one may have libraries of its own in the way of the
        # probe, and expects to find them where it left them
        cmake_push_check_state()
        list(APPEND CMAKE_REQUIRED_LIBRARIES atomic)
        check_c_source_compiles("${M3_ATOMIC64_TEST}" M3_HAVE_ATOMIC64_IN_LIBATOMIC)
        cmake_pop_check_state()

        if(M3_HAVE_ATOMIC64_IN_LIBATOMIC)
            target_link_libraries(m3 PUBLIC atomic)
        endif()
    endif()

    if(Threads_FOUND)
        target_link_libraries(m3 PUBLIC Threads::Threads)

        if(M3_HAVE_ATOMIC64 OR M3_HAVE_ATOMIC64_IN_LIBATOMIC)
            set(M3_THREAD_LIBS_LINKED TRUE)
        endif()
    endif()
endif()

if(M3_THREAD_LIBS_LINKED)
    target_compile_definitions(m3 PUBLIC d_m3LinksThreadLibs)
endif()


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
