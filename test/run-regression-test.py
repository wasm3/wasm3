#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy
# Usage:
#   ./run-regression-test.py
#   ./run-regression-test.py --exec ../custom_build/wasm3 --timeout 120
#   ./run-regression-test.py --exec "valgrind --error-exitcode=9 ../build/wasm3"
#
# Each case in ./regression/ is a minimized module from a reported issue. The
# primary assertion is always the same: wasm3 must not die on it. A clean trap
# or a clean error message is a pass; a signal, an abort, or a sanitizer report
# is a failure. Cases where the returned value is the point of the issue also
# carry an expected output pattern.

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
    "name":           "Unlinked memory import",
    "issue":          462,
    "wasm":           "./regression/github-462.wasm",
    "args":           ["--func", "_start"],
    # wasm3 cannot satisfy a memory import, so runtime->memory.mallocated stays
    # NULL and op_Entry dereferences it. Must be rejected or trapped instead.
    "known_issue":    "segfaults in op_Entry: _mem is NULL when the memory import is unlinked",
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
    "name":           "Native stack exhaustion",
    "issue":          479,
    "wasm":           "./regression/github-479.wasm",
    "args":           ["--func", "main"],
    "expect_pattern": "*stack overflow*",
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
    command = args.exec.split(' ') + test.get('args', []) + [test['wasm']]
    command = list(map(str, command))

    title = f"issue #{test['issue']}: {test['name']}"
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
