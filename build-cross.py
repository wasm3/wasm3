#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy

import argparse
import subprocess
import multiprocessing
from pathlib import Path
import time
import os
import contextlib

musl_targets = [
    { "name": "linux-x86_64"    , "arch": "x86_64-linux-musl"           },
    { "name": "linux-i686"      , "arch": "i686-linux-musl"             , "skip_tests" : True},

    #{ "name": "win-i686"       , "arch": "i686-w64-mingw32"            },
    #{ "name": "win-x64"        , "arch": "x86_64-w64-mingw32"          },

    { "name": "linux-aarch64"   , "arch": "aarch64-linux-musl"          , "runner": "qemu-aarch64-static"   },
    { "name": "linux-armv6"     , "arch": "armv6-linux-musleabihf"      , "runner": "qemu-arm-static"       },
    { "name": "linux-armv7l"    , "arch": "armv7l-linux-musleabihf"     , "runner": "qemu-arm-static"       },
    { "name": "linux-mipsel-sf" , "arch": "mipsel-linux-muslsf"         , "runner": "qemu-mipsel-static"    },
    { "name": "linux-mipsel"    , "arch": "mipsel-linux-musl"           , "runner": "qemu-mipsel-static"    },
    { "name": "linux-mips-sf"   , "arch": "mips-linux-muslsf"           , "runner": "qemu-mips-static"      },
    { "name": "linux-mips"      , "arch": "mips-linux-musl"             , "runner": "qemu-mips-static"      },
    { "name": "linux-mips64el"  , "arch": "mips64el-linux-musl"         , "runner": "qemu-mips64el-static"  },
    { "name": "linux-mips64"    , "arch": "mips64-linux-musl"           , "runner": "qemu-mips64-static"    },
    { "name": "linux-rv32"      , "arch": "riscv32-linux-musl"          , "runner": "qemu-riscv32-static"   },
    { "name": "linux-rv64"      , "arch": "riscv64-linux-musl"          , "runner": "qemu-riscv64-static"   },
    { "name": "linux-ppc"       , "arch": "powerpc-linux-musl"          , "runner": "qemu-ppc-static"       },
    { "name": "linux-ppc64"     , "arch": "powerpc64-linux-musl"        , "runner": "qemu-ppc64-static"     },
    { "name": "linux-s390x"     , "arch": "s390x-linux-musl"            , "runner": "qemu-s390x-static"     },
    #{ "name": "linux-m68k"      , "arch": "m68k-linux-musl"             , "runner": "qemu-m68k-static"      },
    #{ "name": "linux-microblaze", "arch": "microblaze-linux-musl"       , "runner": "qemu-microblaze-static" },

    { "name": "linux-armv7l-vfpv3"      , "arch": "armv7l-linux-musleabihf" , "runner": "qemu-arm-static"       , "cflags": "-march=armv7-a -mfpu=vfpv3 -mthumb -Wa,-mimplicit-it=thumb" },
    { "name": "linux-mipsel-24kc-sf"    , "arch": "mipsel-linux-muslsf"     , "runner": "qemu-mipsel-static"    , "cflags": "-march=24kc" },

    { "name": "wasi-sdk-8"      , "arch": "wasi-sdk-8.0"  , "sdk": 8    , "runner": "wasmer" },
    { "name": "wasi-sdk-11"     , "arch": "wasi-sdk-11.0" , "sdk": 11   , "runner": "wasmer" },
    #{ "name": "wasi-sdk-16"     , "arch": "wasi-sdk-16.0" , "sdk": 16   , "runner": "wasmer" },
]

VERBOSE = False
RETEST = False
REBUILD = False

def run(cmd):
    subprocess.run(cmd, shell=True, check=True, capture_output=not VERBOSE)

def build_target(target):
    if target['name'].startswith("wasi"):
        build_wasi(target)
    else:
        build_musl(target)

