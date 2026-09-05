#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy
# Usage:
#   ./run-regression-test.py
#   ./run-regression-test.py --exec ../custom_build/wasm3 --timeout 120
#   ./run-regression-test.py --exec "valgrind --error-exitcode=9 ../build/wasm3"
#   ./run-regression-test.py --host ../build/wasm3   # assemble on a quick build
#
# Each case in ./regression/ is a minimized module - from a reported issue, or
# from an engine corner that the spec suite does not reach. The primary
# assertion is always the same: wasm3 must not die on it. A clean trap or a
# clean error message is a pass; a signal, an abort, or a sanitizer report is a
# failure. Cases where the value or the diagnosis is the point also say which
# one they expect, exactly.
#
# The cases are kept as text and assembled before the run by the wast2json in
# ./wasi/wabt, which is itself a module and runs on the interpreter under test.
# Pass --host to assemble on something else, when the build under test is too
# slow to be worth 1.5 MB of wabt - a valgrind or a qemu command line.

import argparse
import fnmatch
import os
import re
import signal
import subprocess
import sys

sys.path.append("../extra")

from testutils import *

#
# Args handling
#

parser = argparse.ArgumentParser()
parser.add_argument("--exec", metavar="<interpreter>", default="../build/wasm3")
parser.add_argument(
    "--host",
    metavar="<interpreter>",
    help="what to run wast2json on, if not the interpreter under test",
)
parser.add_argument("--timeout", type=int, default=120)
parser.add_argument("-v", "--verbose", action="store_true")

args = parser.parse_args()

stats = SimpleNamespace(total_run=0, failed=0, crashed=0, timeout=0, known_issues=0)

#
# Test cases
#
# "module" is the source the case runs: a .wat or .wast is assembled first, a
# .wasm is taken as it is.
#
# How the run is expected to have ended, matched exactly against the line wasm3
# printed, so that a case asserting 1 does not also accept 15:
#
#   "expect_result"   what followed "Result: "
#   "expect_trap"     what followed "Error: [trap] "
#   "expect_error"    what followed "Error: ", trap prefix and all
#
# The parenthesised detail wasm3 appends to an error is not part of any of the
# three: a DEBUG build says "out of bounds memory access (memory size: 65536;
# access offset: 1)" where a release build stops at the message, so pinning it
# would pass only for the build it was written against.
#
# "expect_pattern" is the fallback for what cannot be pinned down: fnmatch
# syntax over the whole of stdout+stderr, for output the host gets a say in and
# for what a program prints itself. Keep '[' out of it: fnmatch reads it as a
# character class. A case may carry more than one of the four.
#
# "args" precede the module path; "func_args" follow it.
#

