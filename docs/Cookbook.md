# WASM module examples

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

Create `hello.ts`:
```ts
import "wasi"
import {Console} from "as-wasi"

Console.log('Hello World!');
```

Create `package.json`:
```json
{
  "scripts": {
    "asbuild:debug":        "asc hello.ts -b hello.wasm --debug",
    "asbuild:optimized":    "asc hello.ts -b hello.wasm -O3s --noAssert",
    "asbuild:tiny":         "asc hello.ts -b hello.wasm -O3z --noAssert --runtime stub --use abort=",
    "build":                "npm run asbuild:optimized"
  },
  "devDependencies": {
    "assemblyscript": "*",
    "as-wasi": "*"
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

Create `hello.go`:
```go
package main

import "fmt"

func main() {
    fmt.Printf("Hello, %s!\n", "world")
}
```

Build and run:
```sh
$ tinygo build -o hello.wasm -target wasi -no-debug hello.go

$ wasm3 hello.wasm
Hello, world!
```


### Go WASI app

Go currently does not provide the WASI interface.  
For reference see [this issue](https://github.com/golang/go/issues/31105).


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

### Zig C-code WASI app

Create `hello.c`:
```c
#include <stdio.h>

int main() {
   printf("Hello, %s!\n", "world");
   return 0;
}
```

Build and run:
```sh
$ zig build-exe -O ReleaseSmall -target wasm32-wasi hello.c -lc

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

Useful `clang` options:

- **-nostdlib** Do not use the standard system startup files or libraries when linking

- **-Wl,--no-entry** Do not output any entry point

- **-Wl,--export=\<value\>** Force a symbol to be exported, e.g. **-Wl,--export=foo** to export foo function

- **-Wl,--export-all** Export all symbols (normally combined with --no-gc-sections)

- **-Wl,--initial-memory=\<value\>** Initial size of the linear memory, which must be a multiple of 65536

- **-Wl,--max-memory=\<value\>** Maximum size of the linear memory, which must be a multiple of 65536

- **-z stack-size=\<vlaue\>** The auxiliary stack size, which is an area of linear memory, and must be smaller than initial memory size.

- **-Wl,--strip-all** Strip all symbols

- **-Wl,--shared-memory** Use shared linear memory

- **-Wl,--allow-undefined** Allow undefined symbols in linked binary

- **-Wl,--allow-undefined-file=\<value\>** Allow symbols listed in \<file\> to be undefined in linked binary

- **-pthread** Support POSIX threads in generated code

Limitations:
- `setjmp/longjmp` and `C++ exceptions` are not available
- no support for `threads` and `atomics`
- no support for `dynamic libraries`

### Grain WASI app

Create `hello.gr`:
```
print("Hello, world!")
```

Build and run:
```sh
$ grain compile hello.gr -o hello.wasm

$ wasm3 hello.wasm
Hello, world!
```

### WAT WASI app

Create `hello.wat`:
```wat
(module
    ;; wasi_snapshot_preview1!fd_write(file_descriptor, *iovs, iovs_len, nwritten) -> status_code
    (import "wasi_snapshot_preview1" "fd_write" (func $fd_write (param i32 i32 i32 i32) (result i32)))

    (memory 1)
    (export "memory" (memory 0))

    ;; Put a message to linear memory at offset 32
    (data (i32.const 32) "Hello, world!\n")

    (func $main (export "_start")
        ;; Create a new io vector at address 0x4
        (i32.store (i32.const 4) (i32.const 32))  ;; iov.iov_base - pointer to the start of the message
        (i32.store (i32.const 8) (i32.const 14))  ;; iov.iov_len  - length of the message

        (call $fd_write
            (i32.const 1)  ;; file_descriptor - 1 for stdout
            (i32.const 4)  ;; *iovs           - pointer to the io vector
            (i32.const 1)  ;; iovs_len        - count of items in io vector
            (i32.const 20) ;; nwritten        - where to store the number of bytes written
        )
        drop ;; discard the WASI status code
    )
)
```


Build and run:
```sh
$ wat2wasm hello.wat -o hello.wasm

$ wasm3 hello.wasm    
Hello, world!
```

### WAT library

Create `swap.wat`:
```wat
(module
  (func (export "swap") (param i32 i32) (result i32 i32)
    (get_local 1)
    (get_local 0)
  )
)
```

Build and run:
```sh
$ wat2wasm swap.wat -o swap.wasm

$ wasm3 --repl swap.wasm
wasm3> :invoke swap 123 456
Result: 456:i32, 123:i32
```

# Tracing

