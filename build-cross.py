#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy

import argparse
import subprocess
import multiprocessing
from pathlib import Path
import time

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
]

VERBOSE = False
RETEST = False

def run(cmd):
    subprocess.run(cmd, shell=True, check=True, capture_output=not VERBOSE)

def build_target(target):
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

    if not Path(wasm3_binary).exists():
        build_dir = f"build-cross/{target['name']}/"
        print(f"Building {target['name']} target")
        run(f"""
            mkdir -p {build_dir}
            cd {build_dir}
            export CC="../../{muslcc}"
            export CFLAGS="{target['cflags']}"
            export LDFLAGS="-static -s"
            cmake -DBUILD_NATIVE=OFF ../..
            cmake --build .
            cp wasm3 ../wasm3-{target['name']}
            """)

    if "skip_tests" in target:
        return

    wasm3_cmd = f"{target['runner']} ../{wasm3_binary}".strip()

    test_spec_ok = Path(f"build-cross/{target['name']}/.test-spec-ok")
    if RETEST or not test_spec_ok.exists():
        print(f"Testing {target['name']} target (spec)")
        run(f"""
            cd test
            python3 run-spec-test.py --exec "{wasm3_cmd} --repl"
            """)
        test_spec_ok.touch()

    test_wasi_ok = Path(f"build-cross/{target['name']}/.test-wasi-ok")
    if RETEST or not test_wasi_ok.exists():
        print(f"Testing {target['name']} target (WASI)")
        run(f"""
            cd test
            python3 run-wasi-test.py --fast --exec "{wasm3_cmd}"
            """)
        test_wasi_ok.touch()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-j','--jobs', type=int, metavar='N', default=multiprocessing.cpu_count(), help='parallel builds')
    parser.add_argument('-v','--verbose', action='store_true', help='verbose output')
    parser.add_argument('--retest', action='store_true', help='force tests')
    parser.add_argument('--target', metavar='NAME')
    args = parser.parse_args()

    if args.target:
        musl_targets = filter(lambda t: args.target in t['name'] or args.target in t['arch'], musl_targets)

    VERBOSE = args.verbose
    RETEST = args.retest

    p = multiprocessing.Pool(args.jobs)
    p.map(build_target, musl_targets)
