#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy
# Usage:
#   ./run-spec-test.py
#   ./run-spec-test.py --spec=opam-1.1.1
#   ./run-spec-test.py .spec-v1.1/core/i32.json
#   ./run-spec-test.py .spec-v1.1/core/float_exprs.json --line 2070
#   ./run-spec-test.py .spec-v1.1/proposals/tail-call/*.json
#   ./run-spec-test.py --exec "../build-custom/wasm3 --spec-repl"
#   ./run-spec-test.py --no-validation   # skip the checks that invalid modules are rejected
#   ./run-spec-test.py --no-relax-nan    # compare NaN results by class, not just "is a NaN"
#
# Running WASI version with different engines:
#   cp ../build-wasi/wasm3.wasm ./
#   ./run-spec-test.py --exec "../build/wasm3 wasm3.wasm --spec-repl"
#   ./run-spec-test.py --exec "wasmtime --dir=. wasm3.wasm -- --spec-repl"
#   ./run-spec-test.py --exec "wasmer run --dir=. wasm3.wasm -- --spec-repl"
#   ./run-spec-test.py --exec "wasmer run --dir=. --backend=llvm wasm3.wasm -- --spec-repl"
#   ./run-spec-test.py --exec "wasmer-js run wasm3.wasm --dir=. -- --spec-repl"
#   ./run-spec-test.py --exec "wasirun wasm3.wasm --spec-repl"
#   ./run-spec-test.py --exec "wavm run --mount-root ./ wasm3.wasm -- --spec-repl"
#   ./run-spec-test.py --exec "iwasm --dir=. wasm3.wasm --spec-repl"
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

scriptDir = os.path.dirname(os.path.abspath(sys.argv[0]))
sys.path.append(os.path.join(scriptDir, '..', 'extra'))

from testutils import *
from pprint import pprint


#
# Args handling
#

parser = argparse.ArgumentParser()
# --spec-repl is --repl plus --compile: disabling lazy compilation makes a
# function body that fails validation get rejected at load, as the spec requires
parser.add_argument("--exec", metavar="<interpreter>", default="../build/wasm3 --spec-repl")
parser.add_argument("--spec",                          default="wg-3.0")
parser.add_argument("--timeout", type=int,             default=30)
parser.add_argument("--line", metavar="<source line>", type=int)
parser.add_argument("--all", action="store_true")
parser.add_argument("--no-validation", dest="validation", action="store_false",
                    help="skip assert_invalid/assert_malformed/assert_uninstantiable, "
                         "i.e. don't check that invalid modules are rejected")
parser.add_argument("--relax-nan", dest="relax_nan", action="store_true", default=True,
                    help="accept any NaN where a NaN is expected (default). Targets without "
                         "IEEE-754-2008 float hardware -- x87, ARM soft-float, MIPS with the "
                         "legacy -mnan encoding -- disagree with the spec on the quiet bit")
parser.add_argument("--no-relax-nan", dest="relax_nan", action="store_false",
                    help="compare NaN results by class (canonical/arithmetic/signaling)")
parser.add_argument("--show-logs", action="store_true")
parser.add_argument("--format", choices=["raw", "hex", "fp"], default="fp")
parser.add_argument("-v", "--verbose", action="store_true")
parser.add_argument("-s", "--silent", action="store_true")
parser.add_argument("file", nargs='*')

args = parser.parse_args()

if args.line:
    args.show_logs = True

#
# Utilities
#

log = open("spec-test.log","w+")
log.write("======================\n")

def warning(msg, force=False):
    log.write("Warning: " + msg + "\n")
    log.flush()
    if args.verbose or force:
        print(f"{ansi.WARNING}Warning:{ansi.ENDC} {msg}")

def fatal(msg):
    log.write("Fatal: " + msg + "\n")
    log.flush()
    print(f"{ansi.FAIL}Fatal:{ansi.ENDC} {msg}")
    sys.exit(1)
    
def safe_fn(fn):
    keepcharacters = (' ','.','_','-')
    return "".join(c for c in fn if c.isalnum() or c in keepcharacters).strip()

def binaryToFloat(num, t):
    if t == "f32":
        return struct.unpack('!f', struct.pack('!L', int(num)))[0]
    elif t == "f64":
        return struct.unpack('!d', struct.pack('!Q', int(num)))[0]
    else:
        fatal(f"Unknown type '{t}'")

def escape_str(s):
    if s == "":
        return r'\x00'

    if all((ord(c) < 128 and c.isprintable() and c not in " \n\r\t\\") for c in s):
        return s

    return '\\x' + '\\x'.join('{0:02x}'.format(x) for x in s.encode('utf-8'))

#
# Value format options
#

def formatValueRaw(num, t):
    return str(num)

def formatValueHex(num, t):
    if t == "f32" or t == "i32":
        return "{0:#0{1}x}".format(int(num), 8+2)
    elif t == "f64" or t == "i64":
        return "{0:#0{1}x}".format(int(num), 16+2)
    else:
        return str(num)

