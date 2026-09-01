#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy

import argparse
import contextlib
import json
import multiprocessing
import os
import shutil
import subprocess
import sys
from pathlib import Path

# fmt: off

musl_targets = [
    #{ "name": "linux-x86_64"    , "arch": "x86_64-linux-musl"           },
    #{ "name": "linux-i686"      , "arch": "i686-linux-musl"             , "skip_tests" : True},

    #{ "name": "win-i686"       , "arch": "i686-w64-mingw32"            },
    #{ "name": "win-x64"        , "arch": "x86_64-w64-mingw32"          },

    { "name": "linux-armv6"     , "arch": "arm-unknown-linux-musleabihf"      , "runner": "qemu-arm-static"       },
    { "name": "linux-armv7l"    , "arch": "armv7-unknown-linux-musleabihf"    , "runner": "qemu-arm-static"       },
    { "name": "linux-arm-sf"    , "arch": "arm-unknown-linux-musleabi"        , "runner": "qemu-arm-static"       },
    # clang's atomics do not compile libuv on mips, so these use gcc - which does
    # emit the interpreter's indirect tail calls there, unlike on ppc
    { "name": "linux-mipsel-sf" , "arch": "mipsel-unknown-linux-muslsf"       , "runner": "qemu-mipsel-static"    , "gcc": True },
    { "name": "linux-mipsel"    , "arch": "mipsel-unknown-linux-musl"         , "runner": "qemu-mipsel-static"    , "gcc": True },
    { "name": "linux-mips-sf"   , "arch": "mips-unknown-linux-muslsf"         , "runner": "qemu-mips-static"      , "gcc": True },
    { "name": "linux-mips"      , "arch": "mips-unknown-linux-musl"           , "runner": "qemu-mips-static"      , "gcc": True },
    { "name": "linux-mips64el"  , "arch": "mips64el-unknown-linux-musl"       , "runner": "qemu-mips64el-static"  , "gcc": True },
    { "name": "linux-mips64"    , "arch": "mips64-unknown-linux-musl"         , "runner": "qemu-mips64-static"    , "gcc": True },
    { "name": "linux-ppc"       , "arch": "powerpc-unknown-linux-musl"        , "runner": "qemu-ppc-static"       },
    { "name": "linux-ppc64"     , "arch": "powerpc64-unknown-linux-musl"      , "runner": "qemu-ppc64-static"     },

    { "name": "linux-aarch64"   , "arch": "aarch64-unknown-linux-musl"        , "runner": "qemu-aarch64-static"   },
    { "name": "linux-rv32"      , "arch": "riscv32-unknown-linux-musl"        , "runner": "qemu-riscv32-static"   },
    { "name": "linux-rv64"      , "arch": "riscv64-unknown-linux-musl"        , "runner": "qemu-riscv64-static"   },
    { "name": "linux-s390x"     , "arch": "s390x-ibm-linux-musl"              , "runner": "qemu-s390x-static"     },
    { "name": "linux-loong64"   , "arch": "loongarch64-unknown-linux-musl"    , "runner": "qemu-loong64-static"   },

    # clang-cross has no toolchain for either of these, so they come from musl-cross
    # instead - the same project's gcc build, tagged and laid out the same way
    { "name": "linux-m68k"      , "arch": "m68k-unknown-linux-musl"           , "runner": "qemu-m68k-static"      , "gcc": True },
    { "name": "linux-microblaze", "arch": "microblaze-xilinx-linux-musl"      , "runner": "qemu-microblaze-static", "gcc": True },
    { "name": "linux-sh4"       , "arch": "sh4-multilib-linux-musl"           , "runner": "qemu-sh4-static"       , "gcc": True },

    # No musl cross toolchain exists for alpha, so this one comes from the distro and
    # is built against glibc. CI installs the package named here; see the cross job.
    { "name": "linux-alpha"     , "arch": "alpha-linux-gnu"                   , "runner": "qemu-alpha-static"     , "apt": "gcc-alpha-linux-gnu", "cc": "alpha-linux-gnu-gcc", "nodist": True },

    # Coverage the cross-qemu CI job used to give. These reuse the toolchains above to
    # exercise a build option or an ABI, rather than to ship a binary, so "nodist" keeps
    # them out of the release archive.
    { "name": "linux-arm-u64"   , "arch": "armv7-unknown-linux-musleabihf"    , "runner": "qemu-arm-static"       , "cflags": "-Dd_m3Use32BitSlots=0", "nodist": True },
    { "name": "linux-aarch64-u64", "arch": "aarch64-unknown-linux-musl"       , "runner": "qemu-aarch64-static"   , "cflags": "-Dd_m3Use32BitSlots=0", "nodist": True },
    { "name": "linux-ppc-u64"   , "arch": "powerpc-unknown-linux-musl"        , "runner": "qemu-ppc-static"       , "cflags": "-Dd_m3Use32BitSlots=0", "nodist": True },
    { "name": "linux-ppc64-u64" , "arch": "powerpc64-unknown-linux-musl"      , "runner": "qemu-ppc64-static"     , "cflags": "-Dd_m3Use32BitSlots=0", "nodist": True },

    #{ "name": "linux-armv7l-vfpv3"      , "arch": "armv7l-linux-musleabihf" , "runner": "qemu-arm-static"       , "cflags": "-march=armv7-a -mfpu=vfpv3 -mthumb -Wa,-mimplicit-it=thumb" },
    #{ "name": "linux-mipsel-24kc-sf"    , "arch": "mipsel-linux-muslsf"     , "runner": "qemu-mipsel-static"    , "cflags": "-march=24kc" },

    { "name": "wasi-sdk-8"      , "arch": "wasi-sdk-8.0"  , "sdk": 8    , "runner": "wasmer" },
    { "name": "wasi-sdk-11"     , "arch": "wasi-sdk-11.0" , "sdk": 11   , "runner": "wasmer" },
    #{ "name": "wasi-sdk-16"     , "arch": "wasi-sdk-16.0" , "sdk": 16   , "runner": "wasmer" },
]

