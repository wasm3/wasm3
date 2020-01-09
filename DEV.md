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
It can be easily integarted into any build system, as shown in `platforms`.

## Build on Linux

### Clang

```sh
mkdir build
cd build
cmake -GNinja -DCLANG_SUFFIX="-9" ..
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
set PATH=C:\Program Files\CMake\bin;%PATH%
set PATH=C:\Program Files\LLVM\bin;%PATH%
set PATH=C:\Python36-32\;C:\Python36-32\Scripts\;%PATH%
```

### Build with MSBuild

```sh
# Create a build directory as usual, then build a specific configuration
mkdir build
cd build

# Clang, x64
cmake -G"Visual Studio 16 2019" -A x64 -T ClangCL ..
cmake --build . --config Release

# Clang, x86
cmake -G"Visual Studio 16 2019" -A Win32 -T ClangCL ..
cmake --build . --config Release

# MSVC, x64
cmake -G"Visual Studio 16 2019" -A x64 ..
cmake --build . --config Release

# MSVC, x86
cmake -G"Visual Studio 16 2019" -A Win32 ..
cmake --build . --config Release
```

### Build with Ninja

```sh
# Clang
cmake -GNinja -DCLANG_CL=1 ..
ninja

# MSVC
cmake -GNinja ..
ninja
```

## Build for microcontrollers

In `./platforms/` folder you can find projects for different targets. Some of them are using Platformio, so you can follow the regular pio build process. Others have custom instructions in respective `README.md` files.
