file(GLOB_RECURSE M3_SOURCES "m3/*.c")

idf_component_register(SRCS "main.cpp" ${M3_SOURCES}
                    INCLUDE_DIRS "")

target_compile_options(${COMPONENT_LIB} PRIVATE -w -O3 -Dd_m3LogOutput=false -Dd_m3SkipStackCheck)

