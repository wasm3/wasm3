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

from pprint import pprint

#
# Args handling
#

parser = argparse.ArgumentParser()
parser.add_argument("--exec", metavar="<interpreter>", default="../build/wasm3")
parser.add_argument("--timeout", type=int,             default=30)

args = parser.parse_args()

#
# Helpers
#

class ansi:
    ENDC = '\033[0m'
    HEADER = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

class dotdict(dict):
    def __init__(self, *args, **kwargs):
        super(dotdict, self).__init__(*args, **kwargs)
        for arg in args:
            if isinstance(arg, dict):
                for k, v in arg.items():
                    self[k] = v
        if kwargs:
            for k, v in kwargs.items():
                self[k] = v

    __getattr__ = dict.get
    __setattr__ = dict.__setitem__
    __delattr__ = dict.__delitem__

#
# Actual test
#

stats = dotdict(total_run=0, failed=0, crashed=0, timeout=0)

commands = [
  {
    "name":           "Simple WASI test",
    "wasm":           "./wasi/test.wasm",
    "args":           ["cat", "./wasi/0.txt"],
    "expect_pattern": "Hello world*Constructor OK*Args: *; cat; ./wasi/0.txt;*fib(20) = 6765*[* ms]*=== done ===*"
    #TODO: "expect_pattern": "Hello world*Constructor OK*Args: *; cat; ./wasi/0.txt;*fib(20) = 6765*[* ms]*48 65 6c 6c 6f 20 77 6f 72 6c 64*=== done ===*"
  }, {
    "name":           "Simple WASI test (wasm-opt -O3)",
    "wasm":           "./wasi/test-opt.wasm",
    "args":           ["cat", "./wasi/0.txt"],
    "expect_pattern": "Hello world*Constructor OK*Args: *; cat; ./wasi/0.txt;*fib(20) = 6765*[* ms]*=== done ===*"
    #TODO: "expect_pattern": "Hello world*Constructor OK*Args: *; cat; ./wasi/0.txt;*fib(20) = 6765*[* ms]*48 65 6c 6c 6f 20 77 6f 72 6c 64*=== done ===*"
  }, {
    "name":           "mandelbrot",
    "wasm":           "./benchmark/mandelbrot/mandel.wasm",
    "args":           ["128", "4e5"],
    "expect_sha1":    "2df4c54065e58d3a860fe3b8cc6ad4ffabf6cdaf"
  }, {
    "name":           "mandelbrot (doubledouble)",
    "wasm":           "./benchmark/mandelbrot/mandel_dd.wasm",
    "args":           ["128", "4e5"],
    "expect_sha1":    "dab4899961e2fcfc8691754c2200d64c5c0995e1"
  }, {
    "name":           "C-Ray",
    "stdin":          "./benchmark/c-ray/scene",
    "wasm":           "./benchmark/c-ray/c-ray.wasm",
    "args":           ["-s", "128x128"],
    "expect_sha1":    "90f86845ae227466a06ea8db06e753af4838f2fa"
  }, {
    "skip":           True,  # TODO: Crashes
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
    "args":           ["-9", "-c"],
    "expect_sha1":    "e5f2e4cb0eb0bae1775a7be0795dd2aaf8900f1a"
  }, {
    "name":           "CoreMark",
    "wasm":           "./benchmark/coremark/coremark-wasi.wasm",
    "expect_pattern": "*Correct operation validated.*CoreMark 1.0 : * / Clang* / HEAP*"
  }
]

def fail(msg):
    print(f"{ansi.FAIL}FAIL:{ansi.ENDC} {msg}")
    stats.failed += 1


args_sep = None
if "wasmer" in args.exec or "wasmtime" in args.exec:
    args_sep = "--"

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
