#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy
# Usage:
#   ./run-regression-test.py
#   ./run-regression-test.py --exec ../custom_build/wasm3 --timeout 120
#   ./run-regression-test.py --exec "valgrind --error-exitcode=9 ../build/wasm3"
#
# Each case in ./regression/ is a minimized module - from a reported issue, or
# from an engine corner that the spec suite does not reach. The primary
# assertion is always the same: wasm3 must not die on it. A clean trap or a
# clean error message is a pass; a signal, an abort, or a sanitizer report is a
# failure. Cases where the returned value is the point also carry an expected
# output pattern.

import argparse
import os
import signal
import subprocess
import sys
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
parser.add_argument("-v", "--verbose", action='store_true')

args = parser.parse_args()

stats = dotdict(total_run=0, failed=0, crashed=0, timeout=0, known_issues=0)

#
# Test cases
#
# "expect_pattern" is fnmatch syntax, matched against stdout+stderr. Keep '['
# out of it: fnmatch reads it as a character class.
# "args" precede the module path; "func_args" follow it.
#

tests = [
  {
    "name":           "f32.max NaN propagation",
    "issue":          405,
    "wasm":           "./regression/github-405.wasm",
    "args":           ["--func", "to_test"],
    "expect_pattern": "*Result: *nan*",
  }, {
    "name":           "memory.grow by a negative number",
    "issue":          442,
    "wasm":           "./regression/github-442.wasm",
    "args":           ["--func", "to_test"],
    "expect_pattern": "*Result: -1*",
  }, {
    # _start is the WASI entry point, so the app takes a separate path for it.
    # A plain module can still export _start with results, and those used to be
    # dropped instead of printed. 0/0 leaves the NaN sign up to the host, so the
    # sign of the copysign result is not part of the assertion.
    "name":           "_start with a return value",
    "issue":          351,
    "wasm":           "./regression/github-351.wasm",
    "args":           ["--func", "_start"],
    "expect_pattern": "*Result: *0.000000*",
  }, {
    "name":           "Unlinked memory import",
    "issue":          462,
    "wasm":           "./regression/github-462.wasm",
    "args":           ["--func", "_start"],
  }, {
    "name":           "br out of a typed block",
    "issue":          465,
    "wasm":           "./regression/github-465.wasm",
    "args":           ["--func", "_start"],
  }, {
    "name":           "Call with more args than the callee returns",
    "issue":          477,
    "wasm":           "./regression/github-477.wasm",
    "args":           ["--func", "main"],
    "expect_pattern": "*Result: *",
  }, {
    "name":           "Non-numeric function argument",
    "issue":          367,
    "wasm":           "./lang/fib32.wasm",
    "args":           ["--func", "fib"],
    "func_args":      ["abc"],
    "expect_pattern": "*argument is not a number*",
  }, {
    "name":           "Partially numeric function argument",
    "issue":          367,
    "wasm":           "./lang/fib32.wasm",
    "args":           ["--func", "fib"],
    "func_args":      ["12abc"],
    "expect_pattern": "*argument is not a number*",
  }, {
    "name":           "Function argument out of range",
    "issue":          367,
    "wasm":           "./lang/fib32.wasm",
    "args":           ["--func", "fib"],
    "func_args":      ["4294967296"],
    "expect_pattern": "*argument out of range*",
  }, {
    "name":           "Native stack exhaustion",
    "issue":          479,
    "wasm":           "./regression/github-479.wasm",
    "args":           ["--func", "main"],
    "expect_pattern": "*stack overflow*",
  }, {
    # op_TryTable's native frame outlives the try region when control branches
    # past the block's end, so the frame has to notice it is no longer the
    # innermost handler. Returning 1 means it caught what it should have let by.
    "name":           "throw after branching out of a try region",
    "wasm":           "./regression/exceptions-stale-try.wasm",
    "args":           ["--func", "to_test"],
    "expect_pattern": "*uncaught exception*",
  }, {
    "name":           "throw after leaving a try region via br_table",
    "wasm":           "./regression/exceptions-stale-try-br-table.wasm",
    "args":           ["--func", "to_test"],
    "expect_pattern": "*uncaught exception*",
  }, {
    # the catch handler runs on op_TryTable's frame, whose memory pointer is
    # stale if the body grew linear memory
    "name":           "memory.grow inside a try body",
    "wasm":           "./regression/exceptions-grow-in-try.wasm",
    "args":           ["--func", "to_test"],
    "expect_pattern": "*Result: 7*",
  }, {
    # each lap enters a try region; the native frame has to come back off
    "name":           "try region entered once per loop iteration",
    "wasm":           "./regression/exceptions-try-in-loop.wasm",
    "args":           ["--func", "to_test"],
    "expect_pattern": "*Result: 1000000*",
  }, {
    # the memory is real, so the traps below are about the addresses and not
    # about the module failing to load
    "name":           "memory64 in-bounds access",
    "wasm":           "./regression/memory64-bounds.wasm",
    "args":           ["--func", "ok"],
    "expect_pattern": "*Result: 0*",
  }] + [
  {
    # address + offset is added in a u64, which is only safe because an address
    # at or above d_m3AddressLimit is refused first. Each of these would land
    # back inside the memory if the sum were allowed to wrap.
    "name":           f"memory64 effective address does not wrap ({func})",
    "wasm":           "./regression/memory64-bounds.wasm",
    "args":           ["--func", func],
    "expect_pattern": "*out of bounds*",
  } for func in ("wrap_max", "wrap_limit", "at_limit", "below_limit",
                 "far_offset", "wrap_store")
  ] + [
  {
    # start + length has the same room to wrap as a load's address + offset
    "name":           f"memory64 bulk operation does not wrap ({func})",
    "wasm":           "./regression/memory64-bulk-wrap.wasm",
    "args":           ["--func", func],
    "expect_pattern": "*out of bounds*",
  } for func in ("fill_wrap", "fill_at_max", "copy_wrap", "copy_at_max")
  ] + [
  {
    # the checked address goes to a scratch slot; writing it back over a
    # constant operand would corrupt the function's constant table
    "name":           "memory64 address check leaves its operand alone",
    "wasm":           "./regression/memory64-const-addr.wasm",
    "args":           ["--func", "to_test"],
    "expect_pattern": "*Result: 135*",
  }, {
    "name":           "memory64 failed memory.grow answers 2^64-1",
    "wasm":           "./regression/memory64-grow.wasm",
    "args":           ["--func", "to_test"],
    "expect_pattern": "*Result: 1*",
  }, {
    "name":           "table64 in-bounds call_indirect",
    "wasm":           "./regression/table64-bounds.wasm",
    "args":           ["--func", "ok"],
    "expect_pattern": "*Result: 5*",
  }] + [
  {
    # a table64 index is a whole u64, and a bulk length added to a start still
    # has room to wrap even though table sizes stay far below 2^32
    "name":           f"table64 index does not wrap ({func})",
    "wasm":           "./regression/table64-bounds.wasm",
    "args":           ["--func", func],
    "expect_pattern": "*trap*",
  } for func in ("call_max", "call_past_u32", "get_max", "set_max",
                 "fill_wrap", "fill_at_max", "copy_at_max", "copy_wrap")
  ] + [
  {
    "name":           "table64 failed table.grow answers 2^64-1",
    "wasm":           "./regression/table64-grow.wasm",
    "args":           ["--func", "to_test"],
    "expect_pattern": "*Result: 1*",
  },
]

