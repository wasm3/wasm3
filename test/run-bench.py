#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy
#
# Benchmark harness: compares command lines on the WASI workloads.
#
# Usage:
#   ./run-bench.py --exec "plain=../build/wasm3" \
#                  --exec "metered=../build/wasm3 --gas-limit 1e12"
#   ./run-bench.py --exec "old=../build-old/wasm3" --exec "new=../build/wasm3" --rounds 9
#   ./run-bench.py --exec "a=../build/wasm3" --exec "b=../build/wasm3" --cpu 2 --filter 'stream,mandel'
#   ./run-bench.py --list
#
# An --exec is  <name>=<command>, where the command is however the engine under test is
# invoked: a path to a binary, that binary plus runtime flags, or a different engine
# altogether. Each workload's own arguments are appended to it, and the name is what
# labels the column. Nothing is built, fetched or checked out - what the caller names is
# what runs, exactly as given.
#
# Every metric is normalised to a rate (higher is better) so the ratio columns read the
# same way whatever the workload reports. Commands run interleaved -- one round at a
# time, with their order rotated per round -- so a machine that drifts during the
# session penalises each one equally instead of whichever ran last.

import argparse
import fnmatch
import json
import os
import re
import shlex
import statistics
import subprocess
import sys
import time

TEST_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.dirname(TEST_DIR)

sys.path.append(os.path.join(REPO_DIR, "extra"))

from testutils import ansi

#
# Workloads
#
# metrics: (label, kind, pattern[, scale])
#   "re"      value parsed out of the output, already a rate
#   "re_inv"  value parsed out of the output, a duration -- rate is scale/value
#   "wall"    wall-clock time of the whole process -- rate is scale/seconds
#
# "wall" includes parse+compile, which is a fixed cost of a few ms; it is only used
# where the workload prints no timing of its own. scale defaults to 1 and only exists
# to put a metric in units worth reading (c-ray in Kpixel/s rather than renders/ms).
#

# fmt: off
WORKLOADS = [
    {
        "name":     "coremark",
        "wasm":     "./wasi/coremark/coremark.wasm",
        "expect":   "Correct operation validated",
        "metrics":  [("coremark", "re", r"Iterations/Sec\s*:\s*([0-9.]+)")],
    }, {
        "name":     "c-ray",
        "wasm":     "./wasi/c-ray/c-ray.wasm",
        "args":     ["-s", "1024x1024"],
        "stdin":    "./wasi/c-ray/scene",
        "quiet":    True,
        "metrics":  [("c-ray", "re_inv", r"\((\d+) milliseconds\)", 1024 * 1024)],
    }, {
        "name":     "stream",
        "wasm":     "./wasi/stream/stream.wasm",
        "expect":   "Solution Validates",
        "metrics":  [("stream-copy",  "re", r"^Copy:\s+([0-9.]+)"),
                     ("stream-scale", "re", r"^Scale:\s+([0-9.]+)"),
                     ("stream-add",   "re", r"^Add:\s+([0-9.]+)"),
                     ("stream-triad", "re", r"^Triad:\s+([0-9.]+)")],
    }, {
        "name":     "mandel",
        "wasm":     "./wasi/mandelbrot/mandel.wasm",
        "args":     ["384", "4e5"],
        "quiet":    True,
        "metrics":  [("mandel", "wall", None)],
    }, {
        "name":     "smallpt",
        "wasm":     "./wasi/smallpt/smallpt-ex.wasm",
        "args":     ["8", "128"],
        "quiet":    True,
        "metrics":  [("smallpt", "wall", None)],
    }, {
        "name":     "brotli",
        "wasm":     "./wasi/brotli/brotli.wasm",
        "args":     ["-c", "-f"],
        "stdin":    "./wasi/brotli/alice29.txt",
        "quiet":    True,
        "metrics":  [("brotli", "wall", None)],
    }, {
        "name":     "mal-fib",
        "wasm":     "./wasi/mal/mal.wasm",
        "args":     ["./wasi/mal/test-fib.mal", "22"],
        "metrics":  [("mal-fib", "wall", None)],
    }, {
        "name":     "selfhost-fib",
        "wasm":     "./self-hosting/wasm3-fib.wasm",
        "expect":   "Result: 832040",
        "metrics":  [("selfhost-fib", "re_inv", r"Elapsed:\s*([0-9]+) ms", 1000)],
    },
]
# fmt: on