def formatValueFloat(num, t):
    if t == "f32":
        s = 6
    elif t == "f64":
        s = 10
    else:
        return str(num)

    result = "{0:.{1}f}".format(binaryToFloat(num, t), s).rstrip('0')
    if result.endswith('.'):
        result = result + '0'
    if len(result) > s*2:
        result = "{0:.{1}e}".format(binaryToFloat(num, t), s)
    return result

formaters = {
    'raw': formatValueRaw,
    'hex': formatValueHex,
    'fp':  formatValueFloat,
}
formatValue = formaters[args.format]

if args.format == "fp":
    print("When using fp display format, values are compared loosely (some tests may produce false positives)")

if args.relax_nan:
    print("NaN results are compared as 'any NaN' (--no-relax-nan compares canonical/arithmetic/signaling)")

#
# Spec tests preparation
#

spec_dir = os.path.join(".", ".spec-" + safe_fn(args.spec))

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
                zipInfo.filename = newpath
                zipFile.extract(zipInfo)

#
# Wasm3 REPL
#

from subprocess import Popen, STDOUT, PIPE
from threading import Thread
from queue import Queue, Empty

import shlex

class Wasm3():
    def __init__(self, exe):
        self.exe = exe
        self.p = None
        self.loaded = None
        self.timeout = args.timeout
        self.autorestart = True

        self.run()

    def run(self):
        if self.p:
            self.terminate()

        cmd = shlex.split(self.exe)

        #print(f"wasm3: Starting {' '.join(cmd)}")

        self.q = Queue()
        self.p = Popen(cmd, bufsize=0, stdin=PIPE, stdout=PIPE, stderr=STDOUT)

        def _read_output(out, queue):
            for data in iter(lambda: out.read(1024), b''):
                queue.put(data)
            queue.put(None)

        self.t = Thread(target=_read_output, args=(self.p.stdout, self.q))
        self.t.daemon = True
        self.t.start()

        try:
            self._read_until("wasm3> ")
        except Exception as e:
            print(f"wasm3: Could not start: {e}")

    def restart(self):
        print(f"wasm3: Restarting")
        for i in range(10):
            try:
                self.run()
                try:
                    if self.loaded:
                        self.load(self.loaded)
                except Exception as e:
                    pass
                break
            except Exception as e:
                print(f"wasm3: {e} => retry")
                time.sleep(0.1)

    def init(self):
        return self._run_cmd(f":init\n")

    def version(self):
        return self._run_cmd(f":version\n")

    def load(self, fn):
        self.loaded = None
        with open(fn,"rb") as f:
            wasm = f.read()
        res = self._run_cmd(f":load-hex {len(wasm)}\n{wasm.hex()}\n")
        self.loaded = fn
        return res

    def invoke(self, cmd, module=None):
        if module:
            return self._run_cmd(":invoke-in " + module + " " + " ".join(map(str, cmd)) + "\n")
        return self._run_cmd(":invoke " + " ".join(map(str, cmd)) + "\n")

    def get_global(self, name, module=None):
        if module:
            return self._run_cmd(":get-global-in " + module + " " + name + "\n")
        return self._run_cmd(":get-global " + name + "\n")

    def register(self, name, module=None):
        if module:
            return self._run_cmd(":register " + name + " " + module + "\n")
        return self._run_cmd(":register " + name + "\n")

    def name_module(self, module):
        return self._run_cmd(":name " + module + "\n")

    def _run_cmd(self, cmd):
        if self.autorestart and not self._is_running():
            self.restart()
        self._flush_input()

        #print(f"wasm3: {cmd.strip()}")
        self._write(cmd)
        return self._read_until("wasm3> ")

    def _read_until(self, token):
        buff = ""
        tout = time.time() + self.timeout
        error = None

        while time.time() < tout:
            try:
                data = self.q.get(timeout=0.1)
                if data is None:
                    error = "Crashed"
                    break
                buff = buff + data.decode("utf-8")
                idx = buff.rfind(token)
                if idx >= 0:
                    return buff[0:idx].strip()
            except Empty:
                pass
        else:
            error = "Timeout"

        self.terminate()
        raise Exception(error)

    def _write(self, data):
        self.p.stdin.write(data.encode("utf-8"))
        self.p.stdin.flush()

    def _is_running(self):
        return self.p and (self.p.poll() is None)

    def _flush_input(self):
        while not self.q.empty():
            self.q.get()

    def terminate(self):
        self.p.stdin.close()
        self.p.terminate()
        self.p.wait(timeout=1.0)
        self.p = None

#
# Multi-value result handling
#

def parseResults(s, expected=None):
    values = s.split(", ")
    values = [x.split(":") for x in values]
    # reference results print as "null" rather than a number
    values = [{ "type": x[1], "value": x[0] if x[0] == "null" else int(x[0]) } for x in values]

    return normalizeResults(values, expected)

#
# NaN results are compared by class, not by exact bits: the spec leaves the sign
# of a produced NaN non-deterministic, so only the payload is meaningful.
#
# Under --relax-nan (the default) even the class is dropped and any NaN matches
# any other. wasm3 evaluates float ops as plain C expressions and libm calls, so
# NaN handling is whatever the toolchain does, and the usual divergence is that
# a signaling NaN operand is propagated unquieted where the spec requires an
# arithmetic (quiet) NaN out.
#

