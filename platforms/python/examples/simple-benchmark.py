#!/usr/bin/env python3

import wasm3
import time, timeit

# WebAssembly binary
WASM = bytes.fromhex("""
  00 61 73 6d 01 00 00 00 01 06 01 60 01 7e 01 7e
  03 02 01 00 07 07 01 03 66 69 62 00 00 0a 1f 01
  1d 00 20 00 42 02 54 04 40 20 00 0f 0b 20 00 42
  02 7d 10 00 20 00 42 01 7d 10 00 7c 0f 0b
""")

def run_wasm():
    env = wasm3.Environment()
    rt  = env.new_runtime(4096)
    mod = env.parse_module(WASM)
    rt.load(mod)
    wasm_fib = rt.find_function("fib")
    assert wasm_fib.call_argv("24") == 46368

def fib(n):
    if n < 2:
        return n
    return fib(n-1) + fib(n-2)

def run_py():
    assert fib(24) == 46368

print("Wasm3:  ", timeit.timeit(run_wasm, number=1000))
time.sleep(15)
print("Python: ", timeit.timeit(run_py,   number=1000))
