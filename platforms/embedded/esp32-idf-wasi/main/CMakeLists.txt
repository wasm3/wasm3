file(GLOB_RECURSE M3_SOURCES "wasm3/*.c")

set(APP_SOURCES "main.cpp" "m3_api_esp_wasi.c")

idf_component_register(SRCS ${APP_SOURCES} ${M3_SOURCES}
                    INCLUDE_DIRS "wasm3")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -O3 -DESP32 -Dd_m3MaxFunctionStackHeight=256)

# Disable harmless warnings
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-missing-field-initializers)
