#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy
# Usage:
#   ./run-wasi-test.py
#   ./run-wasi-test.py --exec ../custom_build/wasm3 --timeout 120
#   ./run-wasi-test.py --exec "wasmer run"
#

# TODO
# - Implement wasi args passing => reduce test time & cpu usage
# - Fix "Empty stack" output, so it does not affect the SHA1 checksum

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
    "expect_pattern": "Hello world*Constructor OK*fib(10) = 55*[* ms]*"
  }, {
    "name":           "CoreMark",
    "wasm":           "./benchmark/coremark/coremark-wasi.wasm",
    "expect_pattern": "*Correct operation validated.*CoreMark 1.0 : * / Clang* / HEAP*"
  }, {
    "name":           "C-Ray",
    "stdin":          open("./benchmark/c-ray/scene", "rb"),
    "wasm":           "./benchmark/c-ray/c-ray.wasm",
    "expect_sha1":    "0260a0ee271abd447ffa505aecbf745cb7399e2c" # TODO: should be af7baced15a066eb83150aceaea0add05e0c7edf
  }, {
    "skip":           True,  # TODO
    "name":           "smallpt (explicit light sampling)",
    "wasm":           "./benchmark/smallpt/smallpt-ex.wasm",
    "expect_sha1":    "ad8e505dc15564a86cdbdbffc48682b2a923f37c"
  }, {
    "name":           "STREAM",
    "wasm":           "./benchmark/stream/stream.wasm",
    "expect_pattern": "----*Solution Validates:*on all three arrays*----*"
  }, {
    "skip":           True,  # TODO
    "name":           "Self-hosting",
    "wasm":           "./self-hosting/wasm3-fib.wasm",
    "expect_pattern": "*wasm3 on WASM*Elapsed: * ms*"
  }, {
    "skip":           True,  # TODO
    "name":           "Brotli",
    "stdin":          open("./benchmark/brotli/alice29.txt", "rb"),
    "wasm":           "./benchmark/brotli/brotli.wasm",
    "args":           ["-c"],
    "expect_sha1":    "8eacda4b80fc816cad185330caa7556e19643dff"
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
    print(f"Running {cmd['name']}")
    stats.total_run += 1
    try:
        if "stdin" in cmd:
            output = subprocess.check_output(command, timeout=args.timeout, stdin=cmd['stdin'])
        else:
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