# fmt: on

VERBOSE = False
RETEST = False
REBUILD = False
NOTEST = False
NOBUILD = False
NOWASI = False

LOG_DIR = "build-cross/logs"

# Set per target while it is being worked on. A parallel run interleaves everything on
# the console and the targets share test/spec-test.log, so without this a failure in one
# of them leaves nothing to read afterwards.
LOGFILE = None


def run(cmd):
    if LOGFILE:
        with open(LOGFILE, "a", encoding="utf-8", errors="replace") as f:
            f.write(f"$ {cmd}\n")
            f.flush()
            subprocess.run(
                cmd, shell=True, check=True, stdout=f, stderr=subprocess.STDOUT
            )
    else:
        subprocess.run(cmd, shell=True, check=True, capture_output=not VERBOSE)


def build_target(target):
    """Build and test one target. Returns the stages that failed, empty when all is well."""
    global LOGFILE

    name = target["name"]
    if not VERBOSE:
        Path(LOG_DIR).mkdir(parents=True, exist_ok=True)
        LOGFILE = f"{LOG_DIR}/{name}.log"
        with contextlib.suppress(FileNotFoundError):
            Path(LOGFILE).unlink()

    try:
        if name.startswith("wasi"):
            if NOWASI:
                return []
            return build_wasi(target)
        elif target.get("apt"):
            return build_musl(target, cc=target["cc"])
        elif target.get("gcc"):
            return build_musl(
                target,
                cc=f".toolchains/{target['arch']}/bin/{target['arch']}-gcc",
                toolchain_src="https://github.com/cross-tools/musl-cross/releases/download/20260823",
                tar_name=f"{target['arch']}.tar.xz",
            )
        else:
            return build_musl(
                target,
                cc=f".toolchains/{target['arch']}/bin/{target['arch']}-clang",
                toolchain_src="https://github.com/cross-tools/clang-cross/releases/download/20260823",
                tar_name=f"{target['arch']}.tar.xz",
            )
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print(f"Building {name} target: failed{'' if VERBOSE else f' (see {LOGFILE})'}")
        if VERBOSE:
            print(e)
        return ["build"]


