#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy
# Usage:
#   ./run-strace-test.py
#   ./run-strace-test.py --exec ../build-strace/wasm3
#   ./run-strace-test.py --exec "wasmtime run --dir ./::./ wasm3-strace.wasm"
#   ./run-strace-test.py --update      # re-record the reference outputs
#
# The strace build (-Dd_m3EnableStrace=2 -Dd_m3RecordBacktraces=1) ships as its
# own release binary and runs the engine differently: op_Entry has to stay live
# across the function body to print the closing brace and to record the frame, so
# d_m3EntryKeepsFrame turns d_m3CanTailCall off. Nothing else in CI exercises it.
#
# Each case runs a module and compares the trace, which wasm3 writes to stderr,
# against a recorded reference. The program's own stdout is not part of the
# comparison: it interleaves with the trace differently depending on how the two
# streams are buffered.

import argparse
import difflib
import os
import subprocess
import sys

sys.path.append("../extra")

from testutils import *

#
# Args handling
#

parser = argparse.ArgumentParser()
parser.add_argument("--exec", metavar="<interpreter>", default="../build/wasm3")
parser.add_argument("--timeout", type=int, default=120)
parser.add_argument(
    "--update",
    action="store_true",
    help="write what the build printed to the reference files instead of "
    "comparing against them",
)
parser.add_argument("-v", "--verbose", action="store_true")

args = parser.parse_args()

stats = SimpleNamespace(total_run=0, failed=0, updated=0)

#
# Test cases
#
# "args" precede the module path, "func_args" follow it - that is where wasm3
# expects the arguments of the function being called. "tail" compares only the
# last N lines, for a program whose own start-up trace depends on the host.
#

# fmt: off
tests = [
  {
    "name":     "nested calls, arguments and results",
    "wasm":     "./lang/fib32.wasm",
    "args":     ["--func", "fib"],
    "func_args": ["6"],
    "expect":   "fib32.txt",
  }, {
    # four arguments, and a function the module does not name
    "name":     "call of an unnamed function",
    "wasm":     "./regression/github-477.wasm",
    "args":     ["--func", "main"],
    "expect":   "unnamed-func.txt",
  }, {
    # the only case that reaches the "<native>" branch: a host function has no
    # wasm body to trace, so op_CallRawFunction prints the call itself
    "name":     "call of an imported host function",
    "wasm":     "./regression/wasi-memory-export-index0.wasm",
    "args":     ["--func", "_start"],
    "expect":   "host-call.txt",
  }, {
    # memory64-bounds traps just as well, but its message carries the offset and
    # the memory size only in a DEBUG build, and the reference should not depend
    # on that
    "name":     "trap, and the frame it was raised in",
    "wasm":     "./regression/table64-bounds.wasm",
    "args":     ["--func", "call_max"],
    "can_crash": True,
    "expect":   "trap.txt",
  }, {
    # the case the backtraces exist for: eight frames deep, unwound one at a
    # time. Everything before the trap is this program starting up, which goes
    # through WASI and so depends on the host it runs on
    "name":     "trap, unwound through a call chain",
    "wasm":     "./wasi/simple/test.wasm",
    "func_args": ["trap"],
    "can_crash": True,
    "tail":     20,
    "expect":   "trap-nested.txt",
  },
]
# fmt: on

refDir = "./strace"


def fail(msg):
    print(f"{ansi.FAIL}FAIL:{ansi.ENDC} {msg}")
    stats.failed += 1


for test in tests:
    command = (
        args.exec.split(" ")
        + test.get("args", [])
        + [test["wasm"]]
        + test.get("func_args", [])
    )
    command = list(map(str, command))

    print(f"=== {test['name']} ===")
    if args.verbose:
        print(" ".join(command))

    stats.total_run += 1

    try:
        p = subprocess.run(
            command,
            timeout=args.timeout,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
    except subprocess.TimeoutExpired:
        fail(f"Timeout after {args.timeout}s")
        continue

    if p.returncode != 0 and not test.get("can_crash"):
        fail(f"Exited with error code {p.returncode}")
        continue

    actual = p.stderr.decode("utf-8", errors="replace").splitlines()
    actual = [line.rstrip() for line in actual]

    tail = test.get("tail")
    if tail:
        actual = actual[-tail:]

    refFile = os.path.join(refDir, test["expect"])

    if args.update:
        ensure_path(refDir)
        with open(refFile, "w", newline="\n") as f:
            f.write("\n".join(actual) + "\n")
        print(f"{ansi.WARNING}UPDATED:{ansi.ENDC} {refFile}")
        stats.updated += 1
        continue

    try:
        with open(refFile) as f:
            expected = [line.rstrip() for line in f.read().splitlines()]
    except FileNotFoundError:
        fail(f"{refFile} is missing. Record it with --update")
        continue

    if not actual:
        fail(
            "printed no trace at all - the build needs -Dd_m3EnableStrace=2 "
            "-Dd_m3RecordBacktraces=1"
        )
    elif actual != expected:
        diff = difflib.unified_diff(
            expected, actual, fromfile=refFile, tofile="actual", lineterm=""
        )
        fail("Trace does not match the reference:")
        print(f"{' '.join(command)}")
        print("\n".join(diff))
    else:
        if args.verbose:
            print("\n".join(actual))
        print(f"{ansi.OKGREEN}OK{ansi.ENDC} ({len(actual)} lines)")

    print()

print_stats(stats)

if stats.failed:
    print(f"{ansi.FAIL}=======================")
    print(f" FAILED: {stats.failed}/{stats.total_run}")
    print(f"======================={ansi.ENDC}")
    sys.exit(1)
else:
    print(f"{ansi.OKGREEN}=======================")
    print(f" All {stats.total_run} tests OK")
    print(f"======================={ansi.ENDC}")
