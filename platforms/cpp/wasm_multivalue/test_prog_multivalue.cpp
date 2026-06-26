#include <stdint.h>

#define WASM_EXPORT(name) __attribute__((visibility("default"), export_name(#name))) name

struct Vec4 { 
    float x;
    float y;
    float z;
    float w;
};

struct MixedStruct { 
    uint32_t testU32;
    float testF32;
    double testF64;
    uint64_t testS64;
};

Vec4 WASM_EXPORT(vec4_add) (Vec4 a, Vec4 b) {
    return {
        a.x + b.x,
        a.y + b.y,
        a.z + b.z,
        a.w + b.w,
    };
}

Vec4 WASM_EXPORT(vec4_create) () {
    return {1.0f, 2.0f, 4.0f, 8.0f};
}

MixedStruct WASM_EXPORT(mixed_type) () {
    return {
        42, 
        128.75f,
        1.125,
        0xAABB'CCDD'EEFF'1122,
    };
}

