#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy
#
# Benchmark harness: builds several wasm3 variants and compares them on the WASI workloads.
#
# Usage:
#   ./run-bench.py --variant main=main --variant optz=. --rounds 5
#   ./run-bench.py --variant "preload=.:-Dd_m3PreloadNextOp=1" --rounds 5 --cpu 2
#   ./run-bench.py --list
#
# A variant is  <name>=<source>[:<extra cmake args>]
#   <source> is a git ref, checked out into a throwaway worktree, or "." for the live tree.
#   Everything after the first ':' is passed to cmake verbatim, so a variant can differ
#   from its source tree by configuration alone.
#
# Every metric is normalised to a rate (higher is better) so the ratio columns read the
# same way whatever the workload reports. Variants run interleaved -- one round at a
# time, with the variant order rotated per round -- so a machine that drifts during the
# session penalises each variant equally instead of whichever one ran last.

import argparse
import fnmatch
import json
import os
import re
import shlex
import shutil
import statistics
import subprocess
import sys
import time

sys.path.append("../extra")

from testutils import ansi

TEST_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.dirname(TEST_DIR)

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


def run(cmd, quiet=True, **kwargs):
    """Run a build step. When quiet, its output is swallowed unless it fails --
    cmake writes its banner to stderr, so silencing stdout alone is not enough."""
    kwargs.setdefault("cwd", REPO_DIR)
    if not quiet:
        return subprocess.run(cmd, check=True, **kwargs)
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kwargs)
    if p.returncode != 0:
        print()
        print(p.stdout.decode("utf-8", errors="replace"))
        fatal(f"'{' '.join(cmd[:2])} ...' exited with {p.returncode}")
    return p


#
# Variants
#


class Variant:
    def __init__(self, spec, build_root, deps_dir):
        if "=" not in spec:
            fatal(f"variant '{spec}' is not <name>=<source>[:<cmake args>]")
        name, rest = spec.split("=", 1)
        source, _, cmake_args = rest.partition(":")

        self.name = name
        self.source = source or "."
        # shlex, not split(): a variant usually carries a quoted -DCMAKE_C_FLAGS="..."
        self.cmake_args = shlex.split(cmake_args)
        self.build_dir = os.path.join(build_root, name)
        self.deps_dir = deps_dir
        self.export = (
            None if self.source == "." else os.path.join(build_root, "src-" + name)
        )
        self.exe = os.path.join(self.build_dir, "wasm3")
        self.rev = self._resolve_rev()

    def _resolve_rev(self):
        ref = "HEAD" if self.source == "." else self.source
        try:
            r = subprocess.run(
                ["git", "-C", REPO_DIR, "rev-parse", "--short", ref],
                capture_output=True,
                text=True,
                check=True,
            )
            return r.stdout.strip()
        except subprocess.CalledProcessError:
            fatal(f"{self.name}: '{self.source}' is not a git ref")

    @property
    def src_dir(self):
        return self.export or REPO_DIR

    def describe(self):
        args = " ".join(self.cmake_args) or "(default config)"
        return f"{self.name}: {self.source} @{self.rev} {args}"

    def prepare_source(self):
        # Export rather than `git worktree add`: a worktree would register a path in the
        # repo, and one created from WSL is not resolvable by the Windows-side git.
        if not self.export:
            return
        if os.path.isdir(self.export):
            shutil.rmtree(self.export)
        os.makedirs(self.export)
        archive = subprocess.run(
            ["git", "-C", REPO_DIR, "archive", self.source],
            check=True,
            stdout=subprocess.PIPE,
        ).stdout
        subprocess.run(["tar", "-x", "-C", self.export], check=True, input=archive)

    def stamp(self):
        return f"{self.rev}\n{' '.join(self.cmake_args)}\n"

    def build(self, jobs, verbose=False):
        self.prepare_source()

        # A cmake cache silently keeps the compiler it was first configured with, so
        # re-running with -DCLANG=1 over a gcc build dir yields a gcc binary labelled
        # as clang. Wipe the build dir whenever the configuration changed.
        stamp_file = os.path.join(self.build_dir, ".bench-stamp")
        if os.path.isdir(self.build_dir):
            old = ""
            if os.path.isfile(stamp_file):
                with open(stamp_file) as f:
                    old = f.read()
            if old != self.stamp():
                shutil.rmtree(self.build_dir)

        cmake = [
            "cmake",
            "-S",
            self.src_dir,
            "-B",
            self.build_dir,
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DFETCHCONTENT_BASE_DIR={self.deps_dir}",
        ]
        if shutil.which("ninja"):
            cmake += ["-G", "Ninja"]
        cmake += self.cmake_args

        run(cmake, quiet=not verbose)
        run(["cmake", "--build", self.build_dir, "-j", str(jobs)], quiet=not verbose)

        if not os.path.isfile(self.exe):
            fatal(f"{self.name}: build produced no {self.exe}")
        with open(stamp_file, "w") as f:
            f.write(self.stamp())

    def compiler(self):
        """Who actually produced the binary. Read out of the ELF rather than out of
        CMakeCache.txt -- the project assigns CMAKE_C_COMPILER as a normal variable, so
        it never reaches the cache, and a stale build dir would otherwise go unnoticed.
        """
        try:
            r = subprocess.run(
                ["readelf", "-p", ".comment", self.exe],
                capture_output=True,
                text=True,
                check=True,
            )
        except (OSError, subprocess.CalledProcessError):
            return "?"
        seen = []
        for line in r.stdout.splitlines():
            m = re.match(r"\s*\[\s*\w+\]\s+(.*\S)", line)
            if m and m.group(1) not in seen:
                seen.append(m.group(1))
        return " | ".join(seen) or "?"

    def size(self):
        return os.path.getsize(self.exe)


