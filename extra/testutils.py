#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy

import os
import re, fnmatch
import pathlib

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

class Blacklist():
    def __init__(self, patterns):
        self._patterns = list(map(fnmatch.translate, patterns))
        self.update()

    def add(self, patterns):
        self._patterns += list(map(fnmatch.translate, patterns))
        self.update()

    def update(self):
        self._regex = re.compile('|'.join(self._patterns))

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
