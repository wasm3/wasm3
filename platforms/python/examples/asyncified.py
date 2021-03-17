#!/usr/bin/env python3

import wasm3
import base64, struct
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

# WebAssembly binary
WASM = base64.b64decode("""
    AGFzbQEAAAABDANgAABgAX8AYAABfwImAwNlbnYGYmVmb3JlAAADZW52BXNsZWVwAAEDZW52BWFm
    dGVyAAADBwYAAQABAAIFBAEBAQEGCwJ/AUEAC38BQQALB4QBBwZtZW1vcnkCAARtYWluAAMVYXN5
    bmNpZnlfc3RhcnRfdW53aW5kAAQUYXN5bmNpZnlfc3RvcF91bndpbmQABRVhc3luY2lmeV9zdGFy
    dF9yZXdpbmQABhRhc3luY2lmeV9zdG9wX3Jld2luZAAHEmFzeW5jaWZ5X2dldF9zdGF0ZQAICvcB
    Bo8BAQF/An8CfyMAQQJGBEAjASMBKAIAQXxqNgIAIwEoAgAoAgAhAAsgAEULQQEjABsEQBAAQQAj
    AEEBRg0BGgsgAEEBRkEBIwAbBEBB0A8QAUEBIwBBAUYNARoLIABBAkZBASMAGwRAEAJBAiMAQQFG
    DQEaCw8LIQAjASgCACAANgIAIwEjASgCAEEEajYCAAsZAEEBJAAgACQBIwEoAgAjASgCBEsEQAAL
    CxUAQQAkACMBKAIAIwEoAgRLBEAACwsZAEECJAAgACQBIwEoAgAjASgCBEsEQAALCxUAQQAkACMB
    KAIAIwEoAgRLBEAACwsEACMACw==
""")

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