# fmt: off
tests = [
  {
    "name":           "f32.max NaN propagation",
    "issue":          405,
    "module":         "./regression/github-405.wat",
    "args":           ["--func", "to_test"],
    "expect_pattern": "*Result: *nan*",
  }, {
    "name":           "memory.grow by a negative number",
    "issue":          442,
    "module":         "./regression/github-442.wat",
    "args":           ["--func", "to_test"],
    "expect_result":  "-1",
  }, {
    # _start is the WASI entry point, so the app takes a separate path for it.
    # A plain module can still export _start with results, and those used to be
    # dropped instead of printed. 0/0 leaves the NaN sign up to the host, so the
    # sign of the copysign result is not part of the assertion.
    "name":           "_start with a return value",
    "issue":          351,
    "module":         "./regression/github-351.wat",
    "args":           ["--func", "_start"],
    "expect_pattern": "*Result: *0.000000*",
  }, {
    # WASI addresses the memory the module exports as "memory". These three
    # modules are the same program with that export moved: a host that assumes
    # memory 0 reads a zeroed iovec out of the wrong memory and writes nothing,
    # so the first case is silence rather than a crash.
    "name":           "WASI uses the exported memory, not memory 0",
    "issue":          418,
    "module":         "./regression/wasi-memory-export-index1.wat",
    "args":           ["--func", "_start"],
    "expect_pattern": "*HI*",
  }, {
    "name":           "WASI still uses memory 0 when that is the export",
    "issue":          418,
    "module":         "./regression/wasi-memory-export-index0.wat",
    "args":           ["--func", "_start"],
    "expect_pattern": "*HI*",
  }, {
    "name":           "WASI without an exported memory is refused",
    "issue":          418,
    "module":         "./regression/wasi-memory-export-missing.wat",
    "args":           ["--func", "_start"],
    "expect_error":   'WASI requires the module to export its memory as "memory"',
  }, {
    "name":           "Unlinked memory import",
    "issue":          462,
    "module":         "./regression/github-462.wat",
    "args":           ["--func", "_start"],
  }, {
    "name":           "clock_ms imported as i64",
    "module":         "./regression/clock-ms-i64.wat",
    "args":           ["--func", "to_test"],
    "expect_result":  "1",
  }, {
    "name":           "clock_ms imported as i32",
    "module":         "./regression/clock-ms-i32.wat",
    "args":           ["--func", "to_test"],
    "expect_result":  "1",
  }, {
    "name":           "local.tee over two live copies of an i64 local",
    "module":         "./regression/preserve-two-refs.wat",
    "args":           ["--func", "to_test"],
    "expect_result":  "2",
  }, {
    "name":           "br out of a typed block",
    "issue":          465,
    "module":         "./regression/github-465.wat",
    "args":           ["--func", "_start"],
  }, {
    "name":           "Call with more args than the callee returns",
    "issue":          477,
    "module":         "./regression/github-477.wat",
    "args":           ["--func", "main"],
    "expect_result":  "<Empty Stack>",
  }, {
    # The branch operands have to land in the loop's parameter slots before
    # control goes back around, or the next iteration re-reads whatever the
    # loop was entered with - here, a counter that never leaves 0.
    "name":           "br passes arguments to a loop",
    "issue":          582,
    "module":         "./regression/github-582.wat",
    "args":           ["--func", "run"],
    "expect_result":  "5",
  }, {
    # two parameters that trade places, so the copies collide and one of them
    # has to be routed through a temp slot
    "name":           "br passes colliding arguments to a loop",
    "issue":          582,
    "module":         "./regression/github-582.wat",
    "args":           ["--func", "swap"],
    "expect_result":  "1",
  }, {
    # unreachable code has no operands to hand over; the copy must be skipped
    # rather than reported as a stack-count mismatch
    "name":           "br to a loop from unreachable code",
    "issue":          582,
    "module":         "./regression/github-582.wat",
    "args":           ["--func", "poly-br"],
    "expect_trap":    "unreachable",
  }, {
    "name":           "br_if to a loop from unreachable code",
    "issue":          582,
    "module":         "./regression/github-582.wat",
    "args":           ["--func", "poly-br-if"],
    "expect_trap":    "unreachable",
  }, {
    "name":           "br_table to a loop from unreachable code",
    "issue":          582,
    "module":         "./regression/github-582.wat",
    "args":           ["--func", "poly-br-table"],
    "expect_trap":    "unreachable",
  }, {
    # The 0xFC sub-opcode is a LEB128 u32, and the spec allows non-minimal
    # encodings up to the five bytes a u32 takes. Reading it as a single byte
    # rejected valid modules, and disagreed with the validator, which read a LEB.
    # Both modules spell out their own bytes; see the .wast files.
    "name":           "0xFC sub-opcode in a non-minimal LEB",
    "module":         "./regression/fc-subopcode-overlong.wast",
    "args":           ["--func", "to_test"],
    "expect_result":  "3",
  }, {
    "name":           "0xFC sub-opcode past the LEB u32 limit",
    "module":         "./regression/fc-subopcode-toolong.wast",
    "args":           ["--func", "to_test"],
    "expect_error":   "LEB encoded value overflow",
  }, {
    "name":           "Non-numeric function argument",
    "issue":          367,
    "module":         "./lang/fib32.wasm",
    "args":           ["--func", "fib"],
    "func_args":      ["abc"],
    "expect_error":   "argument is not a number",
  }, {
    "name":           "Partially numeric function argument",
    "issue":          367,
    "module":         "./lang/fib32.wasm",
    "args":           ["--func", "fib"],
    "func_args":      ["12abc"],
    "expect_error":   "argument is not a number",
  }, {
    "name":           "Function argument out of range",
    "issue":          367,
    "module":         "./lang/fib32.wasm",
    "args":           ["--func", "fib"],
    "func_args":      ["4294967296"],
    "expect_error":   "argument out of range",
  }, {
    "name":           "Native stack exhaustion",
    "issue":          479,
    "module":         "./regression/github-479.wat",
    "args":           ["--func", "main"],
    "expect_trap":    "stack overflow",
  }, {
    # op_TryTable's native frame outlives the try region when control branches
    # past the block's end, so the frame has to notice it is no longer the
    # innermost handler. Returning 1 means it caught what it should have let by.
    "name":           "throw after branching out of a try region",
    "module":         "./regression/exceptions-stale-try.wat",
    "args":           ["--func", "to_test"],
    "expect_trap":    "uncaught exception",
  }, {
    "name":           "throw after leaving a try region via br_table",
    "module":         "./regression/exceptions-stale-try-br-table.wat",
    "args":           ["--func", "to_test"],
    "expect_trap":    "uncaught exception",
  }, {
    # the catch handler runs on op_TryTable's frame, whose memory pointer is
    # stale if the body grew linear memory
    "name":           "memory.grow inside a try body",
    "module":         "./regression/exceptions-grow-in-try.wat",
    "args":           ["--func", "to_test"],
    "expect_result":  "7",
  }, {
    # each lap enters a try region; the native frame has to come back off
    "name":           "try region entered once per loop iteration",
    "module":         "./regression/exceptions-try-in-loop.wat",
    "args":           ["--func", "to_test"],
    "expect_result":  "1000000",
  }, {
    # a return_call reuses the m3 frame, so the m3 stack never grows to report
    # the depth - but it leaves the enclosing loop's native frame standing, and
    # op_Loop's own stack probe is the only thing that sees the pile grow
    "name":           "return_call from inside a loop",
    "module":         "./regression/return-call-in-loop.wat",
    "args":           ["--func", "to_test"],
    "expect_trap":    "stack overflow",
  }, {
    # the arguments are marshalled into the runtime stack before any compiled
    # code runs, so op_Entry's overflow check is too late to cover the writes:
    # 28 i64 arguments are 224 bytes going into a 128-byte stack
    "name":           "argument list larger than the runtime stack",
    "module":         "./regression/call-args-overflow-stack.wat",
    "args":           ["--stack-size", "128", "--func", "to_test"],
    "func_args":      ["0"] * 28,
    "expect_trap":    "stack overflow",
  }, {
    # the memory is real, so the traps below are about the addresses and not
    # about the module failing to load
    "name":           "memory64 in-bounds access",
    "module":         "./regression/memory64-bounds.wat",
    "args":           ["--func", "ok"],
    "expect_result":  "0",
  }] + [
  {
    # address + offset is added in a u64, which is only safe because an address
    # at or above d_m3AddressLimit is refused first. Each of these would land
    # back inside the memory if the sum were allowed to wrap.
    "name":           f"memory64 effective address does not wrap ({func})",
    "module":         "./regression/memory64-bounds.wat",
    "args":           ["--func", func],
    "expect_trap":    "out of bounds memory access",
  } for func in ("wrap_max", "wrap_limit", "at_limit", "below_limit",
                 "far_offset", "wrap_store")
  ] + [
  {
    # start + length has the same room to wrap as a load's address + offset
    "name":           f"memory64 bulk operation does not wrap ({func})",
    "module":         "./regression/memory64-bulk-wrap.wat",
    "args":           ["--func", func],
    "expect_trap":    "out of bounds memory access",
  } for func in ("fill_wrap", "fill_at_max", "copy_wrap", "copy_at_max")
  ] + [
  {
    # the checked address goes to a scratch slot; writing it back over a
    # constant operand would corrupt the function's constant table
    "name":           "memory64 address check leaves its operand alone",
    "module":         "./regression/memory64-const-addr.wat",
    "args":           ["--func", "to_test"],
    "expect_result":  "135",
  }, {
    "name":           "memory64 failed memory.grow answers 2^64-1",
    "module":         "./regression/memory64-grow.wat",
    "args":           ["--func", "to_test"],
    "expect_result":  "1",
  }, {
    "name":           "table64 in-bounds call_indirect",
    "module":         "./regression/table64-bounds.wat",
    "args":           ["--func", "ok"],
    "expect_result":  "5",
  }] + [
  {
    # a table64 index is a whole u64, and a bulk length added to a start still
    # has room to wrap even though table sizes stay far below 2^32. The two
    # groups differ in which trap the index has to reach: call_indirect resolves
    # the element before it can find it missing, everything else bounds-checks.
    "name":           f"table64 index does not wrap ({func})",
    "module":         "./regression/table64-bounds.wat",
    "args":           ["--func", func],
    "expect_trap":    trap,
  } for trap, funcs in (
      ("undefined element",          ("call_max", "call_past_u32")),
      ("out of bounds table access", ("get_max", "set_max", "fill_wrap",
                                      "fill_at_max", "copy_at_max", "copy_wrap")),
    ) for func in funcs
  ] + [
  {
    "name":           "table64 failed table.grow answers 2^64-1",
    "module":         "./regression/table64-grow.wat",
    "args":           ["--func", "to_test"],
    "expect_result":  "1",
  },
]
# fmt: on

