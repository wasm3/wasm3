#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy
# Usage:
#   ./run-spec-test.py
#   ./run-spec-test.py --spec=opam-1.1.1
#   ./run-spec-test.py .spec-v1.1/core/i32.json
#   ./run-spec-test.py .spec-v1.1/core/float_exprs.json --line 2070
#   ./run-spec-test.py .spec-v1.1/proposals/tail-call/*.json
#   ./run-spec-test.py --exec "../build-custom/wasm3 --repl"
#
# Running WASI version with different engines:
#   cp ../build-wasi/wasm3.wasm ./
#   ./run-spec-test.py --exec "../build/wasm3 wasm3.wasm --repl"
#   ./run-spec-test.py --exec "wasmtime --dir=. wasm3.wasm -- --repl"
#   ./run-spec-test.py --exec "wasmer run --dir=. wasm3.wasm -- --repl"
#   ./run-spec-test.py --exec "wasmer run --dir=. --backend=llvm wasm3.wasm -- --repl"
#   ./run-spec-test.py --exec "wasmer-js run wasm3.wasm --dir=. -- --repl"
#   ./run-spec-test.py --exec "wasirun wasm3.wasm --repl"
#   ./run-spec-test.py --exec "wavm run --mount-root ./ wasm3.wasm -- --repl"
#   ./run-spec-test.py --exec "iwasm --dir=. wasm3.wasm --repl"
#

# TODO
# - Get more tests from: https://github.com/microsoft/ChakraCore/tree/master/test/WasmSpec
# - Fix "Empty Stack" check
# - Check Canonical NaN and Arithmetic NaN separately
# - Fix imports.wast

import argparse
import os, sys, glob, time
import subprocess
import json
import re
import struct
import math
import pathlib
from pprint import pprint

scriptDir = os.path.dirname(os.path.abspath(sys.argv[0]))
sys.path.append(os.path.join(scriptDir, '..', 'extra'))

from testutils import *


#
# Args handling
#

parser = argparse.ArgumentParser()
parser.add_argument("--exec", metavar="<interpreter>", default="../build/wasm3 --repl")
parser.add_argument("--spec",                          default="opam-1.1.1")
parser.add_argument("--timeout", type=int,             default=30)
parser.add_argument("--line", metavar="<source line>", type=int)
parser.add_argument("--all", action="store_true")
parser.add_argument("--show-logs", action="store_true")
parser.add_argument("--format", choices=["raw", "hex", "fp"], default="fp")
parser.add_argument("-v", "--verbose", action="store_true")
parser.add_argument("-s", "--silent", action="store_true")
parser.add_argument("file", nargs='*')

args = parser.parse_args()

#
# Utilities
#

def warning(msg, force=False):
    if args.verbose or force:
        print(f"{ansi.WARNING}Warning:{ansi.ENDC} {msg}", file=sys.stderr)

def safe_fn(fn):
    keepcharacters = (' ','.','_','-')
    return "".join(c for c in fn if c.isalnum() or c in keepcharacters).strip()

#
# Spec tests preparation
#

spec_dir = os.path.join(scriptDir, ".spec-" + safe_fn(args.spec))

if not (os.path.isdir(spec_dir)):
    from io import BytesIO
    from zipfile import ZipFile
    from urllib.request import urlopen

    officialSpec = f"https://github.com/wasm3/wasm-core-testsuite/archive/{args.spec}.zip"

    print(f"Downloading {officialSpec}")
    resp = urlopen(officialSpec)
    with ZipFile(BytesIO(resp.read())) as zipFile:
        for zipInfo in zipFile.infolist():
            if re.match(r".*-.*/.*/.*(\.wasm|\.json)", zipInfo.filename):
                parts = pathlib.Path(zipInfo.filename).parts
                newpath = str(pathlib.Path(*parts[1:-1]))
                newfn   = str(pathlib.Path(*parts[-1:]))
                ensure_path(os.path.join(spec_dir, newpath))
                newpath = os.path.join(spec_dir, newpath, newfn)
                zipInfo.filename = os.path.relpath(newpath)
                zipFile.extract(zipInfo)

if args.all:
    blacklist = []
else:
    wasm3 = Wasm3(args.exec, timeout=args.timeout)
    wasm3_ver = wasm3.version()

    blacklist = [
      "float_exprs.wast:* f32.nonarithmetic_nan_bitpattern*",
      "imports.wast:*",
      "names.wast:* *.wasm \\x00*", # names that start with '\0'
    ]

    if wasm3_ver in Blacklist(["* on i386* MSVC *", "* on i386* Clang * for Windows"]):
        warning("Win32 x86 has i64->f32 conversion precision issues, skipping some tests", True)
        # See: https://docs.microsoft.com/en-us/cpp/c-runtime-library/floating-point-support
        blacklist.extend([
          "conversions.wast:* f32.convert_i64_u(9007199791611905)",
          "conversions.wast:* f32.convert_i64_u(9223371761976868863)",
          "conversions.wast:* f32.convert_i64_u(9223372586610589697)",
        ])
    elif wasm3_ver in Blacklist(["* on mips* GCC *"]):
        warning("MIPS has NaN representation issues, skipping some tests", True)
        blacklist.extend([
          "float_exprs.wast:* *_nan_bitpattern(*",
          "float_exprs.wast:* *no_fold_*",
        ])
    elif wasm3_ver in Blacklist(["* on sparc* GCC *"]):
        warning("SPARC has NaN representation issues, skipping some tests", True)
        blacklist.extend([
          "float_exprs.wast:* *.canonical_nan_bitpattern(0, 0)",
        ])

if args.file:
    jsonFiles = args.file
else:
    jsonFiles  = [
        os.path.join(spec_dir, "core", "*.json"),
        os.path.join(spec_dir, "proposals", "sign-extension-ops", "*.json"),
        os.path.join(spec_dir, "proposals", "nontrapping-float-to-int-conversions", "*.json"),
    ]

jsonFiles = list(map(lambda x: os.path.relpath(x), jsonFiles))

cmd = [
    sys.executable, os.path.join(scriptDir, "run-wast.py"),
]

for argname in ["exec", "timeout", "line", "show_logs", "format", "verbose", "silent"]:
    if argname not in args:
        continue

    flagname = "--" + argname.replace("_", "-")
    arg = getattr(args, argname)
    if isinstance(arg, bool):
        if arg:
            cmd.append(flagname)
    elif arg is not None:
        cmd.extend([flagname, str(arg)])

for pattern in blacklist:
    cmd.extend(["--exclude", pattern])

cmd.extend(jsonFiles)

subprocess.run(cmd, check=True)
