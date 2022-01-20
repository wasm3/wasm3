#include <cstdio>
#include <cstring>
#include <fstream>

#include "wasm3_cpp.h"
#include "wasm/test_prog.wasm.h"

struct Vec3
{
  float x;
  float y;
  float z;
};

struct Vec4
{
  float x;
  float y;
  float z;
  float w;
};

struct Vec5
{
  float x;
  float y;
  float z;
  float w;
  float v;
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
            //wasm3::function vec4_add_fn = runtime.find_function("vec4_add");
            wasm3::function vec4_create_fn = runtime.find_function("vec4_create");
            //wasm3::function vec4_mixed_type_fn = runtime.find_function("mixed_type");

            // call with no arguments and a return value
            auto [x,y,z,w] = vec4_create_fn.callMultivalue2<float, float, float, float>();
            std::cout << "Vec4: " << x << ", " << y << ", " << z << ", " << w << std::endl;

            auto vec = vec4_create_fn.callMultivalue<Vec4>();
            std::cout << "Vec4: " << vec.x << ", " << vec.y << ", " << vec.z << ", " << vec.w << std::endl;

            try {
                auto vec5 = vec4_create_fn.callMultivalue<Vec5>();
                std::cout << "Vec5: " << vec5.x << ", " << vec5.y << ", " << vec5.z << ", " << vec5.w << ", " << vec5.v << std::endl;
            } catch(wasm3::error &e) {
                std::cerr << "WASM3 error: " << e.what() << std::endl;
            }

            try {
                auto vec3 = vec4_create_fn.callMultivalue<Vec3>();
                std::cout << "Vec3: " << vec3.x << ", " << vec3.y << ", " << vec3.z << ", " << std::endl;
            } catch(wasm3::error &e) {
                std::cerr << "WASM3 error: " << e.what() << std::endl;
            }
        }
    }
    catch(wasm3::error &e) {
        std::cerr << "WASM3 error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
