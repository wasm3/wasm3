# Wasm3

[Wasm3](https://github.com/wasm3/wasm3) is a high performance `WebAssembly` interpreter written in C.  
It's packaged into a `WebAssembly` package, so you can finally run `WebAssembly` on `WebAssembly` 😆

When running natively, wasm3 is:

**∼ 8x faster** than other known wasm interpreters  
**∼ 4-5x slower** than state of the art wasm `JIT` engines  
**∼ 12x slower** than native execution  

<sub>Based on [**CoreMark 1.0** benchmark](https://github.com/wasm3/wasm3/blob/master/docs/Performance.md). Your mileage may vary.</sub>

## Running on WebAssembly.sh

First you need to make a wasm file you want to run available in the filesystem.  
One way to do this is to get it from a server using `curl` command:

```sh
$ curl https://raw.githubusercontent.com/wasm3/wasm3/master/test/lang/fib32.wasm -o /tmp/fib32.wasm

$ ls -l /tmp/fib32.wasm
---------- 1 somebody somegroup 62 1970-01-19 05:45 /tmp/fib32.wasm
```

Now we can run `wasm3` in interactive mode:

```sh
$ wasm3 --repl /tmp/fib32.wasm
wasm3> fib 20
Result: 6765
wasm3> fib 30
Result: 832040
wasm3> ^C
$
```

Or run a specific function directly:

```sh
$ wasm3 --func fib /tmp/fib32.wasm 30
Result: 832040
```

`wasm3` also supports `WASI`, so you can run:

```sh
$ curl https://raw.githubusercontent.com/wasm3/wasm3/master/test/wasi/simple/test.wasm -o /tmp/test.wasm
$ wasm3 /tmp/test.wasm
```

or even... run wasm3 inside wasm3:

```sh
$ curl https://registry-cdn.wapm.io/contents/vshymanskyy/wasm3/0.4.9/build/wasm3-wasi.wasm -o /tmp/wasm3.wasm
$ wasm3 /tmp/wasm3.wasm /tmp/test.wasm
```

## Tracing

You can also get structured traces of arbitrary WASM file execution (and this requires no specific support from the runtime):

```sh
$ wasm3-strace --repl /tmp/fib32.wasm
wasm3> fib 3
fib (i32: 3) {
  fib (i32: 1) {
  } = 1
  fib (i32: 2) {
    fib (i32: 0) {
    } = 0
    fib (i32: 1) {
    } = 1
  } = 1
} = 2
Result: 2
```

WASI apps tracing is also supported:

```sh
$ wasm3-strace /tmp/test.wasm trap
_start () {
  __wasm_call_ctors () {
    __wasilibc_populate_preopens () {
      wasi_snapshot_preview1!fd_prestat_get(3, 65528) { <native> } = 0
      malloc (i32: 2) {
        dlmalloc (i32: 2) {
          sbrk (i32: 0) {
          } = 131072
        } = 70016
      } = 70016
...
        strcmp (i32: 70127, i32: 32) {
        } = 0
        test_trap () {
          a () {
            b () {
              c () {
              } !trap = [trap] unreachable executed
...
==== wasm backtrace:
  0: 0x000c59 - .unnamed!c
  1: 0x000c5e - .unnamed!b
  2: 0x000c68 - .unnamed!a
  3: 0x000c72 - .unnamed!test_trap
  4: 0x000d2c - .unnamed!main
  5: 0x0037c9 - .unnamed!__main_void
  6: 0x000edb - .unnamed!__original_main
  7: 0x0002f3 - .unnamed!_start
Error: [trap] unreachable executed
```
