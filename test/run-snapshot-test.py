#!/usr/bin/env python3

# Carries WASI programs across snapshots, and across builds, and checks that
# what they print never changes.
#
# Each program runs in legs of a fixed gas budget. A leg that runs out saves a
# snapshot, and the next leg resumes it - alternately in --exec and --exec-other,
# which can be builds with different slot widths, byte orders, compilers or
# operating systems. Every few legs the snapshot is also resumed once without a
# gas limit at all, by the other build, and run to the end: metering changes
# where a body can stop, so a snapshot taken at a gas charge has to be able to
# go on without one.
#
# Usage:
#   ./run-snapshot-test.py
#   ./run-snapshot-test.py --exec ../build/wasm3 --exec-other ../build-slot64/wasm3
#   ./run-snapshot-test.py --legs 100 --verify
#
# Only stdout is compared. The programs here read no input and open no files:
# a snapshot carries the program's Wasm state, not the host's, so a file
# position or a half-read stdin would not come across. WASI arguments are host
# state too, and are read once at startup, which is why no leg is cut short
# before the program has seen them.

import argparse
import fnmatch
import hashlib
import os
import re
import subprocess
import sys
import tempfile

sys.path.append("../extra")

from testutils import *

parser = argparse.ArgumentParser()
parser.add_argument("--exec", metavar="<interpreter>", default="../build/wasm3")
parser.add_argument(
    "--exec-other",
    metavar="<interpreter>",
    help="the build every other leg resumes in (default: --exec)",
)
parser.add_argument(
    "--legs",
    type=int,
    default=24,
    help="how many legs to cut each program into, exactly: the first and every "
    "other one in --exec, the rest in --exec-other",
)
parser.add_argument(
    "--forks",
    type=int,
    default=3,
    help="how many snapshots per program to also finish without a gas limit",
)
parser.add_argument(
    "--verify",
    action="store_true",
    help="check every snapshot with extra/w3s-tool.py as well",
)
parser.add_argument("--timeout", type=int, default=120)
parser.add_argument("--filter", default="*", help="run only programs matching this")

args = parser.parse_args()
args.exec_other = args.exec_other or args.exec

stats = SimpleNamespace(total_run=0, failed=0, crashed=0, timeout=0)

# fmt: off
programs = [
  {
    "name":           "mandelbrot",
    "wasm":           "./wasi/mandelbrot/mandel.wasm",
    "args":           ["128", "4e5"],
    "expect_sha1":    "37091e7ce96adeea88f079ad95d239a651308a56"
  }, {
    "name":           "mandelbrot (doubledouble)",
    "wasm":           "./wasi/mandelbrot/mandel_dd.wasm",
    "args":           ["64", "4e5"],
  }, {
    "name":           "smallpt (explicit light sampling)",
    "wasm":           "./wasi/smallpt/smallpt-ex.wasm",
    "args":           ["4", "32"],
    "expect_sha1":    "ea05d85998b2f453b588ef76a1256215bf9b851c"
  }, {
    "name":           "smallpt (explicit light sampling, multi-value)",
    "wasm":           "./wasi/smallpt/smallpt-ex-mv.wasm",
    "args":           ["4", "32"],
    "expect_sha1":    "ea05d85998b2f453b588ef76a1256215bf9b851c"
  }, {
    "name":           "CoreMark (fixed iterations)",
    "wasm":           "./wasi/coremark/coremark.wasm",
    "args":           ["0x0", "0x0", "0x66", "40"],
    # the timings are the one thing that differs from run to run
    "normalize":      rb"(Total ticks|Total time \(secs\)|Iterations/Sec|CoreMark 1\.0)\s*:[^\n]*",
  },
]
# fmt: on


def fail(msg):
    print(f"{ansi.FAIL}FAIL:{ansi.ENDC} {msg}")
    stats.failed += 1