def fatal(msg):
    print(f"{ansi.FAIL}error:{ansi.ENDC} {msg}")
    sys.exit(1)


#
# Commands under test
#


def split_command(s):
    """Split a command line into argv. A Windows path is full of backslashes, which
    shlex in its POSIX mode would eat as escapes, so quoting is unwrapped here instead:
    a token keeps its backslashes and sheds one surrounding pair of quotes."""
    argv = []
    for tok in shlex.split(s, posix=False):
        if len(tok) >= 2 and tok[0] == tok[-1] and tok[0] in "\"'":
            tok = tok[1:-1]
        argv.append(tok)
    return argv


class Exec:
    def __init__(self, spec):
        name, sep, command = spec.partition("=")
        # the name is matched strictly so that a command carrying its own '=' -- say
        # --gas-limit=10 -- is reported as a missing name rather than silently taken
        # apart at the wrong place
        if not sep or not re.fullmatch(r"[\w.-]+", name):
            fatal(f"--exec '{spec}' is not <name>=<command>")

        self.name = name
        self.command = split_command(command)
        if not self.command:
            fatal(f"--exec '{spec}' names no command")

        # The child is run in TEST_DIR, where the workloads are, so a relative command
        # would mean one thing on POSIX (resolved in the child, after the chdir) and
        # another on Windows (resolved in the parent). Pin it to the caller's cwd, which
        # is what a shell would have done. A bare name is left for the PATH lookup.
        exe = self.command[0]
        if os.sep in exe or (os.altsep and os.altsep in exe):
            self.command[0] = os.path.abspath(exe)

    def describe(self):
        return f"{self.name}: {' '.join(self.command)}"


#
# Measurement
#