#
# Helpers
#


def exit_info(rc):
    """(died, human readable) for a process return code."""
    if os.name == "nt":
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


def last_line(output, prefix):
    """What followed the last `<prefix>: ` line wasm3 printed, or None."""
    found = re.findall(rf"^{prefix}: (.*)$", output, re.MULTILINE)
    return found[-1].rstrip() if found else None


def check_expectations(test, output):
    """Every way the case says the run should have ended, against what it printed."""
    problems = []

    result = last_line(output, "Result")

    # wasm3 prints `Error: <result>`, and then ` (<detail>)` when the runtime
    # recorded one. What goes in there is up to the build - a DEBUG build spells
    # out the memory size and the address an access went to, a release build has
    # nothing to add and prints no detail at all - so it is split off here and
    # the case asserts on the result alone.
    error = last_line(output, "Error")
    if error is not None:
        error = error.split(" (", 1)[0]
    trap = error[len("[trap] ") :] if error and error.startswith("[trap] ") else None

    # An exact expectation that finds nothing at all reports what was missing
    # rather than "None != 5", which reads as though wasm3 answered None.
    for key, kind, actual in (
        ("expect_result", "result", result),
        ("expect_trap", "trap", trap),
        ("expect_error", "error", error),
    ):
        if key not in test:
            continue
        if actual is None:
            problems.append(f"printed no {kind}, expected {test[key]!r}")
        elif actual != test[key]:
            problems.append(f"{kind} is {actual!r}, expected {test[key]!r}")

    if "expect_pattern" in test and not fnmatch.fnmatch(output, test["expect_pattern"]):
        problems.append(f"output does not match {test['expect_pattern']!r}")

    return problems


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

