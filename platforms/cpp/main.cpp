#include <cstdio>
#include <cstring>
#include <fstream>

#include "wasm3_cpp.h"
#include "wasm/test_prog.wasm.h"

typedef float f32;
typedef double f64;

struct Vec4 {
    f32 x;
    f32 y;
    f32 z;
    f32 w;
};

struct Struct3Floats { f32 x[3]; };
struct Struct5Floats { f32 x[5]; };

struct MixedStruct { 
    uint32_t a;
    f32 b;
    f64 c;
    uint64_t d;
};

int sum(int a, int b)
{
    return a + b;
}

void * ext_memcpy (void* dst, const void* arg, int32_t size)
{
    return memcpy(dst, arg, (size_t) size);
}

int main(void)
{
    std::cout << "Loading WebAssembly..." << std::endl;

    /* Wasm module can be loaded from a file */
    try {
        wasm3::environment env;
        wasm3::runtime runtime = env.new_runtime(1024);
        const char* file_name = "wasm/test_prog.wasm";
        std::ifstream wasm_file(file_name, std::ios::binary | std::ios::in);
        if (!wasm_file.is_open()) {
            throw std::runtime_error("Failed to open wasm file");
        }
        wasm3::module mod = env.parse_module(wasm_file);
        runtime.load(mod);
    }
    catch(std::runtime_error &e) {
        std::cerr << "WASM3 error: " << e.what() << std::endl;
        return 1;
    }

    /* Wasm module can also be loaded from an array */
    try {
        wasm3::environment env;
        wasm3::runtime runtime = env.new_runtime(1024);
        wasm3::module mod = env.parse_module(test_prog_wasm, test_prog_wasm_len);
        runtime.load(mod);

        mod.link("*", "sum", sum);
        mod.link("*", "ext_memcpy", ext_memcpy);

        {
            wasm3::function test_fn = runtime.find_function("test");
            auto res = test_fn.call<int>(20, 10);
            std::cout << "result: " << res << std::endl;
        }
        {
            wasm3::function memcpy_test_fn = runtime.find_function("test_memcpy");
            auto res = memcpy_test_fn.call<int64_t>();
            std::cout << "result: 0x" << std::hex << res << std::dec << std::endl;
        }

        /**
         * Calling functions that modify an internal state, with mixed argument / return types
         */
        {
            wasm3::function counter_get_fn = runtime.find_function("test_counter_get");
            wasm3::function counter_inc_fn = runtime.find_function("test_counter_inc");
            wasm3::function counter_add_fn = runtime.find_function("test_counter_add");

            // call with no arguments and a return value
            auto value = counter_get_fn.call<int32_t>();
            std::cout << "counter: " << value << std::endl;

            // call with no arguments and no return value
            counter_inc_fn.call();
            value = counter_get_fn.call<int32_t>();
            std::cout << "counter after increment: " << value << std::endl;

            // call with one argument and no return value
            counter_add_fn.call(42);
            value = counter_get_fn.call<int32_t>();
            std::cout << "counter after adding value: " << value << std::endl;
        }
    }
    catch(wasm3::error &e) {
        std::cerr << "WASM3 error: " << e.what() << std::endl;
        return 1;
    }

    /** 
     * Wasm module with multivalue support
     * https://github.com/WebAssembly/multi-value/blob/master/proposals/multi-value/Overview.md
     */
    try {
        wasm3::environment env;
        wasm3::runtime runtime = env.new_runtime(1024);
        const char* file_name = "wasm_multivalue/test_prog_multivalue.wasm";
        std::ifstream wasm_file(file_name, std::ios::binary | std::ios::in);
        if (!wasm_file.is_open()) {
            throw std::runtime_error("Failed to open wasm file");
        }

        wasm3::module mod = env.parse_module(wasm_file);
        runtime.load(mod);
        {
            wasm3::function vec4_add_fn = runtime.find_function("vec4_add");
            wasm3::function vec4_create_fn = runtime.find_function("vec4_create");
            wasm3::function mixed_type_fn = runtime.find_function("mixed_type");

            // return as tuple with structured binding
            auto [x,y,z,w] = vec4_create_fn.call_tuple<f32, f32, f32, f32>();
            std::cout << "Vec4: " << x << ", " << y << ", " << z << ", " << w << std::endl;

            // return multivalue mapped to struct
            auto vec = vec4_create_fn.call_mapped<Vec4>();
            std::cout << "Vec4: " << vec.x << ", " << vec.y << ", " << vec.z << ", " << vec.w << std::endl;

            // if the provided struct is too small or big, a "argument count mismatch" error is thrown,
            // this prevents OOB access or uninitialized values.
            try {
                auto vec5 = vec4_create_fn.call_mapped<Struct5Floats>();
                return 1; // should not be reached, throws
            } catch(wasm3::error &e) {
                std::cerr << "Expected WASM3 error: " << e.what() << std::endl;
            }

            try {
                auto vec3 = vec4_create_fn.call_mapped<Struct3Floats>();
                return 1; // should not be reached, throws
            } catch(wasm3::error &e) {
                std::cerr << "Expected WASM3 error: " << e.what() << std::endl;
            }

            // with arguments
            {
                auto [x,y,z,w] = vec4_add_fn.call_tuple<f32, f32, f32, f32>(
                    1.0f, 2.0f, 3.0f, 4.0f,
                    0.5f, 0.25f, 0.125f, 0.0625f
                );
                std::cout << "Vec4 Sum: " << x << ", " << y << ", " << z << ", " << w << std::endl;

                auto vec = vec4_add_fn.call_mapped<Vec4>(
                    1.0f, 2.0f, 3.0f, 4.0f,
                    0.5f, 0.25f, 0.125f, 0.0625f
                );
                std::cout << "Vec4 Sum: " << vec.x << ", " << vec.y << ", " << vec.z << ", " << vec.w << std::endl;
            }

            // multivalue with mixed types
            {
                auto [a,b,c,d] = mixed_type_fn.call_tuple<uint32_t, f32, f64, uint64_t>();
                std::cout << "Data: " << a << ", " << b << ", " << c << ", " << d << std::endl;

                auto data = mixed_type_fn.call_mapped<MixedStruct>();
                std::cout << "Data: " << data.a << ", " << data.b << ", " << data.c << ", " << data.d << std::endl;
            }
        }
    }
    catch(wasm3::error &e) {
        std::cerr << "WASM3 error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
