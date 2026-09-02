## C++ wrapper

This example shows how to embed WASM3 into a C++ application. It uses a header-only library, `wasm3_cpp.h`, provided in `wasm3_cpp` subdirectory. Like WASM3 itself, this library can be included into CMake projects using `add_subdirectory` function.

The main code of the example in `main.cpp` initializes WASM3, loads a WebAssembly module, links two external functions to the module, and executes two functions defined in WebAssembly.

The WebAssembly module source code is inside `wasm` subdirectory.

### `wasm3_cpp.h` reference

All the classes are located in `wasm3` namespace.

#### Class `wasm_environment`

`wasm_environment::wasm_environment()` - create a new WASM3 environment. Runtimes, modules are owned by an environment.

`wasm_runtime wasm_environment::new_runtime(uint32_t stack_size_bytes)` - create new runtime inside the environment.

`wasm_module wasm_environment::parse_module(std::istream &in)` or `wasm_module wasm_environment::parse_module(const uint8_t *data, size_t size)` - parse a WASM binary module.

#### Class `wasm_runtime`

`wasm_runtime` objects are created using `wasm_environment::new_runtime` method, see above.

`void wasm_runtime::load(wasm_module &m)` - load a parsed module into the runtime.

`wasm_function wasm_runtime::find_function(const char *name)` - find a function defined in one of the loaded modules, by name. Raises a `wasm3::error` exception if the function is not found.

#### Class `wasm_module`

`wasm_module` objects are created by `wasm_environment::parse_module`. Parsed modules can be loaded into a `wasm_runtime` object. One module can only be loaded into one runtime.

Before loading a module, you may need to link some external functions to it:

`template <typename Func> void wasm_module::link(const char *mod, const char *function_name, Func *function)` - link a function `function` to module named `mod` under the name `function_name`. To link to any module, use `mod="*"`. 

`function` has to be either a non-member function or a static member function.

Currently, the following types of arguments can be passed to functions linked this way:

* int32_t
* int64_t
* float
* double
* const/non-const pointers

Automatic conversion of other integral types may be implemented in the future.

If the module doesn't reference an imported function named `func`, an exception is thrown. To link a function "optionally", i.e. without throwing an exception if the function is not imported, use `wasm_module::link_optional` instead.

#### Class `wasm_function`

`wasm_function` object can be obtained from a `wasm_runtime`, looking up the function by name. Function objects are used to call WebAssembly functions.

`template <typename Ret = void, typename ...Args> Ret wasm_function::call(Args...)` - calls a WebAssembly function with or without arguments and a return value.<br>
The return value of the function, if not `void`, is automatically converted to the type `Ret`.<br> 
Note that you always need to specify the matching return type when using this template with a non-void function.<br> 
Examples:
```cpp
// WASM signature: [] → []
func.call();

// WASM signature: [i32, i32] → []
func.call(42, 43); // implicit argument types

// WASM signature: [i32, i64] → []
func.call<void, int32_t, int64_t>(42, 43); // explicit argument types require the return type

// WASM signature: [] → [i32]
auto result = func.call<int32_t>();

// WASM signature: [i32, i32] → [i64]
auto result = func.call<int64_t>(42, 43); // implicit argument types

// WASM signature: [i32, i64] → [i64]
auto result = func.call<int64_t, int32_t, int64_t>(42, 43); // explicit argument types

```

`template <typename Ret, typename ...Args> Ret wasm_function::call_argv(Args...)` - same as above, except that this function takes arguments as C strings (`const char*`).

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

