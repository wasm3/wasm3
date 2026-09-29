# Wasm3 development notes

This project uses CMake.
General build steps look like:
```sh
cmake -S . -B build
cmake --build build
```

Wasm3 is continuously tested with Clang, GCC, TinyCC, MSVC compilers, and on multiple platforms.
It can be easily integrated into any build system, as shown in `platforms`.

## Prerequisites

- [`CMake`](https://cmake.org/download/) v3.11 or newer. [Ninja](https://github.com/ninja-build/ninja/releases) is what CI and the examples below use, but any generator works.
- `A C99 compiler`. Clang, GCC, MSVC, MinGW-w64, TinyCC, Zig, Cosmocc are all supported.
- [`Python 3`](https://www.python.org/downloads/) to run tests and extra tools.

To run `extra/check.py` you'll need:

```sh
pip install codespell pyyaml clang-format==23.1.0 black==26.1.0
```

## Build on Linux, OS X

### Clang

```sh
mkdir build
cd build
cmake -GNinja -DCLANG=1 ..
ninja
```

`CLANG_SUFFIX` picks one of several versions installed side by side - it is appended to
`clang` and `clang++`, so `-DCLANG_SUFFIX="-18"` builds with `clang-18`.

### GCC

```sh
mkdir build
cd build
cmake -GNinja ..
ninja
```

### TinyCC

TinyCC ships no `stdatomic.h`, which libuv needs, so uvwasi is out; build the
simple WASI implementation instead:

```sh
mkdir build
cd build
CC=tcc cmake -GNinja -DBUILD_WASI=simple ..
ninja
```

## Build on Windows

Prerequisites, on top of the ones above:
- [Build Tools for Visual Studio](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2026), with the *Desktop development with C++* workload.
- Both `CMake` and `Ninja` also ship with the Build Tools.
- Select optional *C++ Clang tools for Windows* component for `-T ClangCL` toolset below to work; a [standalone LLVM](https://github.com/llvm/llvm-project/releases) works too (using `-DCLANG_CL=1`).

Use `vswhere` to find out where the latest Build Tools are located:

```bat
"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
```

MSBuild finds the toolchain by itself. A developer environment is only needed to call
the compiler directly, as in [Build using compiler directly](#build-using-compiler-directly),
or to drive Ninja:

```bat
"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
```

### Build with MSBuild

The Visual Studio generator is multi-config: the configuration is chosen at build time,
not at configure time, and the binary lands in a subdirectory named after it.

```bat
mkdir build
cd build

:: Configure Clang, x64
cmake -G"Visual Studio 18 2026" -A x64 -T ClangCL ..

:: Configure Clang, x86
cmake -G"Visual Studio 18 2026" -A Win32 -T ClangCL ..

:: Configure MSVC, x64
cmake -G"Visual Studio 18 2026" -A x64 ..

:: Configure MSVC, x86
cmake -G"Visual Studio 18 2026" -A Win32 ..

:: Configure MSVC, ARM64
cmake -G"Visual Studio 18 2026" -A ARM64 ..

:: Build
cmake --build . --config Release
copy .\Release\wasm3.exe .\
```

The copy is what makes the test runners' default `--exec ../build/wasm3` find the
binary; CI does the same.

### Build with Ninja

Ninja is single-config, and unlike MSBuild it does not set the toolchain up, so this one
runs from a developer environment - `vcvars64.bat` above - with the Build Tools' own
CMake, Ninja and LLVM on `PATH`:

```bat
set VS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools
set PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%
set PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
set PATH=%VS%\VC\Tools\Llvm\x64\bin;%PATH%

:: Clang
cmake -GNinja -DCLANG_CL=1 ..
ninja

:: MSVC
cmake -GNinja ..
ninja
```

## Build in WSL

WSL gives a Windows host the Linux toolchain, after which the
[Linux instructions](#build-on-linux-os-x) apply unchanged.
Prefer using the WSL filesystem, not `/mnt/c`.

### Driving build from a Windows shell

`wsl -e bash -lc '<command>'` runs one command; put
anything multi-line in a script file rather than fighting two layers of quoting. Git Bash
also rewrites arguments that look like Unix paths before WSL ever sees them, so
`MSYS_NO_PATHCONV=1` is what makes a `/mnt/...` argument arrive intact:

```console
$ wsl -e bash -lc 'echo $1' _ /mnt/c/Users
C:/Program Files/Git/mnt/c/Users

$ MSYS_NO_PATHCONV=1 wsl -e bash -lc 'echo $1' _ /mnt/c/Users
/mnt/c/Users
```

### Cosmopolitan binaries do not run under WSL

WSL registers a binfmt handler on the `MZ`
magic - `/proc/sys/fs/binfmt_misc/WSLInterop`, `WSLInterop-late` on newer builds - so
that Windows executables launch from the Linux shell. An APE file starts with `MZ` too,
so `wasm3-cosmopolitan.com` is handed to the Windows loader and answers `APE is running
on WIN32 inside WSL`. Run it from Windows, or convert it in place first with the
Cosmopolitan toolchain's own `assimilate`.

## Build using compiler directly

This can be useful for cross-compilation, quick builds or when a build system (CMake, Ninja, etc.) is not available.

### gcc/clang
```sh
gcc -O3 -g0 -s -Isource -Dd_m3HasWASI source/*.c platforms/app/main.c -lm -o wasm3
```

### msvc/clang-cl
```sh
cl source/*.c platforms/app/main.c /Isource /std:c11 /MD /Ox /Oy /Gw /GS- /W0 /Dd_m3HasWASI /Fewasm3.exe /link advapi32.lib
```

### mingw-w64
```sh
x86_64-w64-mingw32-gcc -O3 -g0 -s -Isource -Dd_m3HasWASI source/*.c platforms/app/main.c -lm -lpthread -static -o wasm3.exe
```

## Build for microcontrollers

In `./platforms/` folder you can find projects for different targets. Some of them are using Platformio, so you can follow the regular pio build process. Others have custom instructions in respective `README.md` files.

## Build for WebAssembly

Wasm3 runs on Wasm too. Build it with the [WASI SDK](https://github.com/WebAssembly/wasi-sdk):

```sh
export WASI_SDK_PATH=/opt/wasi-sdk

mkdir build-wasi
cd build-wasi
cmake -DCMAKE_TOOLCHAIN_FILE="$WASI_SDK_PATH/share/cmake/wasi-sdk-p1.cmake" \
      -DWASI_SDK_PREFIX="$WASI_SDK_PATH" ..
cmake --build .
```

`WASI_SDK_PREFIX` is what the build keys off to produce `wasm3.wasm` against the
`metawasi` implementation, which passes the guest's WASI calls out to the host's. The
toolchain file alone is not enough: CMake only reads it at `project()`, by which point
those choices have been made.

The result needs a host with the tail-call proposal - Wasmtime 14+, Node 22+, or
Wasm3 0.9.0+ - because the interpreter dispatches through `return_call`:

```sh
wasmtime run --dir ./::./ wasm3.wasm hello.wasm
```

The feature set is fixed in `CMakeLists.txt` rather than left to the caller. To try
another one, add it to `CFLAGS` at configure time; `llc -march=wasm32 -mattr=help` lists
what the toolchain understands, and the modules under `test/wasi` show how to pick a
whole set at once with `-mcpu`.

## Build with Zig

Grab the latest [Zig toolchain](https://ziglang.org/download), and then simply run:

```sh
zig build
```

This will install `wasm3` compiled for your target architecture in `zig-out/bin/wasm3`.

The build mode comes from Zig's standard option, so anything but the default Debug is requested as:

```sh
zig build -Doptimize=ReleaseFast
```

To build only the static library and skip the CLI:

```sh
zig build -Dlibm3
```

If you want to cross-compile to some specific target, pass in the target with a flag like so:

```sh
zig build -Dtarget=wasm32-wasi
```

Or if targeting Apple Silicon (this works from *any* host with Zig):

```sh
zig build -Dtarget=aarch64-macos
```

## Build options worth knowing

Every option lives in [`source/m3_config.h`](../source/m3_config.h) with a comment
saying what it costs, and is set the usual way:

```sh
cmake -DCMAKE_C_FLAGS="-Dd_m3DeterministicProfile=1" ..
```

`d_m3GuardedMemory` backs each linear memory with 8GiB of reserved address space and
commits only the part the memory has, so that an access past the end faults and the
fault becomes the trap - which takes the bounds check off every load and store. It
needs a 64-bit host with a MMU.

`d_m3DeterministicProfile` builds the WebAssembly deterministic profile, so that the
same module fed the same input produces the same run anywhere: NaN results are
normalized, and the WASI clock, entropy and waiting come from the runtime rather than
the machine. See [Deterministic execution](./Cookbook.md#deterministic-execution).

`d_m3HasThreadStackProbe` asks the OS how much stack the calling thread has, so
that `d_m3MaxNativeStack` can be cut down to what is really there instead of trusting
a compile-time guess.

`d_m3HasAtomics` implements the atomic instructions of the threads proposal and the
shared flag on a memory's limits. Without `d_m3HasThreads` everything runs on one thread, so
an atomic access is an ordinary one that also checks its alignment, and `memory.atomic.wait`
can only time out: one with no timeout, which nothing could ever end, traps with `wait would
block forever`.

`d_m3HasThreads` lets runtimes on threads of their own share a memory. It is on wherever the
host has threads (`m3_host_posix.h`, `m3_host_win32.h`) and needs `d_m3HasAtomics`. Wasm3
does not start the threads: the embedder does, and gives each one a runtime of its own,
because a runtime holds one value stack and compiles lazily, so it is used by one thread at
a time. What they share is a memory that a module declares `shared`. `m3_ShareModule`
(`extensions/wasm3_ext.h`) makes, in another runtime, a module that exports a view of each
shared memory of the first, and a module in that runtime imports it like any other.

- An `M3Environment` belongs to one thread as well, so each runtime that is parsed into on
  its own thread needs an environment of its own.
- A shared memory never moves. Its declared maximum is reserved up front - a slot of the
  guarded arena, or one allocation - and growing makes more of it reachable and tells every
  view. What is reserved is as much of the maximum as the runtime's limits, the address space
  and the allocator allow, and never less than the initial size, which a memory cannot be
  instantiated without; a `memory.grow` past what was reserved fails, as the spec lets any
  grow do. Without guarded memory a refused allocation is retried at half the size. The
  reservation is charged to the runtime that created it, once, and freed when the last view
  goes.
- Atomic accesses are sequentially consistent, and a read-modify-write is a compare-and-swap
  loop: on hosts of either byte order, at every width, on a 32-bit target as much as on a
  64-bit one, with `-latomic` where the compiler wants it for 64-bit cells.
- `memory.atomic.wait` on a shared memory blocks the calling thread until another runtime
  notifies it, or its timeout passes. It compares the cell under the memory's lock, so a
  notify cannot slip in between the comparison and the queueing, and it queues in one FIFO
  per memory that `memory.atomic.notify` walks oldest first. A wait with no timeout on a
  memory nobody will notify blocks for good, as it does anywhere else. Time is counted on a
  clock that does not jump, and a wakeup that was not a notify goes back to sleep for what is
  left. A runtime has one wait record, made when it first waits and freed with it.
- Every access reads the memory's length with a relaxed atomic load, and reaches the bytes
  through a pointer in the runtime's header for that memory instead of at a fixed offset from
  it. Without `d_m3HasThreads` neither costs anything.