#
# Measurement
#


def measure(variant, wl, taskset, timeout):
    """Run one workload once, return {label: rate}."""
    cmd = []
    if taskset:
        cmd += ["taskset", "-c", taskset]
    cmd += [variant.exe, wl["wasm"]] + wl.get("args", [])

    stdin = open(wl["stdin"], "rb") if "stdin" in wl else None
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
        fatal(f"{variant.name}/{wl['name']}: timed out after {timeout}s")
    finally:
        if stdin:
            stdin.close()

    if proc.returncode != 0:
        fatal(f"{variant.name}/{wl['name']}: exited with {proc.returncode}")

    text = proc.stdout.decode("utf-8", errors="replace")
    if "expect" in wl and wl["expect"] not in text:
        fatal(
            f"{variant.name}/{wl['name']}: output missing '{wl['expect']}'\n{text[-2000:]}"
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
            fatal(
                f"{variant.name}/{wl['name']}: no match for /{pattern}/\n{text[-2000:]}"
            )
        value = float(m.group(1))
        if value <= 0:
            fatal(
                f"{variant.name}/{wl['name']}: metric {label} is {value}; "
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


def report(variants, labels, samples, baseline):
    """samples[(variant, label)] = [rate per round]"""
    base = variants[baseline]

    name_w = max(len(l) for l in labels) + 1
    col_w = max(11, max(len(v.name) for v in variants) + 2)

    print()
    print(f"{ansi.BOLD}Median rate (higher is better){ansi.ENDC}")
    header = (
        " " * name_w + "".join(f"{v.name:>{col_w}}" for v in variants) + "   spread"
    )
    print(header)
    print("-" * len(header))
    for label in labels:
        row = f"{label:<{name_w}}"
        for v in variants:
            row += f"{fmt_rate(statistics.median(samples[(v.name, label)])):>{col_w}}"
        worst = max(spread(samples[(v.name, label)]) for v in variants)
        row += f"{worst:>8.1f}%"
        print(row)

    print()
    print(f"{ansi.BOLD}Paired ratio vs {base.name} (per-round, median){ansi.ENDC}")
    header = " " * name_w + "".join(f"{v.name:>{col_w}}" for v in variants)
    print(header)
    print("-" * len(header))

    totals = {v.name: [] for v in variants}
    for label in labels:
        row = f"{label:<{name_w}}"
        for v in variants:
            ratios = [
                a / b
                for a, b in zip(samples[(v.name, label)], samples[(base.name, label)])
            ]
            r = statistics.median(ratios)
            totals[v.name].append(r)
            if v is base:
                row += f"{'--':>{col_w}}"
            else:
                colour = ansi.OKGREEN if r > 1.005 else (ansi.FAIL if r < 0.995 else "")
                cell = f"{r:.3f}x"
                row += f"{colour}{cell:>{col_w}}{ansi.ENDC if colour else ''}"
        print(row)

    print("-" * len(header))
    row = f"{'geomean':<{name_w}}"
    for v in variants:
        if v is base:
            row += f"{'--':>{col_w}}"
        else:
            g = statistics.geometric_mean(totals[v.name])
            colour = ansi.OKGREEN if g > 1.005 else (ansi.FAIL if g < 0.995 else "")
            cell = f"{g:.3f}x"
            row += f"{colour}{cell:>{col_w}}{ansi.ENDC if colour else ''}"
    print(row)

    print()
    row = f"{'binary':<{name_w}}"
    for v in variants:
        row += f"{v.size() // 1024:>{col_w - 3},} KB"
    print(row)


def main():
    default_root = os.environ.get("WASM3_BENCH_DIR") or os.path.join(
        os.path.expanduser("~"), ".cache", "wasm3-bench"
    )

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--variant", action="append", metavar="NAME=SRC[:CMAKE]", default=[]
    )
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--filter", metavar="GLOB[,GLOB...]", default="*")
    parser.add_argument(
        "--cpu", metavar="LIST", help="pin runs to these cores (taskset)"
    )
    parser.add_argument("--jobs", type=int, default=os.cpu_count())
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--build-root", default=default_root)
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--verbose-build", action="store_true")
    parser.add_argument("--json", metavar="FILE")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    if args.list:
        for wl in WORKLOADS:
            for m in wl["metrics"]:
                print(f"{m[0]:<16} {wl['name']:<14} {m[1]}")
        return

    if not args.variant:
        fatal("no --variant given")

    os.makedirs(args.build_root, exist_ok=True)
    deps_dir = os.path.join(args.build_root, "_deps")

    variants = [Variant(s, args.build_root, deps_dir) for s in args.variant]
    if len(set(v.name for v in variants)) != len(variants):
        fatal("variant names must be unique")

    patterns = args.filter.split(",")
    workloads = [
        w
        for w in WORKLOADS
        if any(fnmatch.fnmatch(w["name"], p.strip()) for p in patterns)
    ]
    if not workloads:
        fatal(f"no workload matches '{args.filter}'")
    labels = [m[0] for w in workloads for m in w["metrics"]]

    print(f"{ansi.BOLD}Variants{ansi.ENDC}")
    for v in variants:
        print(f"  {v.describe()}")

    if not args.no_build:
        print(f"\n{ansi.BOLD}Building{ansi.ENDC} (into {args.build_root})")
        for v in variants:
            print(f"  {v.name} ...", end="", flush=True)
            t0 = time.perf_counter()
            v.build(args.jobs, args.verbose_build)
            print(f" {v.rev}  {v.size() // 1024} KB  ({time.perf_counter() - t0:.0f}s)")
            print(f"      {v.compiler()}")
    for v in variants:
        if not os.path.isfile(v.exe):
            fatal(f"{v.name}: no binary at {v.exe} (drop --no-build)")

    samples = {(v.name, label): [] for v in variants for label in labels}

    print(
        f"\n{ansi.BOLD}Running{ansi.ENDC} {len(labels)} metrics x {len(variants)} variants "
        f"x {args.rounds} rounds" + (f", pinned to cpu {args.cpu}" if args.cpu else "")
    )
    for r in range(args.rounds):
        # rotate the order so no variant is always first or always last
        order = variants[r % len(variants) :] + variants[: r % len(variants)]
        print(f"  round {r + 1}/{args.rounds}: ", end="", flush=True)
        for v in order:
            print(f"{v.name} ", end="", flush=True)
            for wl in workloads:
                for label, rate in measure(v, wl, args.cpu, args.timeout).items():
                    samples[(v.name, label)].append(rate)
        print()

    report(variants, labels, samples, baseline=0)

    if args.json:
        with open(args.json, "w") as f:
            json.dump(
                {
                    "variants": [
                        {
                            "name": v.name,
                            "source": v.source,
                            "cmake": v.cmake_args,
                            "rev": v.rev,
                            "size": v.size(),
                        }
                        for v in variants
                    ],
                    "samples": {f"{k[0]}/{k[1]}": v for k, v in samples.items()},
                },
                f,
                indent=2,
            )
        print(f"\nraw samples -> {args.json}")


if __name__ == "__main__":
    main()
