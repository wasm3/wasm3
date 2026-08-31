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
                 they are disguised as `// @sb<column>` comments, carrying the column
                 the author chose, and are put back there afterwards.

  3. verbatim    some lines are held out of the run entirely and restored byte for
                 byte: #define bodies, which clang-format explodes into indented
                 blocks; the `_try {` / `} _catch:` lines, whose braces would otherwise
                 nest the exception body a level deeper; and the few sidebars whose call
                 wraps onto a second line, which a `//` comment cannot carry.

Each transform inverts exactly what clang-format does to the construct, so the whole
pipeline is idempotent and `--check` is meaningful in CI.

Hand-aligned tables that no configuration can reproduce are fenced in the source with
`// clang-format off` / `on` instead -- see .clang-format.

Needs clang-format 23.x; any other major reformats the tree differently, so running one
is an error unless --any-version downgrades it to a warning.

Usage:
    python extra/format.py                # format in place
    python extra/format.py --check        # exit 1 with a diff if anything is unformatted
    python extra/format.py [paths..]      # limit to given files or directories
    python extra/format.py --any-version  # warn about a wrong clang-format, run anyway
"""

import argparse
import atexit
import difflib
import os
import re
import subprocess
import sys
import tempfile

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG = os.path.join(ROOT, ".clang-format")

# clang-format's output changes between major releases, so the tree is only ever
# formatted by one of them. A different major reformats files this one considers done,
# and every --check in CI then disagrees with whoever last ran it. Point releases within
# the major have not moved the output, so any 23.x will do.
CLANG_FORMAT_MAJOR = 23

sys.path.insert(0, os.path.join(ROOT, "extra"))
from testutils import Blacklist

DEFAULT_PATHS = ["source", "platforms", "test"]

# Code that lives here but is not ours to style. Patterns are fnmatch against the
# repo-relative path, and `*` spans directory separators. Naming a blacklisted file on
# the command line still formats it -- this only filters the directory walk.
EXCLUDE = Blacklist(
    [
        # generated: wasm modules dumped as C byte arrays
        "*.wasm.h",
        "source/extra/*",
        "platforms/embedded/esp32-pio/src/sdkconfig.h",  # written by ESP-IDF menuconfig
        # vendored: the Fomu/LiteX SoC support code, tracked verbatim from upstream
        "platforms/embedded/fomu/src/*",
        "platforms/embedded/fomu/include/*",
        # third-party benchmark programs built to wasm, kept as their authors wrote them
        "test/wasi/c-ray/*",
        "test/wasi/mandelbrot/*",
        "test/wasi/stream/*",
        # the C++ wrapper is a separate API in its own style; .clang-format is tuned for C
        "platforms/cpp/wasm3_cpp/include/*",
    ]
)

NL = "\n"


class ClangFormatError(Exception):
    """clang-format could not be run, or ran and failed. Reported by main()."""


# Settings some files want on top of .clang-format: `style` is merged over the shared
# config for any path `files` matches. A header is mostly one declaration table, with
# the comment on each entry and the blank line between groups part of how it reads, so
# there -- unlike in a function body -- alignment has to carry across both.
STYLE_OVERRIDES = [
    {
        "files": Blacklist(["*.h"]),
        "style": {
            "AlignConsecutiveDeclarations": {
                "Enabled": True,
                "AcrossEmptyLines": True,
                "AcrossComments": True,
            },
        },
    },
]

_derived = {}

# Matches IndentWidth / UseTab: Never in .clang-format.
TAB_WIDTH = 4

KEEP_TAG = "//@m3-keep-"
RE_DEFINE = re.compile(r"^[ \t]*#[ \t]*define\b")
RE_KEEP_TAG = re.compile(r"^[ \t]*" + re.escape(KEEP_TAG) + r"(?P<n>\d+)[ \t]*$")

# A held-back #define stands in as a directive rather than a comment, so that
# clang-format still indents it as one and reports back where the directive belongs.
# That is the only way the `#` prefix of a macro can follow IndentPPDirectives while its
# hand-aligned body stays untouched -- see reindent_define.
KEEP_DEFINE = "M3_KEEP_DEFINE_"
RE_KEEP_DEFINE = re.compile(
    r"^(?P<pre>[ \t]*#[ \t]*)define[ \t]+" + KEEP_DEFINE + r"(?P<n>\d+)[ \t]*$"
)
RE_DEFINE_PREFIX = re.compile(r"^(?P<pre>[ \t]*#[ \t]*)define\b")

# An include guard must stay visible: clang-format only recognises one as a guard when
# it can see the `#ifndef X` / `#define X` pair, and an unrecognised guard makes it treat
# the whole header as one level of preprocessor nesting and indent every directive in it.
RE_GUARD_IFNDEF = re.compile(r"^[ \t]*#[ \t]*ifndef[ \t]+(?P<name>\w+)[ \t]*$")
RE_GUARD_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(?P<name>\w+)[ \t]*$")

# The exception plumbing. `_try {` opens a block whose body deliberately stays at the
# enclosing function's indent, and `} _catch:` closes it on the label's own line. Keeping
# these lines verbatim also hides their braces from clang-format, which is what stops it
# nesting the body a level deeper. A brace-less `_catch:` is kept for its trailing
# statement, which clang-format would otherwise break onto a line of its own.
RE_EXC_LINE = re.compile(r"^[ \t]*(?:_try[ \t]*\{[ \t]*$|\}?[ \t]*_catch[ \t]*:)")

SIDEBAR_MACROS = r"(?:d_m3Assert|m3log)"
SIDEBAR_TAG = "// @sb"

# Sidebar asserts/logs sit in a right-hand column, out of the reading flow. The column
# the author chose is carried across the clang-format run in the tag and restored, so
# formatting never shifts an existing sidebar; only one that would now collide with its
# own line of code gets pushed right.
RE_SIDEBAR_HIDE = re.compile(
    r"^(?P<code>[ \t]*\S.*?)[ \t]{2,}(?P<tail>" + SIDEBAR_MACROS + r"[ \t]*\(.*)$"
)
RE_SIDEBAR_SHOW = re.compile(
    r"^(?P<pre>.*?)"
    + re.escape(SIDEBAR_TAG)
    + r"(?P<col>\d+) (?P<tail>"
    + SIDEBAR_MACROS
    + r".*)$"
)

# clang-format indents the hoisted macro like any other statement, spelling it `_ (` or
# `_(` depending on SpaceBeforeParens. Both have to be caught, or the `_` silently stays
# in the code column and the exception plumbing loses its shared column.
RE_HOISTED = re.compile(r"^(?P<indent> +)_ ?\(")


def wrapped_sidebar_span(lines, i):
    """Line count of a sidebar whose call runs past the end of lines[i], else 0.

    The disguise in hide_sidebars only works on a sidebar that ends on its own line: a
    `//` comment cannot carry the continuation, which would be left behind as code and
    read as a continuation of the statement, cascading the indent of everything after
    it. Only the continuation lines are held back -- the first line still goes through
    the run, since it carries the real code, up to and including a brace that the parse
    depends on.
    """
    m = RE_SIDEBAR_HIDE.match(lines[i])
    if not m or lines[i].lstrip().startswith("#"):
        return 0
    depth = m.group("tail").count("(") - m.group("tail").count(")")
    n = 1
    while depth > 0 and i + n < len(lines):
        depth += lines[i + n].count("(") - lines[i + n].count(")")
        n += 1
    return n if n > 1 else 0


def is_include_guard(lines, i):
    """True if lines[i] is the `#define X` of an `#ifndef X` include guard."""
    m = RE_GUARD_DEFINE.match(lines[i])
    if not m:
        return False
    prev = RE_GUARD_IFNDEF.match(lines[i - 1]) if i else None
    return bool(prev) and prev.group("name") == m.group("name")


def protect_verbatim(text):
    """Pre-pass: lift the constructs clang-format cannot express out of its reach.

    #define blocks, because clang-format restructures macro bodies -- it explodes
    `#define X { a; b; }` into an indented Allman block and rewrites the continuation
    backslashes -- and wasm3's macro tables are hand-aligned.

    `_try` / `_catch` lines, because the exception block is deliberately not a scope in
    the layout: its body sits at the enclosing function's indent. Holding these lines
    back takes their braces out of view, so clang-format lays the body out as ordinary
    function body rather than nesting it a level deeper.

    Both are better left exactly as the author wrote them.
    """
    lines = text.split(NL)
    out, saved = [], []
    i = 0
    while i < len(lines):
        if is_include_guard(lines, i):
            out.append(lines[i])
        elif RE_DEFINE.match(lines[i]):
            block = [lines[i]]
            while block[-1].rstrip().endswith("\\") and i + 1 < len(lines):
                i += 1
                block.append(lines[i])
            out.append("#define " + KEEP_DEFINE + str(len(saved)))
            saved.append(block)
        elif RE_EXC_LINE.match(lines[i]):
            out.append(KEEP_TAG + str(len(saved)))
            saved.append([lines[i]])
        elif wrapped_sidebar_span(lines, i):
            n = wrapped_sidebar_span(lines, i)
            out.append(lines[i])  # real code; hide_sidebars takes it
            out.append(KEEP_TAG + str(len(saved)))
            saved.append(lines[i + 1 : i + n])  # the wrapped call's tail only
            i += n - 1
        else:
            out.append(lines[i])
        i += 1
    return NL.join(out), saved


def reindent_define(block, prefix):
    """Move a held-back #define block to the directive indent clang-format chose.

    Only the gap between `#` and `define` is at stake, but every line of a multi-line
    macro shifts with it, so the body and its continuation backslashes keep the column
    the author lined them up in. A block whose body has no room to move left is left
    exactly as it was -- the indent is worth less than the alignment.
    """
    m = RE_DEFINE_PREFIX.match(block[0])
    if not m:
        return block
    delta = len(prefix) - len(m.group("pre"))
    if delta == 0:
        return block
    if delta < 0 and any(
        len(l) - len(l.lstrip()) < -delta for l in block[1:] if l.strip()
    ):
        return block
    first = prefix + block[0][len(m.group("pre")) :]
    return [first] + [(" " * delta + l) if delta > 0 else l[-delta:] for l in block[1:]]


def restore_verbatim(text, saved):
    """Post-pass: put the held-back lines back, byte for byte -- except for a #define,
    whose `#` prefix follows the indent clang-format gave its stand-in directive."""
    out = []
    for line in text.split(NL):
        m = RE_KEEP_TAG.match(line)
        if m:
            out += saved[int(m.group("n"))]
            continue
        m = RE_KEEP_DEFINE.match(line)
        if m:
            out += reindent_define(saved[int(m.group("n"))], m.group("pre"))
            continue
        out.append(line)
    return NL.join(out)


def hide_sidebars(text):
    """Pre-pass: disguise sidebar asserts/logs as trailing comments, tagged with the
    column the author put them in so the post-pass can put them back there."""
    out = []
    for line in text.split(NL):
        if not line.lstrip().startswith("#"):
            m = RE_SIDEBAR_HIDE.match(line)
            if m:
                col = len(line[: m.start("tail")].expandtabs(TAB_WIDTH))
                line = "{} {}{} {}".format(
                    m.group("code"), SIDEBAR_TAG, col, m.group("tail")
                )
        out.append(line)
    return NL.join(out)


def show_sidebars(text):
    """Post-pass: turn the disguised comments back into code, in the sidebar column."""
    out = []
    for line in text.split(NL):
        m = RE_SIDEBAR_SHOW.match(line)
        if m:
            code = m.group("pre").rstrip()
            col = max(
                int(m.group("col")),  # the column the author chose
                len(code) + 1,
            )  # unless the code now reaches it
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
            # `_` takes column 0; the paren keeps the column the statement was indented
            # to, wherever clang-format happened to put it in the matched text
            line = "_" + " " * (indent - 1) + line[m.end() - 1 :]
        out.append(line)
    return NL.join(out)


def clang_format_version():
    """The clang-format on PATH, as (major, printable version).

    `major` is None when the banner cannot be read, leaving the caller to decide what a
    version it cannot identify is worth. Not being able to run clang-format at all is
    something else, and raises.
    """
    try:
        out = subprocess.run(
            ["clang-format", "--version"], capture_output=True, text=True, check=True
        ).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        raise ClangFormatError(f"cannot run clang-format: {e}") from e

    m = re.search(r"\bversion\s+(\d+)\.(\S+)", out)
    if not m:
        return None, f"an unrecognised version ({out.strip()})"
    return int(m.group(1)), f"{m.group(1)}.{m.group(2)}"


def config_for(path):
    """The .clang-format for `path`: the shared one, or a variant carrying its overrides.

    The variant is built by loading the config as the YAML it is and updating the keys,
    so a nested block is replaced wholesale rather than merged key by key -- and so the
    result cannot end up with the duplicated mapping key that clang-format rejects.
    """
    rel = relative(path)
    for i, entry in enumerate(STYLE_OVERRIDES):
        if rel not in entry["files"]:
            continue
        if i not in _derived:
            with open(CONFIG, encoding="utf-8") as f:
                style = yaml.safe_load(f)
            style.update(entry["style"])
            fd, name = tempfile.mkstemp(
                prefix="clang-format-", suffix=".yaml", text=True
            )
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                yaml.safe_dump(style, f, sort_keys=False)
            atexit.register(os.remove, name)
            _derived[i] = name
        return _derived[i]
    return CONFIG


def format_text(text, path):
    text, kept = protect_verbatim(text)
    text = hide_sidebars(text)
    proc = subprocess.run(
        [
            "clang-format",
            "--assume-filename=" + path,
            "--style=file:" + config_for(path),
        ],
        input=text,
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        raise ClangFormatError(f"clang-format failed on {path}:\n{proc.stderr}")
    # rehoist changes the width of the code ahead of a sidebar, so the sidebar goes
    # back last, once the line it trails has its final text.
    text = rehoist(proc.stdout)
    text = show_sidebars(text)
    return restore_verbatim(text, kept)


def relative(path):
    """Repo-relative, forward-slash path -- what EXCLUDE's patterns are written against."""
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def collect(paths):
    files = []
    for p in paths:
        full = p if os.path.isabs(p) else os.path.join(ROOT, p)
        if os.path.isfile(full):
            files.append(full)  # named outright, so format it, blacklist or not
            continue
        for dirpath, _, filenames in os.walk(full):
            files += [
                os.path.join(dirpath, f)
                for f in filenames
                if f.endswith((".c", ".h"))
                and relative(os.path.join(dirpath, f)) not in EXCLUDE
            ]
    return sorted(files)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "--check",
        action="store_true",
        help="report unformatted files instead of rewriting them",
    )
    ap.add_argument(
        "--any-version",
        action="store_true",
        help=f"warn instead of stopping when clang-format is not {CLANG_FORMAT_MAJOR}.x",
    )
    ap.add_argument("paths", nargs="*")
    args = ap.parse_args()

    major, version = clang_format_version()
    if major != CLANG_FORMAT_MAJOR:
        problem = (
            f"this tree is formatted with clang-format {CLANG_FORMAT_MAJOR}.x, "
            f"but {version} is on PATH, and formatting differs between majors"
        )
        if not args.any_version:
            sys.exit(f"{problem}.\nPass --any-version to make this a warning.")
        print(f"warning: {problem}", file=sys.stderr)

    failed = []
    for path in collect(args.paths or DEFAULT_PATHS):
        with open(path, "r", encoding="utf-8", newline="") as f:
            original = f.read()
        rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
        # A file may be checked out with CRLF. Format on LF, then hand the file back
        # with the endings it came with -- rewriting those would touch every line of
        # the file and swamp the actual formatting change.
        eol = "\r\n" if "\r\n" in original else NL
        formatted = format_text(original.replace("\r\n", NL), path).replace(NL, eol)
        if formatted == original:
            continue
        if args.check:
            failed.append(rel)
            sys.stdout.writelines(
                difflib.unified_diff(
                    original.splitlines(True),
                    formatted.splitlines(True),
                    fromfile=rel,
                    tofile=rel + " (formatted)",
                )
            )
        else:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(formatted)
            print("formatted " + rel)

    if failed:
        sys.exit(
            f"\n{len(failed)} file(s) need formatting; run: python extra/format.py"
        )


if __name__ == "__main__":
    try:
        main()
    except ClangFormatError as e:
        sys.exit(str(e))
