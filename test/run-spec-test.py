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

def run(cmd):
    return subprocess.check_output(cmd, shell=True)

def filename(p):
    _, fn = os.path.split(p)
    return fn

curDir = os.path.dirname(os.path.abspath(sys.argv[0]))
coreDir = os.path.join(curDir, "core")
specDir = "core/spec/"

parser = argparse.ArgumentParser()
parser.add_argument("file", nargs='*')

arguments = parser.parse_args()
sys.argv = sys.argv[:1]

stats = dotdict(modules=0, total=0, skipped=0, failed=0, crashed=0)

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

def runInvoke(test):
    wasm = os.path.relpath(os.path.join(coreDir, test.module), curDir)
    cmd = ["../build/wasm3", wasm, test.action.field]
    for arg in test.action.args:
        cmd.append(arg['value'])

    wasm3 = subprocess.run(cmd, universal_newlines=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    output = (wasm3.stdout + wasm3.stderr).strip()

    def testFail(msg):
        print(" ----------------------")
        print("Source:  " + test.source)
        print("Command: " + ' '.join(cmd))
        print("Message: " + msg)
        print("Log:")
        print(output)
        stats.failed += 1
        #sys.exit(1)

    if len(output) == 0:
        stats.crashed += 1
        return testFail("<CRASHED>")

    if "expected" in test:
        result = re.findall(r'^Result: (.*?)$', "\n" + output + "\n", re.MULTILINE)
        if len(result) != 1:
            result = ["<NO RESULT>"]

        actual = str(result[0])
        expect = str(test.expected[0]['value'])

        if actual != expect:
            return testFail(f"Actual: {actual}, Expected {expect}")

    elif "expected_trap" in test:
        result = re.findall(r'^Error: \[trap\] (.*?) \(', "\n" + output + "\n", re.MULTILINE)
        if len(result) != 1:
            result = ["<NO TRAP>"]

        actual = str(result[0])
        expect = str(test.expected_trap)

        if actual != expect:
            return testFail(f"Actual trap: {actual}, Expected trap {expect}")

if not os.path.isdir(coreDir):
    if not os.path.isdir(specDir):
        specTestsFetch()
    specTestsPreprocess()

jsonFiles = arguments.file if arguments.file else glob.glob(os.path.join(coreDir, "*.json"))
jsonFiles.sort()

for fn in jsonFiles:
    with open(fn) as f:
        data = json.load(f)

    wast_source = filename(data["source_filename"])
    wast_module = ""

    if wast_source in ["linking.wast", "exports.wast"]:
        stats.total += len(data["commands"])
        stats.skipped += len(data["commands"])
        print(f"Warning: skipped {wast_source}")
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

            stats.total += 1

            if test.type == "assert_return":
                test.expected = cmd["expected"]
            if test.type == "assert_trap":
                test.expected_trap = cmd["text"]

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
