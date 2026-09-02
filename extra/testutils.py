#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy

import fnmatch
import os
import pathlib
import re
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
