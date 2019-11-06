file(GLOB_RECURSE M3_SOURCES "m3/*.c")

idf_component_register(SRCS "main.c" ${M3_SOURCES}
                    INCLUDE_DIRS "")

target_compile_options(${COMPONENT_LIB} PRIVATE -w -O3)

