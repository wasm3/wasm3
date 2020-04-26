#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy
# Usage:
#   ./run-wasi-test.py
#   ./run-wasi-test.py --exec ../custom_build/wasm3 --timeout 120
#   ./run-wasi-test.py --exec "wasmer run --dir=."
#

import argparse
import sys
import subprocess
import hashlib
import fnmatch

sys.path.append('../extra')

from testutils import *
from pprint import pprint

#
# Args handling
#

parser = argparse.ArgumentParser()
parser.add_argument("--exec", metavar="<interpreter>", default="../build/wasm3")
parser.add_argument("--timeout", type=int,             default=120)
parser.add_argument("--fast",    action='store_true')

args = parser.parse_args()

stats = dotdict(total_run=0, failed=0, crashed=0, timeout=0)

commands_full = [
  {
    "name":           "Simple WASI test",
    "wasm":           "./wasi/test.wasm",
    "args":           ["cat", "/wasi/0.txt"],
    "expect_pattern": "Hello world*Constructor OK*Args: *; cat; /wasi/0.txt;*fib(20) = 6765*[* ms]*48 65 6c 6c 6f 20 77 6f 72 6c 64*=== done ===*"
  }, {
    "name":           "Simple WASI test (wasm-opt -O3)",
    "wasm":           "./wasi/test-opt.wasm",
    "args":           ["cat", "./wasi/0.txt"],
    "expect_pattern": "Hello world*Constructor OK*Args: *; cat; ./wasi/0.txt;*fib(20) = 6765*[* ms]*48 65 6c 6c 6f 20 77 6f 72 6c 64*=== done ===*"
  }, {
    "skip":           True,  # Direct native calls were removed from wasm3
    "name":           "Raw/Native funcs benchmark",
    "wasm":           "./wasi/test_native_vs_raw.wasm",
    "expect_pattern": "Validation...*Native/Raw: *"
  }, {
    "name":           "mandelbrot",
    "wasm":           "./benchmark/mandelbrot/mandel.wasm",
    "args":           ["128", "4e5"],
    "expect_sha1":    "37091e7ce96adeea88f079ad95d239a651308a56"
  }, {
    "name":           "mandelbrot (doubledouble)",
    "wasm":           "./benchmark/mandelbrot/mandel_dd.wasm",
    "args":           ["128", "4e5"],
    "expect_sha1":    "b3f904daf1c972b4f7d3f8996743cb5b5146b877"
  }, {
    "name":           "C-Ray",
    "stdin":          "./benchmark/c-ray/scene",
    "wasm":           "./benchmark/c-ray/c-ray.wasm",
    "args":           ["-s", "128x128"],
    "expect_sha1":    "90f86845ae227466a06ea8db06e753af4838f2fa"
  }, {
    "name":           "smallpt (explicit light sampling)",
    "wasm":           "./benchmark/smallpt/smallpt-ex.wasm",
    "args":           ["16", "64"],
    "expect_sha1":    "d85df3561eb15f6f0e6f20d5640e8e1306222c6d"
  }, {
    "name":           "STREAM",
    "wasm":           "./benchmark/stream/stream.wasm",
    "expect_pattern": "----*Solution Validates:*on all three arrays*----*"
  }, {
    # TODO "if":             { "file_exists": "./self-hosting/wasm3-fib.wasm" },
    "name":           "Self-hosting",
    "wasm":           "./self-hosting/wasm3-fib.wasm",
    "expect_pattern": "wasm3 on WASM*Result: 832040*Elapsed: * ms*"
  }, {
    "name":           "Brotli",
    "stdin":          "./benchmark/brotli/alice29.txt",
    "wasm":           "./benchmark/brotli/brotli.wasm",
    "args":           ["-c"],
    "expect_sha1":    "8eacda4b80fc816cad185330caa7556e19643dff"
  }, {
    "name":           "CoreMark",
    "wasm":           "./benchmark/coremark/coremark-wasi.wasm",
    "expect_pattern": "*Correct operation validated.*CoreMark 1.0 : * / Clang* / HEAP*"
  }
]

