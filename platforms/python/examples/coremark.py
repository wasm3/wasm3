#!/usr/bin/env python3

import wasm3
import os, time

scriptpath = os.path.dirname(os.path.realpath(__file__))
wasm_fn = os.path.join(scriptpath, "./wasm/coremark-minimal.wasm")

print("Initializing Wasm3 engine...")

def clock_ms():
    return int(round(time.time() * 1000))

env = wasm3.Environment()
rt = env.new_runtime(4096)

with open(wasm_fn, "rb") as f:
    mod = env.parse_module(f.read())
    rt.load(mod)
    mod.link_function("env", "clock_ms",    "i()",  clock_ms)

# Gas metering will only apply to metered (pre-instrumented) modules
mod.gasLimit = 200_000_000

wasm_run = rt.find_function("run")

print("Running CoreMark 1.0...")
res = wasm_run()

if res > 1:
    print(f"Result: {res:.3f}")
else:
    print("Error")

if mod.gasUsed:
    print(f"Gas used: {mod.gasUsed}")

