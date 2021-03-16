#!/usr/bin/env python3

import wasm3
import struct
import asyncio

"""
This is a straightforward translation of JavaScript example from:
  https://kripken.github.io/blog/wasm/2019/07/16/asyncify.html

Input module:
  (module
      (import "env" "before" (func $before))
      (import "env" "sleep" (func $sleep (param i32)))
      (import "env" "after" (func $after))
      (export "memory" (memory 0))
      (export "main" (func $main))
      (func $main
          (call $before)
          (call $sleep (i32.const 2000))
          (call $after)
      )
      (memory 1 1)
  )

Asyncify command:
  wasm-opt async.wasm -O3 --asyncify -o asyncified.wasm
"""

WASM = bytes.fromhex(
    "00 61 73 6d 01 00 00 00 01 0c 03 60 00 00 60 01 7f 00 60 00 01 7f 02 26 03 03 65 6e 76 06 62 65"
    "66 6f 72 65 00 00 03 65 6e 76 05 73 6c 65 65 70 00 01 03 65 6e 76 05 61 66 74 65 72 00 00 03 07"
    "06 00 01 00 01 00 02 05 04 01 01 01 01 06 0b 02 7f 01 41 00 0b 7f 01 41 00 0b 07 84 01 07 06 6d"
    "65 6d 6f 72 79 02 00 04 6d 61 69 6e 00 03 15 61 73 79 6e 63 69 66 79 5f 73 74 61 72 74 5f 75 6e"
    "77 69 6e 64 00 04 14 61 73 79 6e 63 69 66 79 5f 73 74 6f 70 5f 75 6e 77 69 6e 64 00 05 15 61 73"
    "79 6e 63 69 66 79 5f 73 74 61 72 74 5f 72 65 77 69 6e 64 00 06 14 61 73 79 6e 63 69 66 79 5f 73"
    "74 6f 70 5f 72 65 77 69 6e 64 00 07 12 61 73 79 6e 63 69 66 79 5f 67 65 74 5f 73 74 61 74 65 00"
    "08 0a f7 01 06 8f 01 01 01 7f 02 7f 02 7f 23 00 41 02 46 04 40 23 01 23 01 28 02 00 41 04 6b 36"
    "02 00 23 01 28 02 00 28 02 00 21 00 0b 20 00 45 0b 41 01 23 00 1b 04 40 10 00 41 00 23 00 41 01"
    "46 0d 01 1a 0b 20 00 41 01 46 41 01 23 00 1b 04 40 41 d0 0f 10 01 41 01 23 00 41 01 46 0d 01 1a"
    "0b 20 00 41 02 46 41 01 23 00 1b 04 40 10 02 41 02 23 00 41 01 46 0d 01 1a 0b 0f 0b 21 00 23 01"
    "28 02 00 20 00 36 02 00 23 01 23 01 28 02 00 41 04 6a 36 02 00 0b 19 00 41 01 24 00 20 00 24 01"
    "23 01 28 02 00 23 01 28 02 04 4b 04 40 00 0b 0b 15 00 41 00 24 00 23 01 28 02 00 23 01 28 02 04"
    "4b 04 40 00 0b 0b 19 00 41 02 24 00 20 00 24 01 23 01 28 02 00 23 01 28 02 04 4b 04 40 00 0b 0b"
    "15 00 41 00 24 00 23 01 28 02 00 23 01 28 02 04 4b 04 40 00 0b 0b 04 00 23 00 0b")

# Init asyncio

loop = asyncio.get_event_loop()

def set_timeout(ms):
    def decorator(func):
        def wrapper(*args, **kwargs):
            return func(*args, **kwargs)
        loop.call_later(ms/1000, func)
        return wrapper
    return decorator


# Prepare Wasm3 engine

env = wasm3.Environment()
rt  = env.new_runtime(1024)
mod = env.parse_module(WASM)
rt.load(mod)
mem = rt.get_memory(0)

# ------------------------------------------------
def env_before():
    print("before!")

    @set_timeout(1000)
    def callback():
        print("(an event that happens during the sleep)")

# ------------------------------------------------
def env_sleep(ms):
    global sleeping
    if not sleeping:
        print(f"sleep...")
        DATA_ADDR = 16
        mem[DATA_ADDR:DATA_ADDR+8] = struct.pack("<II", DATA_ADDR+8, 1024)
        asyncify_start_unwind(DATA_ADDR)
        sleeping = True

        @set_timeout(ms)
        def callback():
            print("timeout ended, starting to rewind the stack")
            asyncify_start_rewind(DATA_ADDR)
            main()
    else:
        print("...resume")
        asyncify_stop_rewind()
        sleeping = False

# ------------------------------------------------
def env_after():
    print("after!")
    loop.stop()

mod.link_function("env", "before",  "v()",  env_before)
mod.link_function("env", "sleep",   "v(i)", env_sleep)
mod.link_function("env", "after",   "v()",  env_after)

sleeping = False

main = rt.find_function("main")
asyncify_start_unwind   = rt.find_function("asyncify_start_unwind")
asyncify_stop_unwind    = rt.find_function("asyncify_stop_unwind")
asyncify_start_rewind   = rt.find_function("asyncify_start_rewind")
asyncify_stop_rewind    = rt.find_function("asyncify_stop_rewind")

main()
print("stack unwound")
asyncify_stop_unwind()

try:
    loop.run_forever()
finally:
    loop.close()