NAN_PAYLOAD_MASK = { "f32": 0x007FFFFF, "f64": 0x000FFFFFFFFFFFFF }
NAN_QUIET_BIT    = { "f32": 0x00400000, "f64": 0x0008000000000000 }

def nanClass(v, t):
    # "nan:canonical" and "nan:arithmetic" arrive as labels from the spec tests,
    # anything else is a raw bit pattern that we classify the same way.
    if v == "nan:canonical" or v == "nan:arithmetic":
        return v
    payload = int(v) & NAN_PAYLOAD_MASK[t]
    if payload == NAN_QUIET_BIT[t]:
        return "nan:canonical"
    elif payload & NAN_QUIET_BIT[t]:
        return "nan:arithmetic"
    else:
        return "nan:signaling"

def isNan(v, t):
    if v == "nan:canonical" or v == "nan:arithmetic":
        return True
    return math.isnan(binaryToFloat(v, t))

# wasm 3.0 states some reference results without a value: "refnull" and the
# null<T>ref types mean "a null reference" whose exact type is left open, and a
# bare "funcref" means "any non-null function reference".
REF_NULL_TYPES = ("refnull", "nullref", "nullfuncref", "nullexternref", "nullexnref")

VALUE_BITS = { "i32": 0xFFFFFFFF, "i64": 0xFFFFFFFFFFFFFFFF,
               "f32": 0xFFFFFFFF, "f64": 0xFFFFFFFFFFFFFFFF }

def normalizeResults(values, expected=None):
    for i, x in enumerate(values):
        t = x["type"]

        # The handle a host hands back for a reference is its own business, so
        # wherever the test declines to name a value, both sides collapse to
        # "null" or "ref" under one "ref" type. A null where a non-null
        # reference was expected still mismatches, which is the point.
        exp = expected[i] if (expected and i < len(expected)) else x
        if "value" not in exp:
            isNull = t in REF_NULL_TYPES or x.get("value") == "null"
            x["value"] = "null" if isNull else "ref"
            x["type"]  = "ref"
            continue

        # wasm 3.0 states integer results as signed decimals ("-1") where the
        # older suites used the unsigned bit pattern, which is also what wasm3
        # prints. Compare them in one representation.
        v = x["value"]
        if t in VALUE_BITS and v not in ("nan:canonical", "nan:arithmetic"):
            v = x["value"] = int(v) & VALUE_BITS[t]

        if t == "f32" or t == "f64":
            if isNan(v, t):
                if args.relax_nan:
                    x["value"] = "nan"
                    continue

                cls = nanClass(v, t)
                exp = expected[i]["value"] if (expected and i < len(expected)) else None
                expCls = nanClass(exp, t) if (exp is not None and isNan(exp, t)) else None

                # A canonical NaN is also an arithmetic NaN.
                if cls == "nan:canonical" and expCls == "nan:arithmetic":
                    cls = "nan:arithmetic"

                # wasm3 keeps every float in one f64 register (_fp0, see
                # m3_exec_defs.h), so an f32 signaling NaN is quieted by the
                # float->double->float round trip -- even through operations
                # that should preserve bits (load, reinterpret, select, neg,
                # abs, copysign). Tolerate a quieted result where an f32
                # signaling NaN was expected. The opposite direction, a
                # signaling NaN where the spec requires an arithmetic one, is
                # still a failure, and f64 stays strict. Same root cause as the
                # f32.nonarithmetic_nan_bitpattern entry in the blacklist below.
                if t == "f32" and expCls == "nan:signaling" and cls != "nan:signaling":
                    cls = "nan:signaling"

                x["value"] = cls
            else:
                x["value"] = formatValue(v, t)
        else:
            x["value"] = formatValue(v, t)
    return values

def combineResults(values):
    values = [x["value"]+":"+x["type"] for x in values]
    return ", ".join(values)

#
# Actual test
#

wasm3 = Wasm3(args.exec)

wasm3_ver = wasm3.version()
print(wasm3_ver)

