# M3/Wasm development notes

M3/Wasm project uses CMake. It can also be easily integarted into any build system.
General build steps look like:
```sh
mkdir -p build
cd build
cmake ..
make -j8
```

It's recommended to use Clang/Ninja to build the project.

**Note:** When using GCC or Microsoft C++ Compiler, all `.c` files are forced to be comiled as `C++` to eliminate some errors.
Currently, support for these compilers was added for evaluation purposes.

## Build on Linux

### Clang (recommended)

```sh
mkdir -p build
cd build
cmake -GNinja -DCLANG_SUFFIX="-9" ..
ninja
```

### GCC

```sh
cmake -GNinja ..
ninja
```

## Build on Windows

You may need to install:
- [Build Tools for Visual Studio 2019](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2019)
- [CMake](https://cmake.org/download/)
- [Ninja](https://github.com/ninja-build/ninja/releases)
- [Clang 9](https://releases.llvm.org/download.html#9.0.0)

```bat
REM  Prepare environment (if needed):
"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set PATH=C:\Program Files\CMake\bin;%PATH%
set PATH=C:\Program Files\LLVM\bin;%PATH%
set PATH=C:\Python36-32\;C:\Python36-32\Scripts\;%PATH%
```

### Clang with Ninja (recommended)

```bat
mkdir build
cd build
cmake -GNinja -DCLANG_CL=1 ..
ninja
```

### Clang with MSBuild

```bat
cmake -G"Visual Studio 16 2019" -A x64 -T ClangCL ..
MSBuild /p:Configuration=Release wasm3.sln
```

### MSVC with MSBuild

```bat
cmake -G"Visual Studio 16 2019" -A x64 ..
MSBuild /p:Configuration=Release wasm3.sln
```

### MSVC with Ninja

```bat
cmake -GNinja ..
ninja
```

## Build using Emscripten

```sh
source /opt/emsdk/emsdk_env.sh --build=Release
mkdir -p build
cd build
cmake -GNinja -DEMSCRIPTEN=1 ..
ninja
```
**Note:** the build uses tail-call WebAssembly extension.
This is currently available in V8:
```sh
chrome --js-flags="--experimental-wasm-return-call --wasm-opt"
```

## Running WebAssembly spec tests

To run spec tests, you need `python3` and `WABT` (The WebAssembly Binary Toolkit).

```sh
cd test
python3 ./run-spec-test.py
```

It will automatically download, extract, preprocess the WebAssembly core test suite.
