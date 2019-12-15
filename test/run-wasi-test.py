#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy

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

def run(cmd):
    return subprocess.check_output(cmd, timeout=args.timeout, shell=True)

#
# Actual test
#

stats = dotdict(total_run=0, failed=0, crashed=0, timeout=0)

commands = [
  {
    "command":        "<wasm3> ./wasi/test.wasm",
    "expect_pattern": "Hello world*Constructor OK*fib(10) = 55*[* ms]*"
  }, {
    "command":        "<wasm3> ./benchmark/coremark/coremark-wasi.wasm",
    "expect_pattern": "*Correct operation validated.*CoreMark 1.0 : * / Clang* / HEAP*"
  }, {
    "command":        "cat ./benchmark/c-ray/scene | <wasm3> ./benchmark/c-ray/c-ray.wasm",
    "expect_sha1":    "0260a0ee271abd447ffa505aecbf745cb7399e2c"
  }, {
    "command":        "<wasm3> ./benchmark/stream/stream.wasm",
    "expect_pattern": "----*Solution Validates:*on all three arrays*----*"
  }
]

def fail(msg):
    print(f"{ansi.FAIL}FAIL:{ansi.ENDC} {msg}")
    stats.failed += 1

for cmd in commands:
    command = cmd['command'].replace("<wasm3>", args.exec)
    print(f"Running {command}")
    stats.total_run += 1;
    try:
        output = run(command)
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