blacklist = Blacklist([
  # returns the bit pattern as i32, so it can't be compared by NaN class:
  # wasm3 quiets f32 signaling NaNs, see the note in normalizeResults()
  "float_exprs.wast:* f32.nonarithmetic_nan_bitpattern*",

  # the compact import encoding of a module whose imports wasm3 cannot satisfy
  "binary-compact-imports.wast:* binary-compact-imports.10.wasm *",

  # not a gap: wasm3 implements multi-value, which the older v1.1 testsuite
  # still expects to be rejected
  "* assert_invalid (invalid result arity)",
  # likewise for reference types, which lifted the one-table limit
  "* assert_invalid (multiple tables)",
  # and for multiple memories, which lifted the one-memory limit and turned the
  # reserved zero byte of the memory instructions into a memory index. A
  # non-minimal LEB encoding of index 0 is a valid index, so these modules are
  # no longer malformed - wasm3 still rejects an index that names a memory the
  # module does not have, which is what wg-3.0's binary.wast checks.
  # wasm 2.0 made instantiation apply element and data segments one at a time,
  # so whatever ran before one of them traps stays applied - and the 2.0/3.0
  # linking.wast checks exactly that. The 1.1 suites still expect a failed
  # instantiation to leave nothing behind. (These two .wasm files are only ever
  # assert_unlinkable/assert_uninstantiable filenames in the newer suites, never
  # the module an action runs against, so naming them here is unambiguous.)
  "linking.wast:* linking.14.wasm call(7)",
  "linking.wast:* linking.24.wasm load(0)",

  "* assert_invalid (multiple memories)",
  "binary.wast:* assert_malformed (zero flag expected)",
  "binary.wast:* assert_malformed (zero byte expected)",
  # and for memory64, which widened the memarg offset from u32 to u64. Two
  # modules per suite encode a small offset in six LEB bytes: one too many for a
  # u32, and well within a u64, so they are no longer malformed. An offset a
  # 32-bit memory cannot address is still rejected - as invalid rather than
  # malformed, which is what the memory64 suite expects - and wg-3.0 no longer
  # asserts these. The same two modules are binary-leb128.wast in some suites
  # and binary.wast in others.
  "binary-leb128.wast:* binary-leb128.40.wasm *",
  "binary-leb128.wast:* binary-leb128.43.wasm *",
  "binary.wast:* binary.80.wasm *",
  "binary.wast:* binary.83.wasm *",
  # Linking is best-effort: an import nothing satisfies is left alone rather than
  # rejected, because m3_LinkRawFunction needs the runtime and so can only run
  # after m3_LoadModule. So a missing or mistyped import is not reported at
  # instantiation, which is when these expect it.
  "* assert_unlinkable (unknown import)",
  "* assert_unlinkable (incompatible import type)",

  # the spec's "spectest" module is faked with host functions rather than being
  # a real module, so it exports no memory. These grow a memory imported from
  # it, and get the local stand-in built from the import's own limits instead.
  "imports4.wast:* imports4.1.wasm grow(*)",

  # The repl talks in whitespace-separated tokens, so a module registered under
  # the empty name has nothing to send: the token disappears. Any placeholder
  # would be a string some other test could legitimately register under, so the
  # one test that does this is left out instead.
  "imports-compact.wast:* imports-compact.25.wasm call-empty()",

  # names containing NUL bytes are valid UTF-8 but wasm3 uses C strings
  # internally, so embedded NUL truncates the name during function lookup
  "names.wast:* *.wasm \\x00*",

  # exception handling: a tag import is a fresh tag, not an alias of the
  # exporting module's, since wasm3 does not link modules to each other. An
  # exception thrown through an imported tag therefore never carries the tag a
  # catch clause names, and a mismatched imported tag type goes unnoticed.
  "try_table.wast:* try_table.*.wasm catch-imported()",
  "try_table.wast:* try_table.*.wasm catch-imported-alias()",

  # one try_table module spells its types as (ref $t) and (ref exn), which need
  # typed function references and the exn heap type. Neither is implemented, so
  # the module is refused before any of its functions run.
  "try_table.wast:* try_table.*.wasm catch()",
  "try_table.wast:* try_table.*.wasm catch_ref1()",
  "try_table.wast:* try_table.*.wasm catch_ref2()",
  "try_table.wast:* try_table.*.wasm catch_all_ref1()",
  "try_table.wast:* try_table.*.wasm catch_all_ref2()",
])

# wasm3 takes any power-of-two page size from 1 to 65536, where the custom page
# sizes proposal admits only the two endpoints. Nothing in the engine cares which
# power of two a page is, so the ones in between are accepted rather than
# refused, and the tests asserting they are refused don't apply. Modules 2 to 16
# are 2^1 through 2^15; the ceiling itself still holds, and modules 17 and 18 ask
# for 2^17 and 2^65 and are still rejected.
blacklist.add([
  f"custom-page-sizes-invalid.wast:* custom-page-sizes-invalid.{n}.wasm *"
  for n in range(2, 17)
])

