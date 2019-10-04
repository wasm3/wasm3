# M3/Wasm development notes


## Build on Linux

### Clang (recommended)

```sh
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
- [Ninja](https://github.com/ninja-build/ninja/releases)
- [Clang 9](https://releases.llvm.org/download.html#9.0.0)


### Clang with Ninja (recommended)

```bat
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