modules = assemble_modules(
    (test["module"] for test in tests), args.host or args.exec, args.verbose
)

for test in tests:
    # "args" go before the module path, "func_args" after it - that is where
    # wasm3 expects the arguments of the function being called
    command = (
        args.exec.split(" ")
        + test.get("args", [])
        + [modules[test["module"]]]
        + test.get("func_args", [])
    )
    command = list(map(str, command))

    issue = test.get("issue")
    title = f"issue #{issue}: {test['name']}" if issue else test["name"]
    print(f"=== {title} ===")
    if args.verbose:
        print(" ".join(command))

    stats.total_run += 1
    problems = []

    timed_out = False
    try:
        p = subprocess.run(
            command,
            check=False,
            timeout=args.timeout,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
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

    if not died:
        problems += check_expectations(test, output)

    known = test.get("known_issue")

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
            print(
                f"{ansi.FAIL}FAIL:{ansi.ENDC} marked as a known issue, but it passes now. "
                f"Drop known_issue from the case."
            )
        elif args.verbose:
            print(output.rstrip())
        print(f"{ansi.OKGREEN}OK{ansi.ENDC} ({how})")

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
    if stats.known_issues:
        print(
            f"{ansi.WARNING} {stats.known_issues} known issue(s), not failing the build{ansi.OKGREEN}"
        )
    print(f"======================={ansi.ENDC}")
