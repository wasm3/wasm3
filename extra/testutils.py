#!/usr/bin/env python3

# Author: Volodymyr Shymanskyy

import os
import re, fnmatch
import pathlib
from queue import Queue, Empty
import shlex
from subprocess import Popen, STDOUT, PIPE
from threading import Thread
import time

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
        # Ensure an empty list matches nothing.
        self._patterns.append("^$")
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

class Wasm3():
    def __init__(self, exe, timeout=None):
        self.exe = exe
        self.p = None
        self.loaded = None
        self.timeout = timeout
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

    def invoke(self, cmd):
        return self._run_cmd(":invoke " + " ".join(map(str, cmd)) + "\n")

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
