# Wasm3 cookbook

### Rust WASI app

Create a new project:
```sh
$ cargo new --bin hello
$ cd hello
$ rustup target add wasm32-wasi
```

Build and run:
```sh
$ cargo build --release --target wasm32-wasi

$ wasm3 ./target/wasm32-wasi/release/hello.wasm
Hello, world!
```

### AssemblyScript WASI app

Create `package.json`:
```json
{
  "name": "hello",
  "version": "1.0.0",
  "description": "Hello world with AssemblyScript and as-wasi",
  "main": "hello.ts",
  "scripts": {
    "build": "asc hello.ts -b hello.wasm"
  },
  "devDependencies": {
    "assemblyscript": "^0.10.0",
    "as-wasi": "^0.1.1"
  }
}
```

Build and run:
```sh
$ npm install
$ npm run build

$ wasm3 hello.wasm
Hello World!
```

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

Build and run:
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

Build and run:
```sh
$ zig build-lib add.zig -O ReleaseSmall -target wasm32-freestanding

$ wasm3 --repl add.wasm
wasm3> add 1 2
Result: 3
```

### C/C++ WASI app

The easiest way to start is to use [Wasienv](https://github.com/wasienv/wasienv).

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

Build and run:
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