#
# Helpers
#

def exit_info(rc):
    """(died, human readable) for a process return code."""
    if os.name == 'nt':
        # Unhandled SEH exceptions surface as NTSTATUS codes (0xC0000005 = AV).
        status = rc & 0xFFFFFFFF
        if status & 0xC0000000 == 0xC0000000:
            return True, f"crashed, status 0x{status:08X}"
        return False, f"exit code {rc}"

    if rc < 0:
        try:
            name = signal.Signals(-rc).name
        except ValueError:
            name = f"signal {-rc}"
        return True, f"killed by {name}"

    return False, f"exit code {rc}"

# ASan/UBSan/valgrind keep the process alive but say so on the way out
sanitizer_markers = [
    "AddressSanitizer",
    "LeakSanitizer",
    "MemorySanitizer",
    "ThreadSanitizer",
    "UndefinedBehaviorSanitizer",
    "runtime error:",
    "Invalid read of size",
    "Invalid write of size",
]

#
# Run
#

for test in tests:
    # "args" go before the module path, "func_args" after it - that is where
    # wasm3 expects the arguments of the function being called
    command = args.exec.split(' ') + test.get('args', []) + [test['wasm']] + test.get('func_args', [])
    command = list(map(str, command))

    issue = test.get('issue')
    title = f"issue #{issue}: {test['name']}" if issue else test['name']
    print(f"=== {title} ===")
    if args.verbose:
        print(' '.join(command))

    stats.total_run += 1
    problems = []

    timed_out = False
    try:
        p = subprocess.run(command, timeout=args.timeout,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        output = p.stdout.decode("utf-8", errors="replace")
        died, how = exit_info(p.returncode)
    except subprocess.TimeoutExpired:
        timed_out = True
        output, died, how = "", True, f"timeout after {args.timeout}s"

    if died:
        problems.append(how)

    for marker in sanitizer_markers:
        if marker in output:
            problems.append(f"sanitizer report: {marker}")
            break

    if not died and "expect_pattern" in test:
        if not fnmatch.fnmatch(output, test['expect_pattern']):
            problems.append(f"output does not match {test['expect_pattern']!r}")

    known = test.get('known_issue')

    if problems:
        if known:
            stats.known_issues += 1
            print(f"{ansi.WARNING}KNOWN ISSUE:{ansi.ENDC} {known}")
        else:
            stats.failed += 1
            if timed_out:
                stats.timeout += 1
            elif died:
                stats.crashed += 1
            print(f"{ansi.FAIL}FAIL:{ansi.ENDC} {'; '.join(problems)}")
        print(f"{' '.join(command)}")
        print(output.rstrip())
    else:
        if known:
            stats.failed += 1
            print(f"{ansi.FAIL}FAIL:{ansi.ENDC} marked as a known issue, but it passes now. "
                  f"Drop known_issue from the case.")
        elif args.verbose:
            print(output.rstrip())
        print(f"{ansi.OKGREEN}OK{ansi.ENDC} ({how})")

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
    if stats.known_issues:
        print(f"{ansi.WARNING} {stats.known_issues} known issue(s), not failing the build{ansi.OKGREEN}")
    print(f"======================={ansi.ENDC}")