Drag'n'drop any of the WASI apps to [`WebAssembly.sh`](https://webassembly.sh/) and run:
```sh
$ wasm3-strace /tmp/hello.wasm
```

The execution trace will be produced:
```js
_start () {
  __wasilibc_init_preopen () {
    malloc (i32: 16) {
      dlmalloc (i32: 16) {
        sbrk (i32: 0) {
        } = 131072
        sbrk (i32: 65536) {
```
<details>
  <summary>Click to expand!</summary>

```js
        } = 131072
      } = 131088
    } = 131088
    calloc (i32: 24, i32: 0) {
      dlmalloc (i32: 96) {
      } = 131120
      memset (i32: 131120, i32: 65504, i32: 0) {
      } = 131120
    } = 131120
    po_map_assertvalid (i32: 131088) {
    }
    po_map_assertvalid (i32: 131088) {
    }
  }
  wasi_unstable!fd_prestat_get(3, 65528) { <native> } = 0
  malloc (i32: 2) {
    dlmalloc (i32: 2) {
    } = 131232
  } = 131232
  wasi_unstable!fd_prestat_dir_name(3, 131232, 1) { <native> } = 0
  __wasilibc_register_preopened_fd (i32: 3, i32: 131120) {
    po_map_assertvalid (i32: 131088) {
    }
    po_map_assertvalid (i32: 131088) {
    }
    strdup (i32: 131232) {
      strlen (i32: 131232) {
      } = 1
      malloc (i32: 2) {
        dlmalloc (i32: 2) {
        } = 131248
      } = 131248
      memcpy (i32: 131248, i32: 131233, i32: 131232) {
      } = 131248
    } = 131248
    wasi_unstable!fd_fdstat_get(3, 65496) { <native> } = 0
    po_map_assertvalid (i32: 131088) {
    }
    po_map_assertvalid (i32: 131088) {
    }
  } = 0
  free (i32: 131232) {
    dlfree (i32: 131232) {
    }
  }
  wasi_unstable!fd_prestat_get(4, 65528) { <native> } = 0
  malloc (i32: 2) {
    dlmalloc (i32: 2) {
    } = 131232
  } = 131232
  wasi_unstable!fd_prestat_dir_name(4, 131232, 1) { <native> } = 0
  __wasilibc_register_preopened_fd (i32: 4, i32: 131120) {
    po_map_assertvalid (i32: 131088) {
    }
    po_map_assertvalid (i32: 131088) {
    }
    strdup (i32: 131232) {
      strlen (i32: 131232) {
      } = 1
      malloc (i32: 2) {
        dlmalloc (i32: 2) {
        } = 131264
      } = 131264
      memcpy (i32: 131264, i32: 131233, i32: 131232) {
      } = 131264
    } = 131264
    wasi_unstable!fd_fdstat_get(4, 65496) { <native> } = 0
    po_map_assertvalid (i32: 131088) {
    }
    po_map_assertvalid (i32: 131088) {
    }
  } = 0
  free (i32: 131232) {
    dlfree (i32: 131232) {
    }
  }
  wasi_unstable!fd_prestat_get(5, 65528) { <native> } = 8
  __wasm_call_ctors () {
  }
  __original_main () {
    printf (i32: 65536, i32: 0) {
      vfprintf (i32: 69512, i32: 0, i32: 65536) {
        printf_core (i32: 0, i32: -1, i32: 65536, i32: -16, i32: 65480) {
        } = 0
        __towrite (i32: 69512) {
        } = 0
        printf_core (i32: 69512, i32: 8, i32: 65536, i32: -1, i32: 65480) {
          __fwritex (i32: 65536, i32: 0, i32: 7) {
            memcpy (i32: 68472, i32: 0, i32: 65536) {
            } = 68472
          } = 7
          __fwritex (i32: 65543, i32: 0, i32: 0) {
            memcpy (i32: 68479, i32: 0, i32: 65543) {
            } = 68479
          } = 0
          pop_arg (i32: 64456, i32: 0, i32: 9) {
          }
          strnlen (i32: 65548, i32: 0) {
            memchr (i32: 65548, i32: 4, i32: 0) {
            } = 65553
          } = 5
          __fwritex (i32: 67222, i32: 65553, i32: 0) {
            memcpy (i32: 68479, i32: 0, i32: 67222) {
            } = 68479
          } = 0
          __fwritex (i32: 65548, i32: 65553, i32: 5) {
            memcpy (i32: 68479, i32: 0, i32: 65548) {
            } = 68479
          } = 5
          __fwritex (i32: 65545, i32: 0, i32: 2) {
            __stdout_write (i32: 69512, i32: 0, i32: 65545) {
              __isatty (i32: 1) {
                wasi_unstable!fd_fdstat_get(1, 64376) { <native> } = 0
              } = 1
              __stdio_write (i32: 69512, i32: 64368, i32: 65545) {
                writev (i32: 1, i32: -16, i32: 64384) {
Hello, world!
                  wasi_unstable!fd_write(1, 64384, 2, 64380) { <native> } = 0
                } = 14
              } = 2
            } = 2
            memcpy (i32: 68472, i32: -1, i32: 65547) {
            } = 68472
          } = 2
        } = 14
      } = 14
    } = 14
  } = 0
  __prepare_for_exit () {
    dummy () {
    }
    __stdio_exit () {
      __ofl_lock () {
      } = 69504
    }
  }
}
```
</details>

# Gas Metering

```sh
$ npm install -g wasm-metering

$ wasm-meter hello.wasm hello-metered.wasm

$ wasm3 hello-metered.wasm       
Warning: Gas is limited to 500000000.0000
Hello, world!
Gas used: 20.8950

$ wasm3 --gas-limit 10 hello-metered.wasm
Warning: Gas is limited to 10.0000
Gas used: 10.0309
Error: [trap] Out of gas
```

# Other resources

- [WebAssembly by examples](https://wasmbyexample.dev/home.en-us.html) by Aaron Turner
- [Writing WebAssembly](https://docs.wasmtime.dev/wasm.html) in Wasmtime docs