commands_fast = [
  {
    "name":           "Simple WASI test",
    "wasm":           "./wasi/test.wasm",
    "args":           ["cat", "./wasi/0.txt"],
    "expect_pattern": "Hello world*Constructor OK*Args: *; cat; ./wasi/0.txt;*fib(20) = 6765*[* ms]*48 65 6c 6c 6f 20 77 6f 72 6c 64*=== done ===*"
  }, {
    "name":           "mandelbrot",
    "wasm":           "./benchmark/mandelbrot/mandel.wasm",
    "args":           ["32", "4e5"],
    "expect_sha1":    "1fdb7dea7ec0f2465054cc623dc5a7225a876361"
  }, {
    "name":           "mandelbrot (doubledouble)",
    "wasm":           "./benchmark/mandelbrot/mandel_dd.wasm",
    "args":           ["32", "4e5"],
    "expect_sha1":    "b6d3c158a5c0dff1f6e82a3556c071e4f8b9e3f0"
  }, {
    "name":           "C-Ray",
    "stdin":          "./benchmark/c-ray/scene",
    "wasm":           "./benchmark/c-ray/c-ray.wasm",
    "args":           ["-s", "32x32"],
    "expect_sha1":    "05af9604bf352234276e4d64e84b8d666574316c"
  }, {
    "name":           "smallpt (explicit light sampling)",
    "wasm":           "./benchmark/smallpt/smallpt-ex.wasm",
    "args":           ["4", "32"],
    "expect_sha1":    "ea05d85998b2f453b588ef76a1256215bf9b851c"
  }, {
    "name":           "Brotli",
    "stdin":          "./benchmark/brotli/alice29_small.txt",
    "wasm":           "./benchmark/brotli/brotli.wasm",
    "args":           ["-c"],
    "expect_sha1":    "0e8af02a7207c0c617d7d38eed92853c4a619987"
  }
]

def fail(msg):
    print(f"{ansi.FAIL}FAIL:{ansi.ENDC} {msg}")
    stats.failed += 1


args_sep = None
if "wasmer" in args.exec or "wasmtime" in args.exec:
    args_sep = "--"

commands = commands_fast if args.fast else commands_full

for cmd in commands:
    if "skip" in cmd:
        continue

    command = args.exec.split(' ')
    command.append(cmd['wasm'])
    if "args" in cmd:
        if args_sep:
            command.append(args_sep)
        command.extend(cmd['args'])

    command = list(map(str, command))
    print(f"=== {cmd['name']} ===")
    stats.total_run += 1
    try:
        if "stdin" in cmd:
            fn = cmd['stdin']
            f = open(fn, "rb")
            print(f"cat {fn} | {' '.join(command)}")
            output = subprocess.check_output(command, timeout=args.timeout, stdin=f)
        else:
            print(f"{' '.join(command)}")
            output = subprocess.check_output(command, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        stats.timeout += 1
        fail("Timeout")
        continue
    except subprocess.CalledProcessError:
        stats.crashed += 1
        fail("Crashed")
        continue

    if "expect_sha1" in cmd:
        actual = hashlib.sha1(output).hexdigest()
        if actual != cmd['expect_sha1']:
            fail(f"Actual sha1: {actual}")

    if "expect_pattern" in cmd:
        if not fnmatch.fnmatch(output.decode("utf-8"), cmd['expect_pattern']):
            fail(f"Output does not match pattern")

    print()

pprint(stats)

if stats.failed:
    print(f"{ansi.FAIL}=======================")
    print(f" FAILED: {stats.failed}/{stats.total_run}")
    print(f"======================={ansi.ENDC}")
    sys.exit(1)

else:
    print(f"{ansi.OKGREEN}=======================")
    print(f" All {stats.total_run} tests OK")
    print(f"======================={ansi.ENDC}")
