#!/usr/bin/env python3

import argparse
import os
import os.path
import subprocess
import glob
import sys
import json
import re
from pprint import pprint

#
# Utilities
#

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
    print(f"{ansi.WARNING}Warning:{ansi.ENDC} {msg}")

def run(cmd):
    return subprocess.check_output(cmd, shell=True)

def filename(p):
    _, fn = os.path.split(p)
    return fn

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

parser = argparse.ArgumentParser()
parser.add_argument("--exec", metavar="<interpreter>", default="../build/wasm3")
parser.add_argument("--show-logs", action="store_true")
parser.add_argument("--skip-crashes", action="store_true")
parser.add_argument("-v", "--verbose", action="store_true")
parser.add_argument("file", nargs='*')

args = parser.parse_args()
#sys.argv = sys.argv[:1]

stats = dotdict(modules=0, total=0, skipped=0, failed=0, crashed=0)

def runInvoke(test):
    wasm = os.path.relpath(os.path.join(coreDir, test.module), curDir)
    cmd = [args.exec, wasm, test.action.field]
    for arg in test.action.args:
        cmd.append(arg['value'])

    if args.verbose:
        print(f"Running {' '.join(cmd)}")

    wasm3 = subprocess.run(cmd, universal_newlines=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    output = (wasm3.stdout + wasm3.stderr).strip()

    # Parse the actual output
    actual = None
    if len(output) == 0 or wasm3.returncode < 0:
        stats.crashed += 1
        actual = "<Crashed>"
        if args.skip_crashes:
            stats.failed += 1
            return

    if not actual:
        result = re.findall(r'^Result: (.*?)$', "\n" + output + "\n", re.MULTILINE)
        if len(result) == 1:
            actual = "result " + result[0]
    if not actual:
        result = re.findall(r'^Error: \[trap\] (.*?) \(', "\n" + output + "\n", re.MULTILINE)
        if len(result) == 1:
            actual = "trap " + result[0]
    if not actual:
        result = re.findall(r'^Error: (.*?)$', "\n" + output + "\n", re.MULTILINE)
        if len(result) == 1:
            actual = "error " + result[0]
    if not actual:
        actual = "<No Result>"

    # Prepare the expected result
    expect = None
    if "expected" in test:
        if len(test.expected) == 0:
            expect = "result <Empty Stack>"
        elif len(test.expected) == 1:
            expect = "result " + str(test.expected[0]['value'])
        else:
            warning(f"Test {test.source} specifies multiple results")
            expect = "result <Multiple>"
    elif "expected_trap" in test:
        expect = "trap " + str(test.expected_trap)
    else:
        expect = "<Unknown>"

    if actual != expect:
        stats.failed += 1
        print(" ----------------------")
        print(f"Test:     {ansi.HEADER}{test.source}{ansi.ENDC} -> {' '.join(cmd)}")
        #print(f"RetCode:  {wasm3.returncode}")
        print(f"Expected: {ansi.OKGREEN}{expect}{ansi.ENDC}")
        print(f"Actual:   {ansi.WARNING}{actual}{ansi.ENDC}")
        if args.show_logs and len(output):
            print(f"Log:")
            print(output)
        #sys.exit(1)

if not os.path.isdir(coreDir):
    if not os.path.isdir(specDir):
        specTestsFetch()
    specTestsPreprocess()

jsonFiles = args.file if args.file else glob.glob(os.path.join(coreDir, "*.json"))
jsonFiles.sort()

for fn in jsonFiles:
    with open(fn) as f:
        data = json.load(f)

    wast_source = filename(data["source_filename"])
    wast_module = ""

    if wast_source in ["linking.wast", "exports.wast"]:
        stats.total += len(data["commands"])
        stats.skipped += len(data["commands"])
        warning(f"skipped {wast_source}")
        continue

    for cmd in data["commands"]:
        test = dotdict()
        test.source = wast_source + ":" + str(cmd["line"])
        test.module = wast_module
        test.type = cmd["type"]

        if test.type == "module":
            stats.modules += 1

            wast_module = cmd["filename"]

        elif (test.type == "action" or
                test.type == "assert_return" or
                test.type == "assert_trap" or
                test.type == "assert_exhaustion" or
                test.type == "assert_return_canonical_nan" or
                test.type == "assert_return_arithmetic_nan"):

            if args.verbose:
                print(f"Checking {test.source}")

            stats.total += 1

            if test.type == "assert_return":
                test.expected = cmd["expected"]
            elif test.type == "assert_trap":
                test.expected_trap = cmd["text"]
            else:
                stats.skipped += 1
                warning(f"skipped {test.source} {test.type}")
                continue

            test.action = dotdict(cmd["action"])
            if test.action.type == "invoke":
                runInvoke(test)
            else:
                raise(Exception(f"Unknown action: {action}."))

        elif test.type == "register":
            stats.skipped += 1

        elif (test.type == "assert_invalid" or
                test.type == "assert_malformed" or
                test.type == "assert_unlinkable" or
                test.type == "assert_uninstantiable"):

            test.module = cmd["filename"]
            stats.modules += 1
            stats.skipped += 1

        else:
            raise(Exception(f"Unknown command: {test}."))

pprint(stats)

if stats.failed > 0:
    failed = (stats.failed*100)/stats.total
    print(f"{ansi.FAIL}=======================")
    print(f" FAILED: {failed:.2f}%")
    if stats.crashed > 0:
        print(f" Crashed: {stats.crashed}")
    print(f"======================={ansi.ENDC}")
else:
    print(f"{ansi.OKGREEN}=======================")
    print(f" All tests OK")
    if stats.skipped > 0:
        print(f"{ansi.WARNING} (some tests skipped){ansi.OKGREEN}")
    print(f"======================={ansi.ENDC}")
