#!/usr/bin/env python3
"""
Wasm3 pre-push check.

Runs what CI runs for a single compiler, in CI's own order, against one local build:

    format -> build -> embed -> regression -> spec -> wasi

Each stage is a gate on the next: an unformatted tree is not worth building, a build
with a new warning is not worth testing, and the suites all need the binary the build
stage produces. So a stage that fails stops the run unless --keep-going says otherwise.

This is not the matrix. CI runs this sequence across 70+ configurations, and whole
classes of problem -- warnings a config hides, features a compiler lacks -- appear only
in one of them. See AGENTS.md for what stays out of reach of one machine.

Usage:
    python extra/check.py                          # every stage, in order
    python extra/check.py spec wasi                # only the named stages
    python extra/check.py --build-dir build-x      # use (and configure) another tree
    python extra/check.py --cmake=-DBUILD_WASI=simple   # extra configure arguments
    python extra/check.py --list                   # what the stages are
"""

import argparse
import os
import shlex
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST = os.path.join(ROOT, "test")

sys.path.insert(0, os.path.join(ROOT, "extra"))

from testutils import ansi


class StageError(Exception):
    """A stage failed. A message is optional: a command that failed has already said so."""


def run(cmd, cwd=ROOT):
    """Run a command, its output going straight to ours rather than being captured.

    The suites report progress as they go and print a summary that is worth reading in
    full; buffering it to re-print on failure would hide a run that is merely slow.
    """
    print(f"\n{ansi.BOLD}${ansi.ENDC} {shlex.join(cmd)}", flush=True)
    if subprocess.call(cmd, cwd=cwd) != 0:
        raise StageError()


def built(args, name):
    """Locate an executable the build stage produced.

    Single-config generators put it straight in the build tree; Visual Studio and Xcode
    add a per-configuration directory. Searching both beats asking the caller which
    generator configured a tree they may not have configured themselves.
    """
    for d in (
        args.build_dir,
        os.path.join(args.build_dir, "Release"),
        os.path.join(args.build_dir, "Debug"),
    ):
        for exe in (name, name + ".exe"):
            path = os.path.join(d, exe)
            if os.path.isfile(path):
                return os.path.abspath(path)
    raise StageError(f"no {name} in {args.build_dir} -- run the build stage first")


def stage_format(args):
    run(["codespell"])
    run([sys.executable, os.path.join("extra", "format.py"), "--check"])


def stage_build(args):
    # Configure on every run rather than only when the cache is missing. These two
    # options are what make the build the one CI makes, and re-running cmake is the only
    # way to correct a tree that was configured without them -- a cache carrying
    # BUILD_WERROR=OFF would otherwise turn the whole point of this stage off silently.
    run(
        [
            "cmake",
            "-S",
            ROOT,
            "-B",
            args.build_dir,
            "-DBUILD_TESTS=ON",
            "-DBUILD_WERROR=ON",
        ]
        + args.cmake
    )
    run(["cmake", "--build", args.build_dir])


def stage_embed(args):
    run([built(args, "m3_test")])
    run([built(args, "m3_test_reftypes")])


def stage_regression(args):
    run(
        [sys.executable, "run-regression-test.py", "--exec", built(args, "wasm3")],
        cwd=TEST,
    )


def stage_spec(args):
    # This runner splits --exec with shlex, so a Windows path's backslashes need quoting
    # to survive; the other two split on spaces and would take the quotes literally.
    # Hence the one path being passed two different ways.
    exe = shlex.quote(built(args, "wasm3")) + " --spec-repl"
    run([sys.executable, "run-spec-test.py", "--exec", exe], cwd=TEST)
    run([sys.executable, "run-spec-test.py", "--spec=wg-2.0", "--exec", exe], cwd=TEST)


def stage_wasi(args):
    run([sys.executable, "run-wasi-test.py", "--exec", built(args, "wasm3")], cwd=TEST)


# The order is the pipeline: a stage assumes every stage above it has passed. Naming
# stages on the command line selects from this sequence, it does not reorder it.
# fmt: off
STAGES = (
    ("format",      "codespell, then extra/format.py --check",         stage_format),
    ("build",       "cmake, with -Werror and the internal tests on",   stage_build),
    ("embed",       "the embedding API: m3_test, m3_test_reftypes",    stage_embed),
    ("regression",  "test/run-regression-test.py",                     stage_regression),
    ("spec",        "test/run-spec-test.py, wg-3.0 then wg-2.0",       stage_spec),
    ("wasi",        "test/run-wasi-test.py",                           stage_wasi),
)
# fmt: on

NAMES = [name for name, _, _ in STAGES]


def main():
    parser = argparse.ArgumentParser(
        description="Run the CI sequence against one local build.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="stages, in order:\n"
        + "".join(f"  {name:<12}{what}\n" for name, what, _ in STAGES),
    )
    parser.add_argument(
        "stages",
        nargs="*",
        metavar="<stage>",
        help=f"one or more of: {', '.join(NAMES)}",
    )
    parser.add_argument(
        "--build-dir",
        metavar="<dir>",
        default="build",
        help="build tree to configure and use",
    )
    parser.add_argument(
        "--cmake",
        metavar="<arg>",
        action="append",
        default=[],
        help="extra argument for the cmake configure step; repeatable",
    )
    parser.add_argument(
        "--keep-going",
        action="store_true",
        help="run the remaining stages after a failure",
    )
    parser.add_argument("--list", action="store_true", help="list the stages and exit")
    args = parser.parse_args()

    if args.list:
        for name, what, _ in STAGES:
            print(f"{name:<12}{what}")
        return 0

    unknown = set(args.stages) - set(NAMES)
    if unknown:
        parser.error(
            f"unknown stage(s) {', '.join(sorted(unknown))}; known: {', '.join(NAMES)}"
        )

    selected = [s for s in STAGES if not args.stages or s[0] in args.stages]

    results = []
    failed = False
    for name, _, fn in selected:
        print(f"\n{ansi.HEADER}{ansi.BOLD}=== {name} ==={ansi.ENDC}", flush=True)
        started = time.time()
        try:
            fn(args)
            outcome = f"{ansi.OKGREEN}ok{ansi.ENDC}"
        except StageError as e:
            if str(e):
                print(f"{ansi.FAIL}Error:{ansi.ENDC} {e}")
            outcome = f"{ansi.FAIL}FAILED{ansi.ENDC}"
            failed = True
        results.append((name, outcome, time.time() - started))
        if failed and not args.keep_going:
            break

    print()
    for name, outcome, elapsed in results:
        print(f"  {name:<12}{outcome:<22}{elapsed:6.1f}s")

    skipped = len(selected) - len(results)
    if skipped:
        print(f"  {skipped} stage(s) not reached")

    print(
        f"\n{ansi.FAIL if failed else ansi.OKGREEN}{ansi.BOLD}{'FAILED' if failed else 'OK'}{ansi.ENDC}"
    )
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
