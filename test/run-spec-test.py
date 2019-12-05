#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy
# Usage:
#   ./run-spec-test.py
#   ./run-spec-test.py ./core/i32.json
#   ./run-spec-test.py ./core/float_exprs.json --line 2070
#   ./run-spec-test.py --exec ../custom_build/wasm3
#

# TODO
# - Get more tests from: https://github.com/microsoft/ChakraCore/tree/master/test/WasmSpec
# - Fix "Empty Stack" check
# - Check Canonical NaN and Arithmetic NaN separately

import argparse
import os
import os.path
import subprocess
import glob
import sys
import json
import re
import struct
import math

from pprint import pprint

#
# Args handling
#

parser = argparse.ArgumentParser()
parser.add_argument("--exec", metavar="<interpreter>", default="../build/wasm3")
parser.add_argument("--line", metavar="<source line>", type=int)
parser.add_argument("--all", action="store_true")
parser.add_argument("--show-logs", action="store_true")
parser.add_argument("--skip-crashes", action="store_true")
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

class ansi:
    ENDC = '\033[0m'
    HEADER = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

class dotdict(dict):
    def __init__(self, *args, **kwargs):
        super(dotdict, self).__init__(*args, **kwargs)
        for arg in args:
            if isinstance(arg, dict):
                for k, v in arg.items():
                    self[k] = v
        if kwargs:
            for k, v in kwargs.items():
                self[k] = v

    __getattr__ = dict.get
    __setattr__ = dict.__setitem__
    __delattr__ = dict.__delitem__

def warning(msg):
    log.write("Warning: " + msg + "\n")
    print(f"{ansi.WARNING}Warning:{ansi.ENDC} {msg}")

def run(cmd):
    return subprocess.check_output(cmd, shell=True)

def filename(p):
    _, fn = os.path.split(p)
    return fn

def binaryToFloat(num, t):
    if t == "f32":
        return struct.unpack('!f', struct.pack('!L', int(num)))[0]
    elif t == "f64":
        return struct.unpack('!d', struct.pack('!Q', int(num)))[0]
    else:
        raise(Exception(f"Unknown type: {t}"))

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
    if result.endswith('.'): result = result + '0'
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
    warning("When using fp display format, values are compared loosely (some tests may produce false positives)")

#
# Spec tests preparation
#

def specTestsFetch():
    from io import BytesIO
    from zipfile import ZipFile
    from urllib.request import urlopen

    officialSpec = "https://github.com/WebAssembly/spec/archive/wg_draft2.zip"

    print(f"Downloading {officialSpec}")
    resp = urlopen(officialSpec)
    with ZipFile(BytesIO(resp.read())) as zipFile:
        for zipInfo in zipFile.infolist():
            if re.match(r"spec-wg_draft2/test/core/.*", zipInfo.filename):
                zipInfo.filename = specDir + filename(zipInfo.filename)
                zipFile.extract(zipInfo)

def specTestsPreprocess():
    print("Preprocessing spec files...")

    inputFiles = glob.glob(os.path.join(specDir, "*.wast"))
    inputFiles.sort()
    for fn in inputFiles:
        fn = os.path.basename(fn)

        wast_fn = os.path.join(specDir, fn)
        json_fn = os.path.join(coreDir, os.path.splitext(fn)[0]) + ".json"
        run(f"wast2json --debug-names -o {json_fn} {wast_fn}")

#
# Actual test
#

curDir = os.path.dirname(os.path.abspath(sys.argv[0]))
coreDir = os.path.join(curDir, "core")
specDir = "core/spec/"



stats = dotdict(total_run=0, skipped=0, failed=0, crashed=0, success=0, missing=0)

# Convert some trap names from the original spec
trapmap = {
  "unreachable": "unreachable executed"
}

