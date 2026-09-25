#!/usr/bin/env python3

# Carries WASI programs across snapshots, and across builds, and checks that
# what they print never changes.
#
# Each program runs in legs of a fixed gas budget. A leg that runs out pauses at
# the next pause point and saves a snapshot, and the next leg resumes it -
# alternately in --exec and --exec-other, which can be builds with different
# slot widths, byte orders, compilers or operating systems. Every few legs the
# snapshot is also resumed once without a gas limit at all, by the other build,
# and run to the end: a pause gas asked for has to go on in a runtime that is not
# metering.
#
# With --steps, each program is also stepped: from where its first leg stops,
# paused at each next pause point in turn, every resume asking for a pause again.
# Every step has to move on from where the last one stopped, and the last is
# finished by the other build. The first leg is there to get the program past
# reading its arguments.
#
# Usage:
#   ./run-snapshot-test.py
#   ./run-snapshot-test.py --exec ../build/wasm3 --exec-other ../build-slot64/wasm3
#   ./run-snapshot-test.py --legs 100 --steps 30 --verify
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
    "--steps",
    type=int,
    default=0,
    help="how many pause points to step each program through, after its first leg",
)
parser.add_argument(
    "--verify",
    action="store_true",
    help="check every snapshot with extra/snapshot-tool.py as well",
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
    tool = os.path.join("..", "extra", "snapshot-tool.py")
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

    # Half a leg's worth to spare: running out of gas pauses at the next pause
    # point, so each leg spends a little over its budget
    budget = max(gas / (args.legs - 0.5), 1.0) if args.legs > 1 else gas * 2
    fork_every = max(args.legs // (args.forks + 1), 1) if args.forks else 0

    snapshot = os.path.join(workdir, "leg.dmp")
    previous = os.path.join(workdir, "prev.dmp")
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

    if args.steps:
        step_program(prog, workdir, expected, budget)


def without_meta(raw):
    """A snapshot's sections but Meta, whose timestamp differs from save to save"""
    pos, kept = 8, []
    while pos < len(raw):
        kind, pos = raw[pos], pos + 1
        size = shift = 0
        while True:
            byte, pos = raw[pos], pos + 1
            size |= (byte & 0x7F) << shift
            shift += 7
            if byte < 0x80:
                break
        if kind != 0:
            kept.append(raw[pos : pos + size])
        pos += size
    return b"".join(kept)


def step_program(prog, workdir, expected, budget):
    snapshot = os.path.join(workdir, "step.dmp")
    previous = os.path.join(workdir, "step-prev.dmp")
    builds = [args.exec.split(), args.exec_other.split()]

    for path in (snapshot, previous):
        if os.path.exists(path):
            os.remove(path)

    output = run(
        builds[0]
        + ["--gas-limit", str(budget), "--snapshot", snapshot, prog["wasm"]]
        + prog.get("args", [])
    )

    for step in range(1, args.steps):
        if not os.path.exists(snapshot):
            fail(f"finished after {step} steps, short of {args.steps}")
            return
        if args.verify:
            verify(snapshot, prog["wasm"])
        os.replace(snapshot, previous)
        output += run(
            builds[step % 2]
            + [
                "--interrupt",
                "--snapshot",
                snapshot,
                "--resume",
                previous,
                prog["wasm"],
            ]
        )
        if os.path.exists(snapshot):
            with open(snapshot, "rb") as a, open(previous, "rb") as b:
                if without_meta(a.read()) == without_meta(b.read()):
                    fail(f"step {step + 1} stopped where step {step} did")
                    return

    if not os.path.exists(snapshot):
        fail(f"finished after {args.steps} steps, before the last could be resumed")
        return
    os.replace(snapshot, previous)
    output += run(builds[args.steps % 2] + ["--resume", previous, prog["wasm"]])
    if digest(prog, output) != expected:
        fail(f"stepped {args.steps} times, it printed something else")
        return

    print(f"  {args.steps} steps, each one further on")


def run_resource_limits(workdir):
    """A snapshot needing more than the runtime allows is refused, and stays usable

    The program grows its memory and table before it pauses, so the snapshot needs
    more than a fresh instance of the same module holds.
    """
    source = "./snapshot/resource-cli.wat"
    module = assemble_modules([source], args.exec)[source]
    snapshot = os.path.join(workdir, "limits.dmp")
    resumed = os.path.join(workdir, "limits-resumed.dmp")
    builds = [args.exec.split(), args.exec_other.split()]

    for path in (snapshot, resumed):
        if os.path.exists(path):
            os.remove(path)

    run(builds[0] + ["--gas-limit", "1", "--snapshot", snapshot, module])
    if not os.path.exists(snapshot):
        fail("the program finished before it paused")
        return
    with open(snapshot, "rb") as f:
        saved = f.read()

    for flag, value, message in (
        ("--max-memory", "64K", b"runtime memory limit exceeded"),
        ("--max-table-elements", "2", b"table elements limit exceeded"),
    ):
        proc = subprocess.run(
            builds[1] + [flag, value, "--resume", snapshot, module],
            timeout=args.timeout,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if proc.returncode == 0 or message not in proc.stderr:
            fail(f"{flag} {value} did not refuse the snapshot: {proc.stderr[-300:]!r}")
            return

    with open(snapshot, "rb") as f:
        if f.read() != saved:
            fail("a refused resume changed the snapshot")
            return

    # the same snapshot, once the limits fit what it holds
    run(
        builds[1]
        + ["--max-memory", "128K", "--max-table-elements", "4"]
        + ["--gas-limit", "1", "--snapshot", resumed, "--resume", snapshot, module]
    )
    if not os.path.exists(resumed):
        fail("the resume under sufficient limits did not run")
        return

    print("  refused under limits that are too small, resumed under ones that fit")


# Next to the programs, and named relative to here: one of the builds can be a
# Windows binary started from WSL, which sees this directory but not /tmp
check_build(args.exec)
check_build(args.exec_other)


def attempt(name, test, *test_args):
    print(f"=== {name} ===")
    stats.total_run += 1
    try:
        test(*test_args)
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


with tempfile.TemporaryDirectory(dir=".", prefix="snapshot-test-") as workdir:
    workdir = os.path.relpath(workdir)

    for prog in programs:
        if fnmatch.fnmatch(prog["name"], args.filter):
            attempt(prog["name"], run_program, prog, workdir)

    if fnmatch.fnmatch("resource limits", args.filter):
        attempt("resource limits", run_resource_limits, workdir)

if stats.failed:
    print(f"{ansi.FAIL}=======================")
    print(f" FAILED: {stats.failed}/{stats.total_run}")
    print(f"======================={ansi.ENDC}")
    sys.exit(1)
else:
    print(f"{ansi.OKGREEN}=======================")
    print(f" All {stats.total_run} tests OK")
    print(f"======================={ansi.ENDC}")
