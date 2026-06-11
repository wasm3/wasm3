# Wasm3 development notes

This project uses CMake.
General build steps look like:
```sh
mkdir -p build
cd build
cmake ..
make -j8
```

Wasm3 is continuously tested with Clang, GCC, MSVC compilers, and on multiple platforms.
It can be easily integrated into any build system, as shown in `platforms`.

## Build on Linux, OS X

### Clang

```sh
mkdir build
cd build
cmake -GNinja -DCLANG_SUFFIX="-12" ..
ninja
```

### GCC

```sh
mkdir build
cd build
cmake -GNinja ..
ninja
```

## Build on Windows

Prerequisites:
- [Build Tools for Visual Studio 2019](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2019)
- [CMake](https://cmake.org/download/)
- [Ninja](https://github.com/ninja-build/ninja/releases)
- [Clang 9](https://releases.llvm.org/download.html#9.0.0)

Recommended tools:
- [Cmder](https://cmder.net/)
- [Python3](https://www.python.org/downloads/)
- [ptime](http://www.pc-tools.net/win32/ptime/)

```sh
# Prepare environment (if needed):
"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set PATH=C:\Python36-32\;C:\Python36-32\Scripts\;%PATH%
```

### Build with MSBuild

```sh
# Create a build directory as usual, then build a specific configuration
mkdir build
cd build

# Configure Clang, x64
cmake -G"Visual Studio 16 2019" -A x64 -T ClangCL ..

# Configure Clang, x86
cmake -G"Visual Studio 16 2019" -A Win32 -T ClangCL ..

# Configure MSVC, x64
cmake -G"Visual Studio 16 2019" -A x64 ..

# Configure MSVC, x86
cmake -G"Visual Studio 16 2019" -A Win32 ..

# Build
cmake --build . --config Release
cp ./Release/wasm3.exe ./
```

### Build with Ninja

```sh
set PATH=C:\Program Files\CMake\bin;%PATH%
set PATH=C:\Program Files\LLVM\bin;%PATH%

# Clang
cmake -GNinja -DCLANG_CL=1 ..
ninja

# MSVC
cmake -GNinja ..
ninja
```

## Build using compiler directly

This can be useful for cross-compilation, quick builds or when a build system (CMake, Ninja, etc.) is not available.

### gcc/clang
```sh
gcc -O3 -g0 -s -Isource -Dd_m3HasWASI source/*.c platforms/app/main.c -lm -o wasm3
```

### msvc/clang-cl
```sh
cl source/*.c platforms/app/main.c /Isource /MD /Ox /Oy /Gw /GS- /W0 /Dd_m3HasWASI /Fewasm3.exe /link advapi32.lib
```

### mingw-w64
```sh
x86_64-w64-mingw32-gcc -O3 -g0 -s -Isource -Dd_m3HasWASI source/*.c platforms/app/main.c -lm -lpthread -static -o wasm3.exe
```

## Build for microcontrollers

In `./platforms/` folder you can find projects for different targets. Some of them are using Platformio, so you can follow the regular pio build process. Others have custom instructions in respective `README.md` files.

## Build with WasiEnv

```sh
wasimake cmake -GNinja ..
ninja
```

If you want to enable experimental WASM features during the build:

```sh
export CFLAGS="-Xclang -target-feature -Xclang +tail-call"
wasimake cmake -GNinja ..
ninja
```

Here's how some options can be used with different tools:

```log
Clang target-feature option          WABT option                        Chromium --js-flags option
----------------------------------------------------------------------------------------------------------------
+multivalue                          --enable-multi-value               --experimental-wasm-mv
+tail-call                           --enable-tail-call                 --experimental-wasm-return-call
+bulk-memory                         --enable-bulk-memory               --experimental-wasm-bulk-memory
+nontrapping-fptoint                 --enable-saturating-float-to-int   --experimental-wasm-sat-f2i-conversions
+sign-ext                            --enable-sign-extension            --experimental-wasm-se
+simd128, +unimplemented-simd128     --enable-simd                      --experimental-wasm-simd
```

```sh
# List clang options:
llc -march=wasm32 -mattr=help

# List Chromium options:
chromium-browser --single-process --js-flags="--help" 2>&1 | grep wasm
```

## Build with Zig

Grab the latest nightly Zig toolchain from [ziglang.org/download], and then simply run:

[ziglang.org/download]: https://ziglang.org/download/

```sh
zig build
```

This will install `wasm3` compiled for your target architecture in `zig-out/bin/wasm3`.

In order to build `wasm3` in a mode different than Debug, e.g., in ReleaseFast, pass in
an appropriate flag like so:

```sh
zig build -Drelease-fast
```

If you want to cross-compile to some specific target, pass in the target with a flag like so:

```sh
zig build -Dtarget=wasm32-wasi
```

Or if targeting Apple Silicon (this works from *any* host with Zig):

```sh
zig build -Dtarget=aarch64-macos
```