def build_wasi(target):
    WASI_VERSION = str(target['sdk'])
    WASI_VERSION_FULL = WASI_VERSION + ".0"
    WASI_SDK_PATH = f"{os.getcwd()}/.toolchains/{target['arch']}"
    if not Path(f"{WASI_SDK_PATH}/bin").exists():
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

    if REBUILD or not Path(wasm3_binary).exists():
        build_dir = f"build-cross/{target['name']}/"
        print(f"Building {target['name']} target")
        run(f"""
            mkdir -p {build_dir}
            cd {build_dir}
            cmake -GNinja -DCMAKE_TOOLCHAIN_FILE="{WASI_SDK_PATH}/share/cmake/wasi-sdk.cmake" -DWASI_SDK_PREFIX="{WASI_SDK_PATH}" ../..
            cmake --build .
            cp wasm3.wasm ../wasm3-{target['name']}.wasm
            """)

    run_tests(wasm3_binary, target, f"{target['runner']} run --mapdir=/:. ../{wasm3_binary} --".strip())

def build_musl(target):
    muslcc = f".toolchains/{target['arch']}-cross/bin/{target['arch']}-gcc"
    if not Path(muslcc).exists():
        print(f"Downloading {target['name']} toolchain")
        tar_name = f"{target['arch']}-cross.tgz"
        run(f"""
            mkdir -p .toolchains
            cd .toolchains
            curl -O -C - https://musl.cc/{tar_name}
            tar xzf {tar_name}
            rm {tar_name}
            """)

    if not 'cflags' in target:
        target['cflags'] = ""
    if not 'runner' in target:
        target['runner'] = ""

    wasm3_binary = f"build-cross/wasm3-{target['name']}"

    if REBUILD or not Path(wasm3_binary).exists():
        build_dir = f"build-cross/{target['name']}/"
        print(f"Building {target['name']} target")
        run(f"""
            mkdir -p {build_dir}
            cd {build_dir}
            export CC="../../{muslcc}"
            export CFLAGS="{target['cflags']}"
            export LDFLAGS="-static -s"
            cmake -GNinja -DBUILD_NATIVE=OFF ../..
            cmake --build .
            cp wasm3 ../wasm3-{target['name']}
            """)

    run_tests(wasm3_binary, target, f"{target['runner']} ../{wasm3_binary}".strip())

def run_tests(wasm3_binary, target, wasm3_cmd):
    if "skip_tests" in target:
        return

    test_spec_ok = Path(f"build-cross/{target['name']}/.test-spec-ok")
    if RETEST or not test_spec_ok.exists():
        with contextlib.suppress(FileNotFoundError):
            test_spec_ok.unlink()
        try:
            run(f"""
                cd test
                python3 run-spec-test.py --exec "{wasm3_cmd} --repl"
                """)
            print(f"Testing {target['name']} target (spec): OK")
            test_spec_ok.touch()
        except Exception as e:
            print(f"Testing {target['name']} target (spec): failed")
            print(e)
            pass

    test_wasi_ok = Path(f"build-cross/{target['name']}/.test-wasi-ok")
    if RETEST or not test_wasi_ok.exists():
        with contextlib.suppress(FileNotFoundError):
            test_wasi_ok.unlink()
        try:
            run(f"""
                cd test
                python3 run-wasi-test.py --fast --exec "{wasm3_cmd}"
                """)
            print(f"Testing {target['name']} target (WASI): OK")
            test_wasi_ok.touch()
        except Exception as e:
            print(f"Testing {target['name']} target (WASI): failed")
            print(e)
            pass

if __name__ == '__main__':
    parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-j','--jobs', type=int, metavar='N', default=multiprocessing.cpu_count(), help='parallel builds')
    parser.add_argument('-v','--verbose', action='store_true', help='verbose output')
    parser.add_argument('--retest', action='store_true', help='force tests')
    parser.add_argument('--rebuild', action='store_true', help='force builds')
    parser.add_argument('--target', metavar='NAME')
    args = parser.parse_args()

    if args.target:
        musl_targets = filter(lambda t: args.target in t['name'] or args.target in t['arch'], musl_targets)

    VERBOSE = args.verbose
    RETEST = args.retest
    REBUILD = args.rebuild

    if args.jobs <= 1:
        for t in musl_targets:
            build_target(t)
    else:
        p = multiprocessing.Pool(args.jobs)
        p.map(build_target, musl_targets)