def run(command):
    """stdout, and whether it left a snapshot behind"""
    proc = subprocess.run(
        command,
        timeout=args.timeout,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if proc.returncode != 0:
        raise subprocess.CalledProcessError(
            proc.returncode, command, proc.stdout, proc.stderr
        )
    return proc.stdout


def verify(path, wasm):
    tool = os.path.join("..", "extra", "w3s-tool.py")
    proc = subprocess.run(
        [sys.executable, tool, "--wasm", wasm, "verify", path],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stdout.decode(errors="replace").strip())


def total_gas(prog):
    """What the whole program costs, from an uninterrupted metered run"""
    proc = subprocess.run(
        args.exec.split() + ["--gas-meter", prog["wasm"]] + prog.get("args", []),
        timeout=args.timeout,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    match = re.search(rb"Gas used: ([0-9.]+)", proc.stderr)
    if proc.returncode != 0 or not match:
        raise RuntimeError(f"could not meter a run: {proc.stderr[-300:]!r}")
    return proc.stdout, float(match.group(1))


def check_build(command):
    """A build without snapshots would fail every leg, and say so less clearly"""
    proc = subprocess.run(
        command.split() + ["--version"],
        timeout=args.timeout,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if b"snapshots" not in proc.stdout:
        print(f"{ansi.FAIL}{command} is not a build with snapshots:{ansi.ENDC}")
        print(proc.stdout.decode(errors="replace").strip())
        sys.exit(1)


def digest(prog, output):
    if "normalize" in prog:
        output = re.sub(prog["normalize"], b"", output)
    return hashlib.sha1(output).hexdigest()


def run_program(prog, workdir):
    expected, gas = total_gas(prog)
    if "expect_sha1" in prog and digest(prog, expected) != prog["expect_sha1"]:
        fail("an uninterrupted run does not print what it should")
        return
    expected = digest(prog, expected)

    # Half a leg's worth to spare: a leg stops short of the segment it cannot pay
    # for in full, so each one spends a little under its budget
    budget = max(gas / (args.legs - 0.5), 1.0) if args.legs > 1 else gas * 2
    fork_every = max(args.legs // (args.forks + 1), 1) if args.forks else 0

    snapshot = os.path.join(workdir, "leg.w3s")
    previous = os.path.join(workdir, "prev.w3s")
    builds = [args.exec.split(), args.exec_other.split()]

    for path in (snapshot, previous):
        if os.path.exists(path):
            os.remove(path)

    output = run(
        builds[0]
        + ["--gas-limit", str(budget), "--snapshot", snapshot, prog["wasm"]]
        + prog.get("args", [])
    )

    legs = 1
    forks = 0

    while os.path.exists(snapshot):
        if args.verify:
            verify(snapshot, prog["wasm"])

        os.replace(snapshot, previous)
        other = builds[legs % 2]

        # the same stop, taken to the end by the other build with no limit
        if fork_every and legs % fork_every == 0 and forks < args.forks:
            forks += 1
            rest = run(other + ["--resume", previous, prog["wasm"]])
            if digest(prog, output + rest) != expected:
                fail(
                    f"finished without a gas limit after leg {legs}, it printed something else"
                )
                return

        output += run(
            other
            + [
                "--gas-limit",
                str(budget),
                "--snapshot",
                snapshot,
                "--resume",
                previous,
                prog["wasm"],
            ]
        )
        legs += 1

        if legs > args.legs:
            fail(f"still going after {args.legs} legs")
            return

    if digest(prog, output) != expected:
        fail(f"carried across {legs} legs, it printed something else")
        return

    if legs != args.legs:
        fail(f"finished in {legs} legs rather than {args.legs}")
        return

    print(f"  {legs} legs, {forks} finished without a gas limit")


# Next to the programs, and named relative to here: one of the builds can be a
# Windows binary started from WSL, which sees this directory but not /tmp
check_build(args.exec)
check_build(args.exec_other)

with tempfile.TemporaryDirectory(dir=".", prefix="snapshot-test-") as workdir:
    workdir = os.path.relpath(workdir)

    for prog in programs:
        if not fnmatch.fnmatch(prog["name"], args.filter):
            continue

        print(f"=== {prog['name']} ===")
        stats.total_run += 1
        try:
            run_program(prog, workdir)
        except subprocess.TimeoutExpired:
            stats.timeout += 1
            fail("Timeout")
        except subprocess.CalledProcessError as e:
            stats.crashed += 1
            fail(
                f"{' '.join(map(str, e.cmd))} exited with {e.returncode}: "
                f"{e.stderr.decode(errors='replace').strip()[-300:]}"
            )
        except RuntimeError as e:
            fail(str(e))

if stats.failed:
    print(f"{ansi.FAIL}=======================")
    print(f" FAILED: {stats.failed}/{stats.total_run}")
    print(f"======================={ansi.ENDC}")
    sys.exit(1)
else:
    print(f"{ansi.OKGREEN}=======================")
    print(f" All {stats.total_run} tests OK")
    print(f"======================={ansi.ENDC}")
