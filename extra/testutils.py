#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy

import atexit
import fnmatch
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace


class ansi:
    ENDC = "\033[0m"
    HEADER = "\033[94m"
    OKGREEN = "\033[92m"
    WARNING = "\033[93m"
    FAIL = "\033[91m"
    BOLD = "\033[1m"
    UNDERLINE = "\033[4m"


class Blacklist:
    def __init__(self, patterns):
        self._patterns = list(map(fnmatch.translate, patterns))
        self.update()

    def add(self, patterns):
        self._patterns += list(map(fnmatch.translate, patterns))
        self.update()

    def update(self):
        # `(?!)` for the empty list: an alternation of nothing is the empty pattern,
        # which matches every string rather than none of them.
        self._regex = re.compile("|".join(self._patterns) or "(?!)")

    def __contains__(self, item):
        return self._regex.match(item) is not None


def filename(p):
    _, fn = os.path.split(p)
    return fn


def pathname(p):
    pn, _ = os.path.split(p)
    return pn


def ensure_path(p):
    pathlib.Path(p).mkdir(parents=True, exist_ok=True)


def print_stats(stats):
    """The counters a test run accumulated, one per line, in declaration order."""
    width = max(map(len, vars(stats)))
    for name, count in vars(stats).items():
        print(f"  {name:>{width}}: {count}")


#
# Assembling module sources
#

# wabt's wast2json, itself a .wasm module, so the suite needs no assembler
# installed: it runs on whichever engine is being tested. Relative to ./test.
WAST2JSON = "./wasi/wabt/wast2json.wasm"

SOURCE_SUFFIXES = (".wat", ".wast")


class WastError(Exception):
    """A module source that could not be assembled."""


class WastCompiler:
    """Assembles module sources into a temporary directory, one module per source.

    A case is committed as text - `.wat` for a plain module, `.wast` where it needs
    the script grammar - and the binary the engine runs is assembled from that text
    on every run.

    wast2json does the assembling rather than wat2wasm, because a case whose point
    is an encoding no assembler would emit has to be able to spell out its own
    bytes. `(module binary ...)` reaches the output untouched, and wrapping that in
    `assert_malformed` gets even a module that does not decode written out; wat2wasm
    instead decodes such a module and writes it back in its own canonical form,
    losing exactly what the case is about.
    """

    def __init__(self, host, wast2json=WAST2JSON):
        self._host = host if isinstance(host, list) else host.split(" ")
        self._wast2json = wast2json
        self._dir = None
        self._built = {}

    @property
    def assembled(self):
        """How many modules have been assembled so far."""
        return len(self._built)

    def compile(self, source):
        """Path of the module assembled from `source`, relative to the current directory."""
        if source not in self._built:
            self._built[source] = self._assemble(source)
        return self._built[source]

    def workdir(self):
        """The directory the modules are assembled into, made on first use.

        Wasm3 preopens the current directory and nothing else, so this has to sit
        below it for wast2json to be able to write there.
        """
        if self._dir is None:
            self._dir = filename(tempfile.mkdtemp(prefix=".wast-", dir="."))
            atexit.register(shutil.rmtree, self._dir, ignore_errors=True)
        return self._dir

    def _assemble(self, source):
        # Mirroring the directory the source sits in keeps two cases with the same
        # name apart, and lets the module keep that name - a backtrace prints it, so
        # a test can be looking for it.
        rel = os.path.relpath(source, ".").replace(os.sep, "/").replace("../", "")
        stem = f"{self.workdir()}/{rel.rsplit('.', 1)[0]}"
        ensure_path(pathname(stem))

        command = self._host + [
            self._wast2json,
            "--enable-all",
            source,
            "-o",
            f"{stem}.json",
        ]
        p = subprocess.run(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False
        )
        if p.returncode != 0:
            report = p.stdout.decode("utf-8", errors="replace").strip()
            raise WastError(f"{' '.join(command)}\n{report}")

        # The script is a whole grammar, and this only wants the module out of it:
        # the first command that wrote one, whether the script asserts anything
        # about it or not.
        try:
            with open(f"{stem}.json", encoding="utf-8") as f:
                commands = json.load(f)["commands"]
            built = next(c["filename"] for c in commands if "filename" in c)
        except (OSError, ValueError, KeyError, StopIteration):
            raise WastError(f"{' '.join(command)}\nwrote no module") from None

        # wast2json numbers the modules it writes, and names them after the .json
        out = f"{stem}.wasm"
        os.replace(f"{pathname(stem)}/{built}", out)
        return out


def assemble_modules(sources, host, verbose=False):
    """{source: the module to run} over the sources a run needs.

    A .wat or .wast is assembled, a .wasm is taken as it is, and each source is
    handled once however many cases share it. A source that will not assemble is a
    broken tool chain rather than a failed case, so it ends the run right here.
    """
    wasts = WastCompiler(host)
    modules = {}
    for source in sources:
        if source in modules:
            continue
        if not source.endswith(SOURCE_SUFFIXES):
            modules[source] = source
            continue
        try:
            modules[source] = wasts.compile(source)
        except WastError as e:
            sys.exit(f"{ansi.FAIL}Could not assemble {source}:{ansi.ENDC}\n{e}")
    if verbose and wasts.assembled:
        print(f"Assembled {wasts.assembled} module(s) into {wasts.workdir()}/\n")
    return modules