# Wasm 3.0 folded several proposals into the core suite. wasm3 implements typed
# function references, but not all of it and not the proposals that arrived with
# it, so a handful of modules are still refused or answer differently. Skipped by
# module rather than by file: the rest of each file runs.
if args.spec == "wg-3.0":
    blacklist.add([
      # br_on_null / br_on_non_null (0xd5, 0xd6) are not implemented: they branch
      # on the null-ness of a reference and pass it on non-null, which needs the
      # branch to carry a different stack shape than the fall-through
      "br_on_null.wast:* br_on_null.0.wasm *",
      "br_on_null.wast:* br_on_null.2.wasm *",
      "br_on_non_null.wast:* br_on_non_null.0.wasm *",
      "br_on_non_null.wast:* br_on_non_null.2.wasm *",

      # local initialization is not tracked, so a local whose type has no default
      # is accepted when read before it is set, instead of being rejected
      "local_init.wast:* local_init.0.wasm *",
      "local_init.wast:* local_init.1.wasm *",
      "local_init.wast:* local_init.2.wasm *",
      "local_init.wast:* local_init.4.wasm *",
      "local_init.wast:* local_init.5.wasm *",
      "func.wast:* func.21.wasm *",

      # the validator reasons in storage types, so it does not reject a nullable
      # reference where a non-nullable one is required
      "br_if.wast:* br_if.30.wasm *",
      "local_tee.wast:* local_tee.36.wasm *",
      "ref_as_non_null.wast:* ref_as_non_null.1.wasm *",

      # garbage collection: the abstract heap types (any, eq, i31, struct, array,
      # none) and exception handling's exnref
      "ref_null.wast:* ref_null.0.wasm *",
      "ref_null.wast:* ref_null.1.wasm *",
      "ref_is_null.wast:* ref_is_null.0.wasm *",

      # recursive type groups, also from garbage collection
      "type-rec.wast:* type-rec.17.wasm *",
      "type-rec.wast:* type-rec.18.wasm *",
      "type-rec.wast:* type-rec.19.wasm *",
      "type-equivalence.wast:* type-equivalence.8.wasm *",
      "type-equivalence.wast:* type-equivalence.9.wasm *",

      # a tag import is a fresh tag rather than an alias (see the exception
      # handling entries above), so a tag imported at the wrong type is accepted
      "tag.wast:* tag.7.wasm assert_unlinkable (incompatible import)",

      # multiple memories
      "instance.wast:* instance.1.wasm *",
      "instance.wast:* instance.2.wasm *",
      "instance.wast:* instance.4.wasm *",
      "custom-page-sizes.wast:* custom-page-sizes.6.wasm *",

      # wasm 3.0 lets a constant expression read any global declared before it,
      # where wasm3 still allows only imported ones
      "global.wast:* global.50.wasm *",

      # wasm3 never links imported globals, so a table initialized from one fills
      # with null - the same gap as the elem.wast entry above
      "table.wast:* table.33.wasm get4*",
      "table.wast:* table.33.wasm get5*",

      # a reference is an opaque host handle to wasm3, so a test that pins which
      # function a funcref names can't be matched against what the repl prints
      "br_table.wast:* * meet-funcref*",
      "select.wast:* * join-funcnull(1)",
    ])

    if (wasm3_ver not in Blacklist(["*typed-refs*"])):
        blacklist.add([
            "br_table.wast:* br_table.0.wasm *",
            "call_ref.wast:* call_ref.0.wasm *",
            "call_ref.wast:* call_ref.1.wasm *",
            "call_ref.wast:* call_ref.2.wasm *",
            "call_ref.wast:* call_ref.3.wasm *",
            "return_call_ref.wast:* return_call_ref.0.wasm *",
            "return_call_ref.wast:* return_call_ref.8.wasm *",
            "return_call_ref.wast:* return_call_ref.9.wasm *",
            "return_call_ref.wast:* return_call_ref.10.wasm *",
            "ref_as_non_null.wast:* ref_as_non_null.0.wasm *",
            "table.wast:* table.33.wasm *",
            "type-equivalence.wast:* type-equivalence.7.wasm *",
            "unreached-valid.wast:* * call_ref-unreached*",
        ])

if args.spec in ("v1.1", "opam-1.1.1"):
    # not a gap: wasm 2.0 gave validation a bottom type, so a br_table in
    # unreachable code may name targets whose result types differ. The pre-2.0
    # suites still expect this module to be rejected; 2.0 moved the very same
    # module to unreached-valid.wast as "meet-bottom" and expects it to run
    blacklist.add([
      "unreached-invalid.wast:539 unreached-invalid.87.wasm assert_invalid (type mismatch)",
    ])

if wasm3_ver in Blacklist(["* on i386* MSVC *", "* on i386* Clang * for Windows"]):
    warning("Win32 x86 has i64->f32 conversion precision issues, skipping some tests", True)
    # See: https://docs.microsoft.com/en-us/cpp/c-runtime-library/floating-point-support
    blacklist.add([
      "conversions.wast:* f32.convert_i64_u(9007199791611905)",
      "conversions.wast:* f32.convert_i64_u(9223371761976868863)",
      "conversions.wast:* f32.convert_i64_u(9223372586610589697)",
      "conversions.wast:* i64.trunc_f64_u(4895412794951729151)",
      "conversions.wast:* i64.trunc_sat_f64_u(4895412794951729151)",
    ])
elif wasm3_ver in Blacklist(["* on mips* GCC *"]):
    warning("MIPS has NaN representation issues, skipping some tests", True)
    blacklist.add([
      "float_exprs.wast:* *_nan_bitpattern(*",
      "float_exprs.wast:* *no_fold_*",
    ])
elif wasm3_ver in Blacklist(["* on sparc* GCC *"]):
    warning("SPARC has NaN representation issues, skipping some tests", True)
    blacklist.add([
      "float_exprs.wast:* *.canonical_nan_bitpattern(0, 0)",
    ])