def measure(ex, wl, taskset, timeout):
    """Run one workload once, return {label: rate}."""
    cmd = []
    if taskset:
        cmd += ["taskset", "-c", taskset]
    cmd += ex.command + [wl["wasm"]] + wl.get("args", [])

    # the workload paths are relative to TEST_DIR, which is the child's cwd -- this
    # open() is the parent's, so it has to be resolved rather than inherited
    stdin = open(os.path.join(TEST_DIR, wl["stdin"]), "rb") if "stdin" in wl else None
    try:
        t0 = time.perf_counter()
        proc = subprocess.run(
            cmd,
            cwd=TEST_DIR,
            stdin=stdin,
            timeout=timeout,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        wall = time.perf_counter() - t0
    except subprocess.TimeoutExpired:
        fatal(f"{ex.name}/{wl['name']}: timed out after {timeout}s")
    except OSError as e:
        fatal(f"{ex.name}: cannot run '{' '.join(ex.command)}': {e}")
    finally:
        if stdin:
            stdin.close()

    text = proc.stdout.decode("utf-8", errors="replace")
    if proc.returncode != 0:
        fatal(f"{ex.name}/{wl['name']}: exited with {proc.returncode}\n{text[-2000:]}")

    if "expect" in wl and wl["expect"] not in text:
        fatal(
            f"{ex.name}/{wl['name']}: output missing '{wl['expect']}'\n{text[-2000:]}"
        )

    out = {}
    for metric in wl["metrics"]:
        label, kind, pattern = metric[:3]
        scale = metric[3] if len(metric) > 3 else 1.0
        if kind == "wall":
            out[label] = scale / wall
            continue
        m = re.search(pattern, text, re.MULTILINE)
        if not m:
            fatal(f"{ex.name}/{wl['name']}: no match for /{pattern}/\n{text[-2000:]}")
        value = float(m.group(1))
        if value <= 0:
            fatal(
                f"{ex.name}/{wl['name']}: metric {label} is {value}; "
                f"the workload is too small to time on this machine"
            )
        out[label] = value if kind == "re" else scale / value
    return out


#
# Reporting
#


def spread(values):
    return (max(values) / min(values) - 1.0) * 100.0


def fmt_rate(x):
    # wall-clock metrics come back as 1/seconds, self-reported ones as scores in the
    # thousands, so pick the precision from the magnitude
    if x >= 1000:
        return f"{x:,.0f}"
    if x >= 10:
        return f"{x:.1f}"
    return f"{x:.3f}"


def report(execs, labels, samples, baseline):
    """samples[(exec name, label)] = [rate per round]"""
    base = execs[baseline]

    name_w = max(len(l) for l in labels) + 1
    col_w = max(11, max(len(e.name) for e in execs) + 2)

    print()
    print(f"{ansi.BOLD}Median rate (higher is better){ansi.ENDC}")
    header = " " * name_w + "".join(f"{e.name:>{col_w}}" for e in execs) + "   spread"
    print(header)
    print("-" * len(header))
    for label in labels:
        row = f"{label:<{name_w}}"
        for e in execs:
            row += f"{fmt_rate(statistics.median(samples[(e.name, label)])):>{col_w}}"
        worst = max(spread(samples[(e.name, label)]) for e in execs)
        row += f"{worst:>8.1f}%"
        print(row)

    if len(execs) < 2:
        return

    print()
    print(f"{ansi.BOLD}Paired ratio vs {base.name} (per-round, median){ansi.ENDC}")
    header = " " * name_w + "".join(f"{e.name:>{col_w}}" for e in execs)
    print(header)
    print("-" * len(header))

    totals = {e.name: [] for e in execs}
    for label in labels:
        row = f"{label:<{name_w}}"
        for e in execs:
            ratios = [
                a / b
                for a, b in zip(samples[(e.name, label)], samples[(base.name, label)])
            ]
            r = statistics.median(ratios)
            totals[e.name].append(r)
            if e is base:
                row += f"{'--':>{col_w}}"
            else:
                colour = ansi.OKGREEN if r > 1.005 else (ansi.FAIL if r < 0.995 else "")
                cell = f"{r:.3f}x"
                row += f"{colour}{cell:>{col_w}}{ansi.ENDC if colour else ''}"
        print(row)

    print("-" * len(header))
    row = f"{'geomean':<{name_w}}"
    for e in execs:
        if e is base:
            row += f"{'--':>{col_w}}"
        else:
            g = statistics.geometric_mean(totals[e.name])
            colour = ansi.OKGREEN if g > 1.005 else (ansi.FAIL if g < 0.995 else "")
            cell = f"{g:.3f}x"
            row += f"{colour}{cell:>{col_w}}{ansi.ENDC if colour else ''}"
    print(row)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--exec", action="append", metavar="NAME=COMMAND", default=[], dest="execs"
    )
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--filter", metavar="GLOB[,GLOB...]", default="*")
    parser.add_argument(
        "--cpu", metavar="LIST", help="pin runs to these cores (taskset)"
    )
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--json", metavar="FILE")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    if args.list:
        for wl in WORKLOADS:
            for m in wl["metrics"]:
                print(f"{m[0]:<16} {wl['name']:<14} {m[1]}")
        return

    if not args.execs:
        fatal("no --exec given")

    execs = [Exec(s) for s in args.execs]
    if len({e.name for e in execs}) != len(execs):
        fatal("--exec names must be unique")

    patterns = args.filter.split(",")
    workloads = [
        w
        for w in WORKLOADS
        if any(fnmatch.fnmatch(w["name"], p.strip()) for p in patterns)
    ]
    if not workloads:
        fatal(f"no workload matches '{args.filter}'")
    labels = [m[0] for w in workloads for m in w["metrics"]]

    print(f"{ansi.BOLD}Comparing{ansi.ENDC}")
    for e in execs:
        print(f"  {e.describe()}")

    samples = {(e.name, label): [] for e in execs for label in labels}

    print(
        f"\n{ansi.BOLD}Running{ansi.ENDC} {len(labels)} metrics x {len(execs)} commands "
        f"x {args.rounds} rounds" + (f", pinned to cpu {args.cpu}" if args.cpu else "")
    )
    for r in range(args.rounds):
        # rotate the order so no command is always first or always last
        order = execs[r % len(execs) :] + execs[: r % len(execs)]
        print(f"  round {r + 1}/{args.rounds}: ", end="", flush=True)
        for e in order:
            print(f"{e.name} ", end="", flush=True)
            for wl in workloads:
                for label, rate in measure(e, wl, args.cpu, args.timeout).items():
                    samples[(e.name, label)].append(rate)
        print()

    report(execs, labels, samples, baseline=0)

    if args.json:
        with open(args.json, "w") as f:
            json.dump(
                {
                    "execs": [{"name": e.name, "command": e.command} for e in execs],
                    "samples": {f"{k[0]}/{k[1]}": v for k, v in samples.items()},
                },
                f,
                indent=2,
            )
        print(f"\nraw samples -> {args.json}")


if __name__ == "__main__":
    main()