def runInvoke(test):
    wasm = os.path.relpath(os.path.join(coreDir, test.module), curDir)
    cmd = [args.exec, wasm, test.action.field]

    displayArgs = []
    for arg in test.action.args:
        cmd.append(arg['value'])
        displayArgs.append(formatValue(arg['value'], arg['type']))

    if args.verbose:
        print(f"Running {' '.join(cmd)}")

    try:
        wasm3 = subprocess.run(cmd, universal_newlines=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except ValueError:
        stats.skipped += 1
        return

    output = (wasm3.stdout + wasm3.stderr).strip()

    # Parse the actual output
    actual = None
    actual_val = None
    if len(output) == 0 or wasm3.returncode < 0:
        stats.crashed += 1
        actual = "<Crashed>"
    if not actual:
        result = re.findall(r'^Result: (.*?)$', "\n" + output + "\n", re.MULTILINE)
        if len(result) > 0:
            actual = "result " + result[-1]
            actual_val = result[0]
    if not actual:
        result = re.findall(r'^Error: \[trap\] (.*?) \(', "\n" + output + "\n", re.MULTILINE)
        if len(result) > 0:
            actual = "trap " + result[-1]
    if not actual:
        result = re.findall(r'^Error: (.*?)$', "\n" + output + "\n", re.MULTILINE)
        if len(result) > 0:
            actual = "error " + result[-1]
    if not actual:
        actual = "<No Result>"

    if actual == "error no operation ()":
        actual = "<Not Implemented>"
        stats.missing += 1

    # Prepare the expected result
    expect = None
    if "expected" in test:
        if len(test.expected) == 0:
            expect = "result <Empty Stack>"
        elif len(test.expected) == 1:
            t = test.expected[0]['type']
            value = str(test.expected[0]['value'])
            expect = "result " + value

            if actual_val != None:
                if (t == "f32" or t == "f64") and (value == "<Canonical NaN>" or value == "<Arithmetic NaN>"):
                    val = binaryToFloat(actual_val, t)
                    #warning(f"{actual_val} => {val}")
                    if math.isnan(val):
                        actual = "<Some NaN>"
                        expect = "<Some NaN>"
                else:
                    expect = "result " + formatValue(value, t)
                    actual = "result " + formatValue(actual_val, t)

        else:
            warning(f"Test {test.source} specifies multiple results")
            expect = "result <Multiple>"
    elif "expected_trap" in test:
        if test.expected_trap in trapmap:
            test.expected_trap = trapmap[test.expected_trap]

        expect = "trap " + str(test.expected_trap)
    else:
        expect = "<Unknown>"

    def showTestResult():
        print(" ----------------------")
        print(f"Test:     {ansi.HEADER}{test.source}{ansi.ENDC} -> {' '.join(cmd)}")
        print(f"Args:     {', '.join(displayArgs)}")
        #print(f"RetCode:  {wasm3.returncode}")
        print(f"Expected: {ansi.OKGREEN}{expect}{ansi.ENDC}")
        print(f"Actual:   {ansi.WARNING}{actual}{ansi.ENDC}")
        if args.show_logs and len(output):
            print(f"Log:")
            print(output)

    log.write(f"{test.source}\t|\t{filename(wasm)} {test.action.field}({', '.join(displayArgs)})\t=>\t\t")
    if actual == expect:
        stats.success += 1
        log.write(f"OK: {actual}\n")
        if args.line:
            showTestResult()
    else:
        stats.failed += 1
        log.write(f"FAIL: {actual}, should be: {expect}\n")
        if args.silent: return
        if args.skip_crashes and actual == "<Crashed>": return

        showTestResult()
        #sys.exit(1)

if not os.path.isdir(coreDir):
    if not os.path.isdir(specDir):
        specTestsFetch()
    specTestsPreprocess()

# Currently default to running the predefined list of tests
# TODO: Switch to running all tests when wasm spec is implemented

if args.file:
    jsonFiles = args.file
elif args.all:
    jsonFiles = glob.glob(os.path.join(coreDir, "*.json"))
    jsonFiles.sort()
else:
    jsonFiles = list(map(lambda x : f"./core/{x}.json", [
        #--- Complete ---
        "i32", "i64",
        "int_exprs",

        "f32", "f32_cmp", "f32_bitwise",
        "f64", "f64_cmp", "f64_bitwise",
        "float_misc",

        "conversions",
        "stack", "fac",
        "call", "call_indirect",
        "left-to-right",
        "break-drop",
        "forward",
        "func_ptrs",
        "endianness",
        "int_literals",

        #--- Almost ready ---
        #"memory_trap", "address",     -> init memory size + track memory bounds
        #"float_memory",
        #"memory_redundancy", "memory_grow",

        #--- TODO ---
        #"get_local", "set_local", "tee_local",
        #"if", "loop", "labels", "block", "br", "br_if", "br_table", "return",
        #"nop", "unreachable",
        #"align", "memory",
        #"float_literals",
        #"globals",
        #"func",
        #"float_exprs",
        #"elem",
        #"switch",
    ]))

for fn in jsonFiles:
    with open(fn) as f:
        data = json.load(f)

    wast_source = filename(data["source_filename"])
    wast_module = ""

    if wast_source in ["linking.wast", "exports.wast", "names.wast"]:
        count = len(data["commands"])
        stats.skipped += count
        warning(f"Skipped {wast_source} ({count} tests)")
        continue

    print(f"Running {fn}")

    for cmd in data["commands"]:
        test = dotdict()
        test.line = int(cmd["line"])
        test.source = wast_source + ":" + str(test.line)
        test.module = wast_module
        test.type = cmd["type"]

        if test.type == "module":
            wast_module = cmd["filename"]

        elif (  test.type == "action" or
                test.type == "assert_return" or
                test.type == "assert_trap" or
                test.type == "assert_exhaustion" or
                test.type == "assert_return_canonical_nan" or
                test.type == "assert_return_arithmetic_nan"):

            if args.line and test.line != args.line:
                continue

            if args.verbose:
                print(f"Checking {test.source}")

            if test.type == "assert_return":
                test.expected = cmd["expected"]
            elif test.type == "assert_return_canonical_nan":
                test.expected = cmd["expected"]
                test.expected[0]["value"] = "<Canonical NaN>"
            elif test.type == "assert_return_arithmetic_nan":
                test.expected = cmd["expected"]
                test.expected[0]["value"] = "<Arithmetic NaN>"
            elif test.type == "assert_trap":
                test.expected_trap = cmd["text"]
            else:
                stats.skipped += 1
                warning(f"Skipped {test.source} {test.type}")
                continue

            test.action = dotdict(cmd["action"])
            if test.action.type == "invoke":
                stats.total_run += 1
                runInvoke(test)
            else:
                stats.skipped += 1
                warning(f"Unknown action: {test.action}")
                continue

        elif (  test.type == "register" or
                test.type == "assert_invalid" or
                test.type == "assert_malformed" or
                test.type == "assert_unlinkable" or
                test.type == "assert_uninstantiable"):
            stats.skipped += 1
        else:
            raise(Exception(f"Unknown command: {test}"))

if (stats.failed + stats.success) != stats.total_run:
    warning("Statistics summary invalid")

pprint(stats)

if stats.failed > 0:
    failed = (stats.failed*100)/stats.total_run
    print(f"{ansi.FAIL}=======================")
    print(f" FAILED: {failed:.2f}%")
    if stats.crashed > 0:
        print(f" Crashed: {stats.crashed}")
    print(f"======================={ansi.ENDC}")
elif stats.success > 0:
    print(f"{ansi.OKGREEN}=======================")
    print(f" {stats.success}/{stats.total_run} tests OK")
    if stats.skipped > 0:
        print(f"{ansi.WARNING} ({stats.skipped} tests skipped){ansi.OKGREEN}")
    print(f"======================={ansi.ENDC}")
