#!/usr/bin/env python3
"""
wasm3 source formatter.

Runs clang-format, then restores the wasm3 conventions that clang-format has no
option to express. Always use this instead of bare clang-format -- see .clang-format.

  1. `_ (TRY)`   the exception macro is hoisted to column 0, leaving its opening paren
                 on the column the statement would normally be indented to. `_catch:`
                 lands at column 0 too, so all exception plumbing shares one column.

  2. sidebar     d_m3Assert / m3log trailing a line of real code stay out of the
                 reading flow, in a right-hand column. Across the clang-format run
                 they are disguised as `// @sb` comments, so AlignTrailingComments
                 lines up consecutive ones, then they are restored in place.

  3. signatures  `static` (and `static inline`, `static M3_NOINLINE`) sit on their own
                 line, and the function name is set off by double spaces:

                     static
                     M3Result  CopyStackTopToRegister  (IM3Compilation o)

  4. macros      #define bodies are held out of the run entirely and restored byte for
                 byte, since clang-format explodes them into Allman blocks and wasm3's
                 macro tables are hand-aligned.

Each transform inverts exactly what clang-format does to the construct, so the whole
pipeline is idempotent and `--check` is meaningful in CI.

Usage:
    python extra/format.py            # format in place
    python extra/format.py --check    # exit 1 with a diff if anything is unformatted
    python extra/format.py [paths..]  # limit to given files or directories
"""

import argparse
import difflib
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG = os.path.join(ROOT, ".clang-format")

DEFAULT_PATHS = ["source"]

# source/extra holds generated wasm byte-array headers; never format those.
EXCLUDE_DIRS = {"extra"}

NL = "\n"

MACRO_TAG = "//@m3-macro-"
RE_DEFINE = re.compile(r"^[ \t]*#[ \t]*define\b")
RE_MACRO_TAG = re.compile(r"^[ \t]*" + re.escape(MACRO_TAG) + r"(?P<n>\d+)[ \t]*$")

SIDEBAR_MACROS = r"(?:d_m3Assert|m3log)"
SIDEBAR_TAG = "// @sb "

# Sidebar asserts/logs start no earlier than this column, which is what keeps them out
# of the reading flow. A group clang-format aligned further right keeps its own column,
# and code already longer than this just gets a single space.
SIDEBAR_COLUMN = 90

# A line of real code, two or more spaces, then a sidebar macro running to end of line.
RE_SIDEBAR_HIDE = re.compile(
    r"^(?P<code>[ \t]*\S.*?)[ \t]{2,}(?P<tail>" + SIDEBAR_MACROS + r"[ \t]*\(.*)$")
RE_SIDEBAR_SHOW = re.compile(
    r"^(?P<pre>.*?)" + re.escape(SIDEBAR_TAG) + r"(?P<tail>" + SIDEBAR_MACROS + r".*)$")

# clang-format normalises the hoisted macro to `<indent>_ (...`.
RE_HOISTED = re.compile(r"^(?P<indent> +)_ \(")

FN_MODS = r"(?:static|inline|M3_NOINLINE)"
RE_FN_DEF = re.compile(
    r"^(?P<mods>" + FN_MODS + r"(?:[ \t]+" + FN_MODS + r")*[ \t]+)?"
    r"(?P<ret>[A-Za-z_][A-Za-z0-9_]*(?:[ \t]+[A-Za-z0-9_]+)*[ \t]*\**)[ \t]+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)[ \t]*"
    r"\((?P<args>.*)\)[ \t]*$")


def protect_macros(text):
    """Pre-pass: lift #define blocks out of clang-format's reach.

    clang-format restructures macro bodies -- it explodes `#define X { a; b; }` into an
    indented Allman block and rewrites the continuation backslashes -- and wasm3's macro
    tables are hand-aligned. Both are better left exactly as the author wrote them.
    """
    lines = text.split(NL)
    out, saved = [], []
    i = 0
    while i < len(lines):
        if RE_DEFINE.match(lines[i]):
            block = [lines[i]]
            while block[-1].rstrip().endswith("\\") and i + 1 < len(lines):
                i += 1
                block.append(lines[i])
            out.append(MACRO_TAG + str(len(saved)))
            saved.append(block)
        else:
            out.append(lines[i])
        i += 1
    return NL.join(out), saved