# Wasm3 keeps f32 and f64 in one shared f64 register, so an f32 routed through it is
# promoted and demoted, which quiets a signaling NaN (the same gap as the blacklisted
# float_exprs nonarithmetic_nan_bitpattern tests). Natively these cases happen to stay in
# slots, but once Wasm3 is itself compiled to Wasm its f32 ops become guest f32 loads, and
# the hosting Wasm3 does route those through the register. Self-hosting is affected.
if wasm3_ver in Blacklist(["* self-hosting *"]):
    warning("self-hosting: the hosting Wasm3 quiets f32 signaling NaNs, skipping some tests", True)
    blacklist.add([
      "conversions.wast:* i32.reinterpret_f32(2141192192)",
      "conversions.wast:* i32.reinterpret_f32(4288675840)",
      # the i32.load right after "f32.store" of an f32 sNaN; the other i32.loads in
      # these modules read back a data segment or an i32.store, and are unaffected
      "float_memory.wast:21 *",
      "float_memory.wast:73 *",
    ])

# Without the "tail-call" build feature, return_call still runs correctly, it just doesn't
# reuse the caller's frame, so the tests that recurse a million deep trap instead of
# completing. Reusing it needs the compiler to guarantee a tail call, i.e. to support
# musttail (Clang, and GCC from 15 on).
if wasm3_ver not in Blacklist(["*tail-call*"]):
    warning("build has no guaranteed tail calls, skipping unbounded tail recursion", True)
    blacklist.add([
      "return_call.wast:* count(1000000)",
      "return_call.wast:* even(1000000)",
      "return_call.wast:* even(1000001)",
      "return_call.wast:* odd(1000000)",
      "return_call.wast:* odd(999999)",
      "return_call_indirect.wast:* even(100000)",
      "return_call_indirect.wast:* even(111111)",
      "return_call_indirect.wast:* odd(200002)",
      "return_call_indirect.wast:* odd(300003)",
      "return_call_ref.wast:* count(1000000)",
      "return_call_ref.wast:* even(1000000)",
      "return_call_ref.wast:* even(1000001)",
      "return_call_ref.wast:* odd(1000000)",
      "return_call_ref.wast:* odd(999999)",
    ])

stats = dotdict(total_run=0, skipped=0, failed=0, crashed=0, timeout=0,  success=0, missing=0)

# Convert some trap names from the original spec
trapmap = {
  "unreachable": "unreachable executed",
  # the bulk-memory suite appends the offending index to this one trap text
  "uninitialized element 2": "uninitialized element",
  # wasm 2.0 renamed three call_indirect traps; wasm3 uses the newer wording, so
  # the v1.1 and opam suites spell them the old way
  "uninitialized": "uninitialized element",
  "undefined": "undefined element",
  "indirect call": "indirect call type mismatch",
}

def runInvoke(test):
    test.cmd = [test.action.field]

    displayArgs = []
    for arg in test.action.args:
        test.cmd.append(arg['value'])
        displayArgs.append(formatValue(arg['value'], arg['type']))

    test_id = f"{test.source} {test.wasm} {test.cmd[0]}({', '.join(test.cmd[1:])})"
    if test_id in blacklist and not args.all:
        warning(f"Skipped {test_id} (blacklisted)")
        stats.skipped += 1
        return

    if args.verbose:
        print(f"Running {test_id}")

    stats.total_run += 1

    output = ""
    actual = None
    actual_val = None
    force_fail = False

    try:
        if test.action.type == "get":
            output = wasm3.get_global(test.cmd[0], test.action.module)
        else:
            output = wasm3.invoke(test.cmd, test.action.module)
    except Exception as e:
        actual = f"<{e}>"
        force_fail = True

    # Parse the actual output
    if not actual:
        result = re.findall(r'Result: (.*?)$', "\n" + output + "\n", re.MULTILINE)
        if len(result) > 0:
            actual = "result " + result[-1]
            actual_val = result[0]
    if not actual:
        result = re.findall(r'Error: \[trap\] (.*?) \(', "\n" + output + "\n", re.MULTILINE)
        if len(result) > 0:
            actual = "trap " + result[-1]
    if not actual:
        result = re.findall(r'Error: (.*?)$', "\n" + output + "\n", re.MULTILINE)
        if len(result) > 0:
            actual = "error " + result[-1]
    if not actual:
        actual = "<No Result>"
        force_fail = True

    if actual == "error no operation ()":
        actual = "<Not Implemented>"
        stats.missing += 1
        force_fail = True
    elif actual == "<Crashed>":
        stats.crashed += 1
        force_fail = True
    elif actual == "<Timeout>":
        stats.timeout += 1
        force_fail = True

    # Prepare the expected result
    expect = None
    if "expected" in test:
        if len(test.expected) == 0:
            expect = "result <Empty Stack>"
        else:
            if actual_val is not None:
                # normalize the actual result first: it needs the raw expected
                # values, which normalizeResults() rewrites in place below
                actual = "result " + combineResults(parseResults(actual_val, test.expected))
            expect = "result " + combineResults(normalizeResults(test.expected))

    elif "expected_trap" in test:
        if test.expected_trap in trapmap:
            test.expected_trap = trapmap[test.expected_trap]

        expect = "trap " + str(test.expected_trap)
    elif "expected_anything" in test:
        expect = "<Anything>"
    else:
        expect = "<Unknown>"

    def showTestResult():
        print(" ----------------------")
        print(f"Test:     {ansi.HEADER}{test_id}{ansi.ENDC}")
        print(f"Args:     {', '.join(displayArgs)}")
        print(f"Expected: {ansi.OKGREEN}{expect}{ansi.ENDC}")
        print(f"Actual:   {ansi.WARNING}{actual}{ansi.ENDC}")
        if args.show_logs and len(output):
            print(f"Log:")
            print(output)

    log.write(f"{test.source}\t|\t{test.wasm} {test.action.field}({', '.join(displayArgs)})\t=>\t\t")
    if actual == expect or (expect == "<Anything>" and not force_fail):
        stats.success += 1
        log.write(f"OK: {actual}\n")
        if args.line:
            showTestResult()
    else:
        stats.failed += 1
        log.write(f"FAIL: {actual}, should be: {expect}\n")
        if args.silent:
            return

        showTestResult()
        #sys.exit(1)

