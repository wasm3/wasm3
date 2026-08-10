# Multivalue

WebAssembly supports returning multiple values from a function.<br>
While support has been added for a while, compiling C/C++ code to WASM with multi-values can be a bit tricky, and Emscripten might not always produce correct results. 

To make this example work, `clang` is used directly with a few experimental settings.<br>
If you want to compile this, make sure your `clang` version is at least 11 and LLVM supports WASM.

