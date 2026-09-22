#!/usr/bin/env python3
"""List every pause point a module has, worked out from its instructions alone.

The Snapshot proposal (docs/proposals/Snapshot.md) says where a frame can stop,
in terms of the module and nothing else. This reads the module's disassembly from
the tree's wasm-objdump and applies that list, so what an engine's compiler
records can be checked against it:

    extra/pause-points.py [--exec build/wasm3] module.wasm

One pause point a line, sorted, the way a snapshot names it:

    <function> back-edge +0x<offset> -> loop@+0x<loop offset>
    <function> call +0x<offset>
    <function> entry +0x<offset>

An offset counts from just past the function's code-entry size, as a snapshot's
wasm_offset does. Two clauses of one instruction that go to the same loop are the
same pause point, so they are one line.

The bundled wasm-objdump predates stack switching, so a module that uses it cannot
be read here: suspend and resume, and the back edges of their `on` clauses, are not
listed.
"""

import argparse
import os
import shlex
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OBJDUMP = os.path.join(HERE, "..", "test", "wasi", "wabt", "wasm-objdump.wasm")

OPENERS = ("block", "loop", "if", "try_table")
CALLS = ("call", "call_indirect", "call_ref")


def disassemble(module, host):
    """[(function index, body start, [(offset, text), ...])] from wasm-objdump -d.

    The module is read from its own directory: the disassembler is a WASI program,
    and that is the directory it is given."""
    directory, name = os.path.split(os.path.abspath(module))
    cmd = list(host) if isinstance(host, (list, tuple)) else shlex.split(host)
    if cmd and (os.path.dirname(cmd[0]) or os.path.exists(cmd[0])):
        cmd[0] = os.path.abspath(cmd[0])
    proc = subprocess.run(
        host.split()
        + ["--stack-size", "1048576", os.path.abspath(OBJDUMP), "-d", name],
        cmd + ["--stack-size", "1048576", os.path.abspath(OBJDUMP), "-d", name],
        cwd=directory,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if proc.returncode != 0 or b"error:" in proc.stderr:
        raise RuntimeError(proc.stderr.decode(errors="replace").strip())

    functions = []
    for line in proc.stdout.decode().splitlines():
        if " func[" in line and line.rstrip().endswith(":"):
            start = int(line.split()[0], 16)
            index = int(line.split("func[")[1].split("]")[0])
            functions.append((index, start, []))
        elif functions and "|" in line and line.startswith(" "):
            where, _, text = line.partition("|")
            text = text.strip()
            # the local declarations come first, and a long instruction's bytes
            # run on over lines of their own
            if text and not text.startswith("local["):
                functions[-1][2].append((int(where.split(":")[0], 16), text))
    return functions


def labels_of_catch(tokens):
    """The labels of a try_table's clauses, in order."""
    labels, i = [], 0
    while i < len(tokens):
        clause = tokens[i]
        if clause in ("catch", "catch_ref"):
            labels.append(int(tokens[i + 2]))
            i += 3
        elif clause in ("catch_all", "catch_all_ref"):
            labels.append(int(tokens[i + 1]))
            i += 2
        else:
            i += 1
    return labels


def pause_points(functions):
    points = set()
    for index, start, instructions in functions:
        if not instructions:
            continue
        # before the first instruction, just past the local declarations; an
        # empty body's first instruction is its end
        points.add((index, "entry", instructions[0][0] - start, None))

        # the labels in scope, innermost last: (opcode, offset)
        scopes = [("func", None)]

        def back_edges(at, depths, scope_list):
            for depth in depths:
                opener, offset = scope_list[-1 - depth]
                if opener == "loop":
                    points.add((index, "back-edge", at - start, offset - start))

        for offset, text in instructions:
            tokens = text.split()
            op = tokens[0]
            if op in OPENERS:
                if op == "try_table":
                    # a clause's label counts from outside its try_table
                    back_edges(offset, labels_of_catch(tokens[1:]), scopes)
                scopes.append((op, offset))
            elif op == "end":
                scopes.pop()
            elif op in ("br", "br_if"):
                back_edges(offset, [int(tokens[1])], scopes)
            elif op == "br_table":
                back_edges(offset, [int(t) for t in tokens[1:]], scopes)
            elif op in CALLS:
                points.add((index, "call", offset - start, None))
    return points


def describe(point):
    index, kind, offset, loop = point
    line = f"{index} {kind} +0x{offset:x}"
    if loop is not None:
        line += f" -> loop@+0x{loop:x}"
    return line


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument(
        "--exec",
        default=os.path.join(HERE, "..", "build", "wasm3"),
        help="the Wasm3 to run the bundled disassembler on",
    )
    parser.add_argument("module")
    args = parser.parse_args()
    for point in sorted(pause_points(disassemble(args.module, args.exec))):
        print(describe(point))


if __name__ == "__main__":
    sys.exit(main())