def runValidation(test, cmd, wasm_dir, keep_modules=False):
    # assert_invalid/assert_malformed/assert_uninstantiable: the module must be
    # rejected at load. wasm3's error strings don't match the spec's, so we only
    # check that it *is* rejected; the expected text is kept for the log.
    #
    # Requires --exec to disable lazy compilation (--spec-repl), otherwise an
    # invalid function body is not seen until the function is first compiled.

    # Text modules would need a wat parser, only binaries can go to wasm3
    if cmd.get("module_type") != "binary":
        stats.skipped += 1
        warning(f"Skipped {test.source} ({test.type} of a text module)")
        return

    test.wasm = cmd["filename"]
    expected_text = cmd.get("text", "")
    # the expected error is part of the id so the blacklist can name a whole
    # class of check, which survives the module filenames shifting between
    # spec revisions
    test_id = f"{test.source} {test.wasm} {test.type} ({expected_text})"

    if test_id in blacklist and not args.all:
        warning(f"Skipped {test_id} (blacklisted)")
        stats.skipped += 1
        return

    if args.verbose:
        print(f"Running {test_id}")

    stats.total_run += 1

    detail = ""
    try:
        # Once modules are being kept, the runtime holds the registered ones the
        # rest of the file still needs - and an unlinkable/uninstantiable module
        # is only meaningful against them - so it must not be reset here.
        if not keep_modules:
            wasm3.init()
        detail = wasm3.load(os.path.join(wasm_dir, test.wasm))
        actual = "rejected" if detail else "accepted"
    except Exception as e:
        actual = "<Crashed>"
        detail = str(e)
        stats.crashed += 1

    log.write(f"{test.source}\t|\t{test.wasm} {test.type} ({expected_text})\t=>\t\t")
    if actual == "rejected":
        stats.success += 1
        log.write(f"OK: {detail}\n")
    else:
        stats.failed += 1
        log.write(f"FAIL: module {actual}, should be rejected: {expected_text}\n")
        if args.silent:
            return
        print(" ----------------------")
        print(f"Test:     {ansi.HEADER}{test_id}{ansi.ENDC}")
        print(f"Expected: {ansi.OKGREEN}rejected ({expected_text}){ansi.ENDC}")
        print(f"Actual:   {ansi.WARNING}{actual}{ansi.ENDC}")

# A build without the "multi-memory" feature rejects any module with more than
# one memory, so the whole proposal suite is out of reach for it. Its files share
# wast names with core/ (memory_grow.wast and friends), so they are dropped at
# discovery rather than by blacklisting.
hasMultiMemory = wasm3_ver in Blacklist(["*multi-memory*"])
if not hasMultiMemory:
    warning("build has no multiple memories, skipping the multi-memory suite", True)
    # these live in the compact-import suite but import two memories apiece, so
    # they need the feature just as much as the multi-memory suite does
    blacklist.add([
      "imports-compact.wast:* imports-compact.16.wasm sum()",
      "imports-compact.wast:* imports-compact.17.wasm sizes()",
      "imports-compact.wast:* imports-compact.18.wasm sizes()",
      "imports-compact.wast:* imports-compact.19.wasm sum()",
    ])

if args.file:
    jsonFiles = args.file
else:
    jsonFiles  = glob.glob(os.path.join(spec_dir, "core", "*.json"))

    # Directories holding tests for proposals wasm3 implements but that this
    # suite still keeps out of the core suite. Which ones exist depends on the
    # version - a proposal moves into core/ once it ships - so a glob that comes
    # up empty just means this suite already covers it in core/:
    #   up to v1.1: sign-extension, non-trapping float-to-int, tail call
    #   up to 2.0:  tail call, extended const
    #   3.0:        bulk memory moved to core/bulk-memory; exception handling
    #               moved to core/exceptions; custom page sizes is still a
    #               proposal
    for stage in ('proposals', 'core'):
        for subdir in (
            "sign-extension-ops",
            "nontrapping-float-to-int-conversions",
            "tail-call",
            "extended-const",
            "custom-page-sizes",
            "bulk-memory",
            "compact-import-section",
        ) + (("multi-memory",) if hasMultiMemory else ()):
            jsonFiles += glob.glob(os.path.join(spec_dir, stage, subdir, "*.json"))

    # Exception handling. 3.0 keeps it in core/exceptions; the earlier suites are irrelevant/outdated
    if args.spec == "wg-3.0":
        jsonFiles += glob.glob(os.path.join(spec_dir, "core", "exceptions", "*.json"))

