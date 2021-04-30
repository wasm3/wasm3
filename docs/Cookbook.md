# Wasm3 cookbook

### Rust WASI app

TODO

### AssemblyScript WASI app

TODO

### TinyGo WASI app

TODO

### Zig WASI app

Create `hello.zig`:
```zig
const std = @import("std");

pub fn main() !void {
    const stdout = std.io.getStdOut().writer();
    try stdout.print("Hello, {s}!\n", .{"world"});
}
```

```sh
$ zig build-exe -O ReleaseSmall -target wasm32-wasi hello.zig

$ wasm3 hello.wasm
Hello, world!
```

## Zig library

Create `add.zig`:
```zig
export fn add(a: i32, b: i32) i64 {
    return a + b;
}
```

```sh
$ zig build-lib add.zig -O ReleaseSmall -target wasm32-freestanding

$ wasm3 --repl add.wasm
wasm3> add 1 2
Result: 3
```

### C WASI app

The easiest way to start is to use [Wasienv](https://github.com/wasienv/wasienv).

### C/C++ WASI app

Create `hello.cpp`:
```cpp
#include <iostream>
int main() {
    std::cout << "Hello, world!" << std::endl;
    return 0;
}
```

Or `hello.c`:
```c
#include <stdio.h>
int main() {
   printf("Hello, %s!\n", "world");
   return 0;
}
```

```sh
$ wasic++ -O3 hello.cpp -o hello.wasm
$ wasicc -O3 hello.c -o hello.wasm

$ wasm3 hello.wasm
Hello World!
```

Limitations:
- `setjmp/longjmp` and `C++ exceptions` are not available
- no support for `threads` and `atomics`
- no support for `dynamic libraries`
