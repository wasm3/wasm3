## C++ wrapper

This example shows how to embed WASM3 into a C++ application. It uses a header-only library, `wasm3_cpp.h`, provided in `wasm3_cpp` subdirectory. Like WASM3 itself, this library can be included into CMake projects using `add_subdirectory` function.

The main code of the example in `main.cpp` initializes WASM3, loads a WebAssembly module, links two external functions to the module, and executes two functions defined in WebAssembly.

The WebAssembly module source code is inside `wasm` subdirectory.

### `wasm3_cpp.h` reference

All the classes are located in `wasm3` namespace.

#### Class `environment`

`environment::environment()` — create a new WASM3 environment. Runtimes, modules are owned by an environment.

`runtime environment::new_runtime(size_t stack_size_bytes)` — create new runtime inside the environment.

`module environment::parse_module(std::istream &in)` or `module environment::parse_module(const uint8_t *data, size_t size)` — parse a WASM binary module.

#### Class `runtime`

`runtime` objects are created using `environment::new_runtime` method, see above.

`void runtime::load(module &m)` — load a parsed module into the runtime.

`function runtime::find_function(const char *name)` — find a function defined in one of the loaded modules, by name. Raises a `wasm3::error` exception if the function is not found.

#### Class `module`

`module` objects are created by `environment::parse_module`. Parsed modules can be loaded into a `runtime` object. One module can only be loaded into one runtime.

Before loading a module, you may need to link some external functions to it:

`template <Func> void module::link(const char *mod, Func *function, const char *function_name)` — link a function `function` to module named `mod` under the name `function_name`. To link to any module, use `mod="*"`. 

`function` has to be either a non-member function or a static member function.

Currently, the following types of arguments can be passed to functions linked this way:

* int32_t
* int64_t
* float
* double
* const/non-const pointers

Automatic conversion of other integral types may be implemented in the future.

If the module doesn't reference an imported function named `func`, an exception is thrown. To link a function "optionally", i.e. without throwing an exception if the function is not imported, use `module::link_optional` instead.

#### Class `function`

`function` object can be obtained from a `runtime`, looking up the function by name. Function objects are used to call WebAssembly functions.

`template <typename Ret> Ret function::call()` — call a WebAssembly function which doesn't take any arguments. The return value of the function is automatically converted to the type `Ret`. Note that you need to specify the return type when using this template function, and the type has to match the type returned by the WebAssembly function.

`template <typename Ret, typename ...Args> Ret function::call(Args...)` — same as above, but also allows passing arguments to the WebAssembly function.

`template <typename Ret, typename ...Args> Ret function::call_argv(Args...)` — same as above, except that this function takes arguments as C strings (`const char*`).

### Building and running

This directory is a CMake project, and can be built as follows:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

Then run the example:

```bash
./wasm3_cpp_example
```