def restore_macros(text, saved):
    """Post-pass: put the #define blocks back, verbatim."""
    out = []
    for line in text.split(NL):
        m = RE_MACRO_TAG.match(line)
        if m:
            out += saved[int(m.group("n"))]
        else:
            out.append(line)
    return NL.join(out)


def hide_sidebars(text):
    """Pre-pass: disguise sidebar asserts/logs as trailing comments."""
    out = []
    for line in text.split(NL):
        if not line.lstrip().startswith("#"):
            m = RE_SIDEBAR_HIDE.match(line)
            if m:
                line = m.group("code") + " " + SIDEBAR_TAG + m.group("tail")
        out.append(line)
    return NL.join(out)


def show_sidebars(text):
    """Post-pass: turn the disguised comments back into code, in the sidebar column."""
    out = []
    for line in text.split(NL):
        m = RE_SIDEBAR_SHOW.match(line)
        if m:
            code = m.group("pre").rstrip()
            col = max(len(code) + 1,                              # always leave a gap
                      SIDEBAR_COLUMN,                             # the sidebar column
                      len(m.group("pre")) + len(SIDEBAR_TAG))     # group alignment
            line = code + " " * (col - len(code)) + m.group("tail")
        out.append(line)
    return NL.join(out)


def rehoist(text):
    """Post-pass: pull `_ (` back to column 0, the paren keeping the indent column."""
    out = []
    for line in text.split(NL):
        m = RE_HOISTED.match(line)
        if m:
            indent = len(m.group("indent"))
            # drop the `_ ` clang-format left behind; the paren takes the indent column
            line = "_" + " " * (indent - 1) + line[indent + 2:]
        out.append(line)
    return NL.join(out)


def split_signatures(text):
    """Post-pass: restore the `static` break and the double-spaced function name.

    Only applies to a definition header at column 0 whose very next line is `{`, which
    leaves one-line accessors and macro-built op definitions (d_m3Op) alone.
    """
    lines = text.split(NL)
    out = []
    for i, line in enumerate(lines):
        if (line
                and not line[0].isspace()
                and not line.startswith("#")
                and i + 1 < len(lines)
                # startswith, not ==, since the brace line may carry a sidebar
                and lines[i + 1].startswith("{")):
            m = RE_FN_DEF.match(line)
            if m:
                sig = "{}  {}  ({})".format(
                    m.group("ret").rstrip(), m.group("name"), m.group("args"))
                mods = m.group("mods")
                line = mods.strip() + NL + sig if mods else sig
        out.append(line)
    return NL.join(out)


def format_text(text, path):
    text, macros = protect_macros(text)
    text = hide_sidebars(text)
    proc = subprocess.run(
        ["clang-format", "--assume-filename=" + path, "--style=file:" + CONFIG],
        input=text, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        sys.exit(f"clang-format failed on {path}:\n{proc.stderr}")
    text = show_sidebars(proc.stdout)
    text = rehoist(text)
    text = split_signatures(text)
    return restore_macros(text, macros)


def collect(paths):
    files = []
    for p in paths:
        full = p if os.path.isabs(p) else os.path.join(ROOT, p)
        if os.path.isfile(full):
            files.append(full)
            continue
        for dirpath, dirnames, filenames in os.walk(full):
            dirnames[:] = [d for d in dirnames if d not in EXCLUDE_DIRS]
            files += [os.path.join(dirpath, f)
                      for f in filenames if f.endswith((".c", ".h"))]
    return sorted(files)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="report unformatted files instead of rewriting them")
    ap.add_argument("paths", nargs="*")
    args = ap.parse_args()

    failed = []
    for path in collect(args.paths or DEFAULT_PATHS):
        with open(path, "r", encoding="utf-8", newline="") as f:
            original = f.read()
        formatted = format_text(original.replace("\r\n", NL), path)
        if formatted == original:
            continue
        rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
        if args.check:
            failed.append(rel)
            sys.stdout.writelines(difflib.unified_diff(
                original.splitlines(True), formatted.splitlines(True),
                fromfile=rel, tofile=rel + " (formatted)"))
        else:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(formatted)
            print("formatted " + rel)

    if failed:
        sys.exit(f"\n{len(failed)} file(s) need formatting; run: python extra/format.py")


if __name__ == "__main__":
    main()