jsonFiles = list(map(lambda x: os.path.relpath(x, scriptDir), jsonFiles))
jsonFiles.sort()

for fn in jsonFiles:
    with open(fn, encoding='utf-8') as f:
        data = json.load(f)

    wast_source = filename(data["source_filename"])
    wasm_module = ""

    # a validation test loads a module of its own, so the one under test has to
    # be put back before the next invoke
    reload_module = False

    # Once a module is registered, later ones import from it; once one is given
    # a name, a later action can address it. Either way the runtime has to keep
    # them all from that point on. Unqualified lookups hit the most recently
    # loaded module first.
    keep_modules = False

    print(f"Running {fn}")

    for cmd in data["commands"]:
        test = dotdict()
        test.line = int(cmd["line"])
        test.source = wast_source + ":" + str(test.line)
        test.wasm = wasm_module
        test.type = cmd["type"]

        if test.type == "module":
            wasm_module = cmd["filename"]

            if args.verbose:
                print(f"Loading {wasm_module}")

            try:
                wasm_fn = os.path.join(pathname(fn), wasm_module)

                if not keep_modules:
                    wasm3.init()

                res = wasm3.load(wasm_fn)
                if res:
                    warning(res)
                elif cmd.get("name"):
                    # the .wast gave this module a variable name; later actions
                    # address its exports by it, so it has to stay loaded
                    wasm3.name_module(cmd["name"])
                    keep_modules = True
            except Exception as e:
                pass #fatal(str(e))

        elif (  test.type == "action" or
                test.type == "assert_return" or
                test.type == "assert_trap" or
                test.type == "assert_exhaustion" or
                test.type == "assert_exception" or
                test.type == "assert_return_canonical_nan" or
                test.type == "assert_return_arithmetic_nan"):

            if args.line and test.line != args.line:
                continue

            if reload_module:
                reload_module = False
                if wasm_module:
                    try:
                        wasm3.init()
                        wasm3.load(os.path.join(pathname(fn), wasm_module))
                    except Exception:
                        pass

            if test.type == "action":
                test.expected_anything = True
            elif test.type == "assert_return":
                test.expected = cmd["expected"]
            elif test.type == "assert_return_canonical_nan":
                test.expected = cmd["expected"]
                test.expected[0]["value"] = "nan:canonical"
            elif test.type == "assert_return_arithmetic_nan":
                test.expected = cmd["expected"]
                test.expected[0]["value"] = "nan:arithmetic"
            elif test.type == "assert_trap":
                test.expected_trap = cmd["text"]
            elif test.type == "assert_exhaustion":
                test.expected_trap = "stack overflow"
            elif test.type == "assert_exception":
                # the exception handling proposal only asserts that something
                # was thrown and never caught, without naming it
                test.expected_trap = "uncaught exception"
            else:
                stats.skipped += 1
                warning(f"Skipped {test.source} ({test.type} not implemented)")
                continue

            test.action = dotdict(cmd["action"])
            if test.action.type in ("invoke", "get"):

                # an action may name the module whose export it means, which is
                # how a test reaches past the most recently loaded one
                if test.action.module:
                    test.action.module = escape_str(test.action.module)

                test.action.field = escape_str(test.action.field)
                test.action.args = test.action.get("args", [])

                runInvoke(test)
            else:
                stats.skipped += 1
                warning(f"Skipped {test.source} (unknown action type '{test.action.type}')")


        elif (test.type == "assert_invalid" or
              test.type == "assert_malformed" or
              test.type == "assert_uninstantiable" or
              test.type == "assert_unlinkable"):

            if args.line and test.line != args.line:
                continue

            if not args.validation:
                continue

            runValidation(test, cmd, pathname(fn), keep_modules)
            # the module under test was replaced in the runtime, so it has to go
            # back before the next action - unless modules are being kept, where
            # nothing was reset in the first place
            reload_module = not keep_modules

        elif test.type == "register":
            try:
                res = wasm3.register(cmd["as"], cmd.get("name"))
                if res:
                    warning(res)
                else:
                    keep_modules = True
            except Exception as e:
                warning(str(e))

        # Others - report as skipped
        else:
            stats.skipped += 1
            warning(f"Skipped {test.source} ('{test.type}' not implemented)")

if (stats.failed + stats.success) != stats.total_run:
    warning("Statistics summary invalid", True)

pprint(stats)

if stats.failed > 0:
    failed = (stats.failed*100)/stats.total_run
    print(f"{ansi.FAIL}=======================")
    print(f" FAILED: {failed:.2f}%")
    if stats.crashed > 0:
        print(f" Crashed: {stats.crashed}")
    print(f"======================={ansi.ENDC}")
    sys.exit(1)

elif stats.success > 0:
    print(f"{ansi.OKGREEN}=======================")
    print(f" {stats.success}/{stats.total_run} tests OK")
    if stats.skipped > 0:
        print(f"{ansi.WARNING} ({stats.skipped} tests skipped){ansi.OKGREEN}")
    print(f"======================={ansi.ENDC}")
    
elif stats.total_run == 0:
    print("Error: No tests run")
    sys.exit(1)