def build_wasi(target):
    WASI_VERSION = str(target["sdk"])
    WASI_VERSION_FULL = WASI_VERSION + ".0"
    WASI_SDK_PATH = f"{os.getcwd()}/.toolchains/{target['arch']}"
    if not NOBUILD and not Path(f"{WASI_SDK_PATH}/bin").exists():
        print(f"Downloading {target['name']} toolchain")
        WASI_TAR = f"wasi-sdk-{WASI_VERSION_FULL}-linux.tar.gz"
        run(f"""
            mkdir -p .toolchains
            cd .toolchains
            wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-{WASI_VERSION}/{WASI_TAR}
            tar xzf {WASI_TAR}
            rm {WASI_TAR}
            """)

    wasm3_binary = f"build-cross/wasm3-{target['name']}.wasm"

    if not NOBUILD and (REBUILD or not Path(wasm3_binary).exists()):
        build_dir = f"build-cross/{target['name']}/"
        print(f"Building {target['name']} target")
        run(f"""
            mkdir -p {build_dir}
            cd {build_dir}
            cmake -GNinja -DCMAKE_TOOLCHAIN_FILE="{WASI_SDK_PATH}/share/cmake/wasi-sdk.cmake" -DWASI_SDK_PREFIX="{WASI_SDK_PATH}" ../..
            cmake --build .
            cp wasm3.wasm ../wasm3-{target["name"]}.wasm
            """)

    return run_tests(
        wasm3_binary,
        target,
        f"{target['runner']} run --mapdir=/:. ../{wasm3_binary} --".strip(),
    )


def build_musl(target, cc, toolchain_src=None, tar_name=None):
    if not toolchain_src:
        # installed system-wide rather than unpacked here
        if not NOBUILD and shutil.which(cc) is None:
            raise FileNotFoundError(f"{cc} not found - install {target.get('apt', cc)}")
    elif not NOBUILD and not Path(cc).exists():
        url = f"{toolchain_src}/{tar_name}"
        print(f"Downloading {url}")
        run(f"""
            mkdir -p .toolchains
            cd .toolchains
            curl -O -L -C - {url}
            """)
        print(f"Extracting {target['name']} toolchain")
        run(f"""
            cd .toolchains
            tar xf {tar_name}
            rm {tar_name}
            """)

    if not "cflags" in target:
        target["cflags"] = ""
    if not "runner" in target:
        target["runner"] = ""

    wasm3_binary = f"build-cross/wasm3-{target['name']}"

    if not NOBUILD and (REBUILD or not Path(wasm3_binary).exists()):
        build_dir = f"build-cross/{target['name']}/"
        print(f"Building {target['name']} target")
        run(f"""
            mkdir -p {build_dir}
            cd {build_dir}
            export CC="{f"../../{cc}" if toolchain_src else cc}"
            export CFLAGS="-Dd_m3HasTypedRefs=1 {target["cflags"]}"
            export LDFLAGS="-static -s"
            cmake -GNinja -DBUILD_NATIVE=OFF ../..
            cmake --build .
            cp wasm3 ../wasm3-{target["name"]}
            """)

    return run_tests(
        wasm3_binary, target, f"{target['runner']} ../{wasm3_binary}".strip()
    )


def run_tests(wasm3_binary, target, wasm3_cmd):
    name = target["name"]
    failures = []

    if NOTEST or "skip_tests" in target:
        return failures

    if not Path(wasm3_binary).exists():
        print(f"Testing {name} target: {wasm3_binary} is missing - build it first")
        return ["build"]

    # the regression cases run first: they are the quickest of the three, and they
    # hold the memory64/table64 address-wrap tests, which is what the 32-bit
    # targets here are most likely to get wrong
    for stage, cmd in (
        ("regression", f'python3 run-regression-test.py --exec "{wasm3_cmd}"'),
        ("spec", f'python3 run-spec-test.py --exec "{wasm3_cmd} --spec-repl"'),
        ("wasi", f'python3 run-wasi-test.py --fast --exec "{wasm3_cmd}"'),
    ):
        ok_marker = Path(f"build-cross/{name}/.test-{stage}-ok")
        if not (RETEST or not ok_marker.exists()):
            continue
        with contextlib.suppress(FileNotFoundError):
            ok_marker.unlink()
        try:
            run(f"""
                cd test
                {cmd}
                """)
            print(f"Testing {name} target ({stage}): OK")
            ok_marker.touch()
        except subprocess.CalledProcessError as e:
            print(
                f"Testing {name} target ({stage}): failed{'' if VERBOSE else f' (see {LOGFILE})'}"
            )
            if VERBOSE:
                print(e)
            failures.append(stage)

    return failures


if __name__ == "__main__":
    # fmt: off
    parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter,
        epilog="A single --target streams its output; several targets each get "
               "build-cross/logs/<target>.log instead. Exits non-zero if any target failed, "
               "so CI can split the work into two steps: --notest to build, --nobuild to test.")
    parser.add_argument('-j','--jobs', type=int, metavar='N', default=multiprocessing.cpu_count(), help='parallel builds')
    parser.add_argument('-v','--verbose', action='store_true', help='stream output instead of writing per-target logs')
    parser.add_argument('-q','--quiet', action='store_true', help='write per-target logs even for a single target')
    parser.add_argument('--retest', action='store_true', help='force tests')
    parser.add_argument('--notest', action='store_true', help='skip tests (build only)')
    parser.add_argument('--nobuild', action='store_true', help='skip builds (test only)')
    parser.add_argument('--nowasi', action='store_true', help='skip WASI builds')
    parser.add_argument('--rebuild', action='store_true', help='force builds')
    parser.add_argument('--target', metavar='NAME')
    parser.add_argument('--list', action='store_true', help='print the target names as JSON and exit')
    args = parser.parse_args()
    # fmt: on

    if args.nowasi:
        musl_targets = [t for t in musl_targets if not t["name"].startswith("wasi")]

    if args.list:
        # so a CI matrix can be driven from this file rather than a copy of it
        print(
            json.dumps(
                [
                    {
                        "name": t["name"],
                        "dist": not t.get("nodist"),
                        "apt": t.get("apt", ""),
                    }
                    for t in musl_targets
                ]
            )
        )
        sys.exit(0)

    if args.target:
        musl_targets = [
            t for t in musl_targets if args.target in (t["name"], t["arch"])
        ]
        if not musl_targets:
            sys.exit(f"No such target: {args.target}")

    # One target is someone watching a single build, so let them see it happen. Many
    # targets interleave into noise, so those go to a log apiece.
    VERBOSE = args.verbose or (bool(args.target) and not args.quiet)
    RETEST = args.retest
    REBUILD = args.rebuild
    NOTEST = args.notest
    NOBUILD = args.nobuild
    NOWASI = args.nowasi

    if args.jobs <= 1 or len(musl_targets) == 1:
        results = [(t["name"], build_target(t)) for t in musl_targets]
    else:
        # imap keeps going after a target fails, where map would abandon the rest
        with multiprocessing.Pool(args.jobs) as p:
            results = list(
                zip(
                    [t["name"] for t in musl_targets],
                    p.imap(build_target, musl_targets),
                )
            )

    failed = [(name, stages) for name, stages in results if stages]
    if len(results) > 1:
        print("\n=== Summary ===")
        for name, stages in results:
            print(f"{name:<20} {', '.join(stages) + ' FAILED' if stages else 'ok'}")
    if failed:
        sys.exit(f"\n{len(failed)} of {len(results)} targets failed")
