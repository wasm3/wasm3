#!/usr/bin/env python3
"""
wasm3 source formatter.

C and headers go through clang-format, then the wasm3 conventions clang-format has no
option to express are put back. Always use this instead of bare clang-format -- see
.clang-format.

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
                 nest the exception body a level deeper; the sidebar entries that take a
                 whole line, whose only indent is the sidebar column itself; and the few
                 sidebars whose call wraps onto a second line, which a `//` comment
                 cannot carry.

Each transform inverts exactly what clang-format does to the construct, so the whole
pipeline is idempotent and `--check` is meaningful in CI.

Trailing whitespace is taken off every line last of all. clang-format only does that for
the code it lays out -- never inside a `clang-format off` fence, and never on the lines
held back from the run here.

Hand-aligned tables that no configuration can reproduce are fenced in the source with
`// clang-format off` / `on` instead -- see .clang-format.

The scripts go through black, called through its Python API, plain and unconfigured --
none of the above applies to them, and black's defaults are the whole of their style.
Their hand-aligned tables are fenced the same way as the ones in the C, with
`# fmt: off` / `on`. Black is driven from here rather than from a CI step of its own so
that one command formats the whole tree and one command checks it.

Needs clang-format 23.x and black 26.x; another major of either reformats the tree
differently, so running one is an error unless --any-version downgrades it to a warning.
Only the formatter a run actually needs is looked for: black is imported on first use,
so laying out the C alone does not require it installed.

Usage:
    python extra/format.py                # format in place
    python extra/format.py --check        # exit 1 with a diff if anything is unformatted
    python extra/format.py [paths..]      # limit to given files or directories
    python extra/format.py --any-version  # warn about a wrong formatter, run anyway
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
sys.path.insert(0, os.path.join(ROOT, "extra"))

from testutils import Blacklist


class GitIgnore:
    """Every .gitignore in the tree, as fnmatch patterns, read as the walk reaches it.

    What the walk leaves alone: git's own directory, and whatever git is told to ignore.
    The walk starts at the root, so without these it would wander into build trees and
    downloaded toolchains, which dwarf the tree being formatted.

    A .gitignore governs the directory it sits in and everything below, so each one is
    read when the walk enters that directory and matched against paths relative to it.
    Reading them on the way down, rather than gathering them all up front, is also what
    keeps the search for them out of the very trees the outer ones exist to name.

    A trailing `/` is kept and means what git means by it -- the rule matches a
    directory and never a file -- so a rule that names a build tree can be told apart
    from one that would also take a tracked script of the same name.

    Comments, blank lines and `!` negations are skipped: nothing in this tree relies on
    a negation, and a blacklist has no way to express one.
    """

    # Ignored wherever it turns up, with no .gitignore having to say so.
    ALWAYS = [".git"]

    def __init__(self):
        self._at = {}

    def patterns(self, line):
        """One .gitignore line as (fnmatch patterns, whether it names directories only).

        An anchored line matches at its own level only; an unanchored one matches at any
        depth below the .gitignore holding it, which takes a second pattern to say.
        """
        directory = line.endswith("/")
        anchored = "/" in line.rstrip("/")
        line = line.strip("/")
        return ([line] if anchored else [line, "*/" + line]), directory

    def at(self, dirpath):
        """What the .gitignore in `dirpath` names, as a (whatever it is, directories
        only) pair of blacklists over paths relative to `dirpath`. Read once per
        directory, and empty where there is no .gitignore."""
        if dirpath not in self._at:
            anything, dirs_only = list(self.ALWAYS), []
            try:
                with open(os.path.join(dirpath, ".gitignore"), encoding="utf-8") as f:
                    for line in f:
                        line = line.strip()
                        if line and not line.startswith(("#", "!")):
                            pats, directory = self.patterns(line)
                            (dirs_only if directory else anything).extend(pats)
            except OSError:
                pass
            self._at[dirpath] = Blacklist(anything), Blacklist(dirs_only)
        return self._at[dirpath]

    def ignores(self, dirpath, name, isdir):
        """True if the .gitignore in `dirpath`, or any of the ones above it up to the
        repo root, names `dirpath/name`."""
        path = os.path.join(dirpath, name)
        d = os.path.normpath(dirpath)
        while True:
            anything, dirs_only = self.at(d)
            rel = relative(path, d)
            if rel in anything or (isdir and rel in dirs_only):
                return True
            parent = os.path.dirname(d)
            if d == ROOT or parent == d:
                return False
            d = parent

    def prune(self, dirpath, names, isdir):
        """Drop the ignored entries from `names`, in place. Handed the directories
        os.walk offers, that is also how it is told not to descend into them."""
        names[:] = [n for n in names if not self.ignores(dirpath, n, isdir)]


IGNORED = GitIgnore()

# Code that lives here but is not ours to style. Patterns are fnmatch against the
# repo-relative path, and `*` spans directory separators. Naming a blacklisted file on
# the command line still formats it -- this only filters the directory walk.
EXCLUDE = Blacklist(
    [
        # generated: wasm modules dumped as C byte arrays
        "*.wasm.h",
        "source/extra/*",
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


class FormatterError(Exception):
    """A formatter could not be run, or ran and failed. Reported by main()."""


class CFormatter:
    """The C and the headers: clang-format, with the wasm3 conventions put back.

    Everything the conventions are made of -- the patterns that recognise them, the
    columns they are kept in, the passes that hide them from clang-format and restore
    them -- belongs to this class and to nothing else. See the module docstring for
    what the three of them are and why clang-format cannot express them.
    """

    NAME = "clang-format"
    SUFFIXES = (".c", ".h")
    MAJOR = 23  # the release the tree is formatted with; see the module docstring

    CONFIG = os.path.join(ROOT, ".clang-format")

    # Settings some files want on top of .clang-format: `style` is merged over the
    # shared config for any path `files` matches. A header is mostly one declaration
    # table, with the comment on each entry and the blank line between groups part of
    # how it reads, so there -- unlike in a function body -- alignment has to carry
    # across both.
    STYLE_OVERRIDES = [  # noqa: RUF012
        {
            "files": Blacklist(["*.h"]),
            "style": {
                "AlignConsecutiveDeclarations": {
                    "Enabled": True,
                    "AcrossEmptyLines": True,
                    "AcrossComments": True,
                },
                "AlignConsecutiveAssignments": {
                    "EnumAssignments": True,
                    "AcrossEmptyLines": True,
                    "AcrossComments": True,
                },
            },
        },
    ]

    # Matches IndentWidth / UseTab: Never in .clang-format.
    TAB_WIDTH = 4

    KEEP_TAG = "//@m3-keep-"
    RE_DEFINE = re.compile(r"^[ \t]*#[ \t]*define\b")
    RE_KEEP_TAG = re.compile(r"^[ \t]*" + re.escape(KEEP_TAG) + r"(?P<n>\d+)[ \t]*$")

    # A held-back #define stands in as a directive rather than a comment, so that
    # clang-format still indents it as one and reports back where the directive belongs.
    # That is the only way the `#` prefix of a macro can follow IndentPPDirectives while
    # its hand-aligned body stays untouched -- see reindent_define.
    KEEP_DEFINE = "M3_KEEP_DEFINE_"
    RE_KEEP_DEFINE = re.compile(
        r"^(?P<pre>[ \t]*#[ \t]*)define[ \t]+" + KEEP_DEFINE + r"(?P<n>\d+)[ \t]*$"
    )
    RE_DEFINE_PREFIX = re.compile(r"^(?P<pre>[ \t]*#[ \t]*)define\b")

    # An include guard must stay visible: clang-format only recognises one as a guard
    # when it can see the `#ifndef X` / `#define X` pair, and an unrecognised guard makes
    # it treat the whole header as one level of preprocessor nesting and indent every
    # directive in it.
    RE_GUARD_IFNDEF = re.compile(r"^[ \t]*#[ \t]*ifndef[ \t]+(?P<name>\w+)[ \t]*$")
    RE_GUARD_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(?P<name>\w+)[ \t]*$")

    # The exception plumbing. `_try {` opens a block whose body deliberately stays at the
    # enclosing function's indent, and `} _catch:` closes it on the label's own line.
    # Keeping these lines verbatim also hides their braces from clang-format, which is
    # what stops it nesting the body a level deeper. A brace-less `_catch:` is kept for
    # its trailing statement, which clang-format would otherwise break onto a line of its
    # own.
    RE_EXC_LINE = re.compile(r"^[ \t]*(?:_try[ \t]*\{[ \t]*$|\}?[ \t]*_catch[ \t]*:)")

    SIDEBAR_MACROS = r"(?:d_m3Assert|m3log|expect)"
    SIDEBAR_TAG = "// @sb"

    # Sidebar asserts/logs sit in a right-hand column, out of the reading flow. The
    # column the author chose is carried across the clang-format run in the tag and
    # restored, so formatting never shifts an existing sidebar; only one that would now
    # collide with its own line of code gets pushed right.
    RE_SIDEBAR_HIDE = re.compile(
        r"^(?P<code>[ \t]*\S.*?)[ \t]{2,}(?P<tail>" + SIDEBAR_MACROS + r"[ \t]*\(.*)$"
    )
    # A sidebar entry can also take a whole line, continuing the column of the one above
    # it with nothing of its own on the left. Such a line has no code for clang-format to
    # lay out -- only an indent, which is the sidebar column -- so it is held back
    # verbatim rather than disguised; see is_lone_sidebar.
    RE_SIDEBAR_ALONE = re.compile(
        r"^(?P<indent>[ \t]*)(?P<tail>" + SIDEBAR_MACROS + r"[ \t]*\(.*)$"
    )
    # Lines that say nothing about where the code column runs: blank ones, comments and
    # directives -- and a sidebar entry, which is measured against code, never against
    # the entry above it.
    RE_NOT_CODE = re.compile(r"^[ \t]*(?:$|//|#)")
    RE_SIDEBAR_SHOW = re.compile(
        r"^(?P<pre>.*?)"
        + re.escape(SIDEBAR_TAG)
        + r"(?P<col>\d+) (?P<tail>"
        + SIDEBAR_MACROS
        + r".*)$"
    )

    # clang-format indents the hoisted macro like any other statement, spelling it `_ (`
    # or `_(` depending on SpaceBeforeParens. Both have to be caught, or the `_` silently
    # stays in the code column and the exception plumbing loses its shared column.
    RE_HOISTED = re.compile(r"^(?P<indent> +)_ ?\(")

    def __init__(self):
        # config_for writes a derived .clang-format per STYLE_OVERRIDES entry it needs,
        # and each is written once and reused for every file that matches.
        self._derived = {}

    def version(self):
        """The clang-format on PATH, as (major, printable version).

        `major` is None when the banner cannot be read, leaving the caller to decide
        what a version it cannot identify is worth. Not being able to run clang-format
        at all is something else, and raises.
        """
        try:
            out = subprocess.run(
                [self.NAME, "--version"], capture_output=True, text=True, check=True
            ).stdout
        except (OSError, subprocess.CalledProcessError) as e:
            raise FormatterError(f"cannot run {self.NAME}: {e}") from e

        m = re.search(r"\bversion\s+(\d+)\.(\S+)", out)
        if not m:
            return None, f"an unrecognised version ({out.strip()})"
        return int(m.group(1)), f"{m.group(1)}.{m.group(2)}"

    def format(self, text, path):
        """`text` through clang-format, between the pre-passes and the post-passes."""
        text, kept = self.protect_verbatim(text)
        text = self.hide_sidebars(text)
        proc = subprocess.run(
            [
                self.NAME,
                "--assume-filename=" + path,
                "--style=file:" + self.config_for(path),
            ],
            input=text,
            capture_output=True,
            text=True,
            check=False,
        )
        if proc.returncode != 0:
            raise FormatterError(f"{self.NAME} failed on {path}:\n{proc.stderr}")
        # rehoist changes the width of the code ahead of a sidebar, so the sidebar goes
        # back last, once the line it trails has its final text.
        text = self.rehoist(proc.stdout)
        text = self.show_sidebars(text)
        return self.strip_trailing(self.restore_verbatim(text, kept))

    def config_for(self, path):
        """The .clang-format for `path`: the shared one, or a variant with its overrides.

        The variant is built by loading the config as the YAML it is and updating the
        keys, so a nested block is replaced wholesale rather than merged key by key --
        and so the result cannot end up with the duplicated mapping key that
        clang-format rejects.
        """
        rel = relative(path)
        for i, entry in enumerate(self.STYLE_OVERRIDES):
            if rel not in entry["files"]:
                continue
            if i not in self._derived:
                with open(self.CONFIG, encoding="utf-8") as f:
                    style = yaml.safe_load(f)
                style.update(entry["style"])
                fd, name = tempfile.mkstemp(
                    prefix="clang-format-", suffix=".yaml", text=True
                )
                with os.fdopen(fd, "w", encoding="utf-8") as f:
                    yaml.safe_dump(style, f, sort_keys=False)
                atexit.register(os.remove, name)
                self._derived[i] = name
            return self._derived[i]
        return self.CONFIG

    def sidebar_call_span(self, lines, i, tail):
        """Line count of the sidebar call at lines[i] that reads on as `tail`."""
        depth = tail.count("(") - tail.count(")")
        n = 1
        while depth > 0 and i + n < len(lines):
            depth += lines[i + n].count("(") - lines[i + n].count(")")
            n += 1
        return n

    def wrapped_sidebar_span(self, lines, i):
        """Line count of a sidebar whose call runs past the end of lines[i], else 0.

        The disguise in hide_sidebars only works on a sidebar that ends on its own line:
        a `//` comment cannot carry the continuation, which would be left behind as code
        and read as a continuation of the statement, cascading the indent of everything
        after it. Only the continuation lines are held back -- the first line still goes
        through the run, since it carries the real code, up to and including a brace that
        the parse depends on.
        """
        m = self.RE_SIDEBAR_HIDE.match(lines[i])
        if not m or lines[i].lstrip().startswith("#"):
            return 0
        n = self.sidebar_call_span(lines, i, m.group("tail"))
        return n if n > 1 else 0

    def code_indent(self, lines, i, step):
        """Indent of the nearest line to lines[i] in direction `step` that shows where
        the code column runs, or None if the file has no such line left that way."""
        j = i + step
        while 0 <= j < len(lines):
            if not self.RE_NOT_CODE.match(lines[j]) and not self.RE_SIDEBAR_ALONE.match(
                lines[j]
            ):
                expanded = lines[j].expandtabs(self.TAB_WIDTH)
                return len(expanded) - len(expanded.lstrip())
            j += step
        return None

    def is_lone_sidebar(self, lines, i):
        """True if lines[i] is a sidebar entry with the whole line to itself.

        What tells one apart from an ordinary statement that happens to be one of these
        macros is how far right it sits. A statement on a line of its own can be one
        level past the code around it -- that is what the body of a brace-less `if` looks
        like -- and a sidebar is out beyond that, in a column of its own. Anything closer
        in is read as code and left to clang-format to indent.
        """
        m = self.RE_SIDEBAR_ALONE.match(lines[i])
        if not m:
            return False
        around = [
            c
            for c in (self.code_indent(lines, i, -1), self.code_indent(lines, i, 1))
            if c is not None
        ]
        col = len(m.group("indent").expandtabs(self.TAB_WIDTH))
        return bool(around) and col > max(around) + self.TAB_WIDTH

    def is_include_guard(self, lines, i):
        """True if lines[i] is the `#define X` of an `#ifndef X` include guard."""
        m = self.RE_GUARD_DEFINE.match(lines[i])
        if not m:
            return False
        prev = self.RE_GUARD_IFNDEF.match(lines[i - 1]) if i else None
        return bool(prev) and prev.group("name") == m.group("name")

    def protect_verbatim(self, text):
        """Pre-pass: lift the constructs clang-format cannot express out of its reach.

        #define blocks, because clang-format restructures macro bodies -- it explodes
        `#define X { a; b; }` into an indented Allman block and rewrites the continuation
        backslashes -- and wasm3's macro tables are hand-aligned.

        `_try` / `_catch` lines, because the exception block is deliberately not a scope
        in the layout: its body sits at the enclosing function's indent. Holding these
        lines back takes their braces out of view, so clang-format lays the body out as
        ordinary function body rather than nesting it a level deeper.

        Whole-line sidebar entries, because such a line is nothing but the sidebar column
        and the entry standing in it, and clang-format reads it as a statement that has
        drifted far to the right and pulls it back to the code column.

        All three are better left exactly as the author wrote them.
        """
        lines = text.split(NL)
        out, saved = [], []
        i = 0
        while i < len(lines):
            if self.is_include_guard(lines, i):
                out.append(lines[i])
            elif self.RE_DEFINE.match(lines[i]):
                block = [lines[i]]
                while block[-1].rstrip().endswith("\\") and i + 1 < len(lines):
                    i += 1
                    block.append(lines[i])
                out.append("#define " + self.KEEP_DEFINE + str(len(saved)))
                saved.append(block)
            elif self.RE_EXC_LINE.match(lines[i]):
                out.append(self.KEEP_TAG + str(len(saved)))
                saved.append([lines[i]])
            elif self.is_lone_sidebar(lines, i):
                tail = self.RE_SIDEBAR_ALONE.match(lines[i]).group("tail")
                n = self.sidebar_call_span(lines, i, tail)
                out.append(self.KEEP_TAG + str(len(saved)))
                saved.append(lines[i : i + n])
                i += n - 1
            elif self.wrapped_sidebar_span(lines, i):
                n = self.wrapped_sidebar_span(lines, i)
                out.append(lines[i])  # real code; hide_sidebars takes it
                out.append(self.KEEP_TAG + str(len(saved)))
                saved.append(lines[i + 1 : i + n])  # the wrapped call's tail only
                i += n - 1
            else:
                out.append(lines[i])
            i += 1
        return NL.join(out), saved

    def reindent_define(self, block, prefix):
        """Move a held-back #define block to the directive indent clang-format chose.

        Only the gap between `#` and `define` is at stake, but every line of a multi-line
        macro shifts with it, so the body and its continuation backslashes keep the
        column the author lined them up in. A block whose body has no room to move left
        is left exactly as it was -- the indent is worth less than the alignment.
        """
        m = self.RE_DEFINE_PREFIX.match(block[0])
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
        return [first] + [
            (" " * delta + l) if delta > 0 else l[-delta:] for l in block[1:]
        ]

    def restore_verbatim(self, text, saved):
        """Post-pass: put the held-back lines back, byte for byte -- except for a
        #define, whose `#` prefix follows the indent clang-format gave its stand-in."""
        out = []
        for line in text.split(NL):
            m = self.RE_KEEP_TAG.match(line)
            if m:
                out += saved[int(m.group("n"))]
                continue
            m = self.RE_KEEP_DEFINE.match(line)
            if m:
                out += self.reindent_define(saved[int(m.group("n"))], m.group("pre"))
                continue
            out.append(line)
        return NL.join(out)

    def hide_sidebars(self, text):
        """Pre-pass: disguise sidebar asserts/logs as trailing comments, tagged with the
        column the author put them in so the post-pass can put them back there."""
        out = []
        for line in text.split(NL):
            if not line.lstrip().startswith("#"):
                m = self.RE_SIDEBAR_HIDE.match(line)
                if m:
                    col = len(line[: m.start("tail")].expandtabs(self.TAB_WIDTH))
                    line = "{} {}{} {}".format(
                        m.group("code"), self.SIDEBAR_TAG, col, m.group("tail")
                    )
            out.append(line)
        return NL.join(out)

    def show_sidebars(self, text):
        """Post-pass: turn the disguised comments back into code, in the sidebar
        column."""
        out = []
        for line in text.split(NL):
            m = self.RE_SIDEBAR_SHOW.match(line)
            if m:
                code = m.group("pre").rstrip()
                col = max(
                    int(m.group("col")),  # the column the author chose
                    len(code) + 1,
                )  # unless the code now reaches it
                line = code + " " * (col - len(code)) + m.group("tail")
            out.append(line)
        return NL.join(out)

    def rehoist(self, text):
        """Post-pass: pull `_ (` back to column 0, the paren keeping the indent column."""
        out = []
        for line in text.split(NL):
            m = self.RE_HOISTED.match(line)
            if m:
                indent = len(m.group("indent"))
                # `_` takes column 0; the paren keeps the column the statement was
                # indented to, wherever clang-format happened to put it in the match
                line = "_" + " " * (indent - 1) + line[m.end() - 1 :]
            out.append(line)
        return NL.join(out)

    def strip_trailing(self, text):
        """Post-pass: take the trailing whitespace off every line, wherever it came
        from."""
        return NL.join(line.rstrip() for line in text.split(NL))


class PyFormatter:
    """The scripts: black, through its Python API, unconfigured.

    Nothing of the C side applies here -- black's defaults are the whole of the style,
    so there is no config to derive and no convention to hide and put back.
    """

    NAME = "black"
    SUFFIXES = (".py",)
    MAJOR = 26  # the release the tree is formatted with; see the module docstring

    def black(self):
        """black itself, imported on first use.

        Not imported at the top with the rest: a run over the C alone should not need it
        installed, the same way a run over the scripts alone never looks for
        clang-format.
        """
        try:
            import black
        except ImportError as e:
            raise FormatterError(f"cannot import {self.NAME}: {e}") from e
        return black

    def version(self):
        """The black that would be imported, as (major, printable version).

        Same shape as CFormatter.version, and `major` is None on the same terms: a
        version string nothing can be read out of.
        """
        version = self.black().__version__
        m = re.match(r"(\d+)\.", version)
        return (int(m.group(1)) if m else None), version

    def format(self, text, path):
        """`text` through black, whose own defaults are the whole of the style."""
        black = self.black()
        try:
            return black.format_str(text, mode=black.Mode())
        except black.InvalidInput as e:
            raise FormatterError(f"{self.NAME} failed on {path}: {e}") from e


C_FORMATTER = CFormatter()
PY_FORMATTER = PyFormatter()


def is_ours(path):
    """True if either formatter lays out a file named `path`."""
    return path.endswith(C_FORMATTER.SUFFIXES + PY_FORMATTER.SUFFIXES)


def formatter_for(path):
    """The formatter that lays out a file named `path`. The suffix decides; callers
    filter with is_ours() first, so the C one is not handed something it would mangle.
    """
    return PY_FORMATTER if path.endswith(PY_FORMATTER.SUFFIXES) else C_FORMATTER


def relative(path, base=ROOT):
    """Forward-slash path relative to `base`, the repo root by default -- what EXCLUDE's
    patterns, and a .gitignore's, are written against."""
    return os.path.relpath(path, base).replace(os.sep, "/")


def collect(paths):
    files = []
    for p in paths:
        full = p if os.path.isabs(p) else os.path.join(ROOT, p)
        if os.path.isfile(full):
            files.append(full)  # named outright, so format it, blacklist or not
            continue
        for dirpath, dirnames, filenames in os.walk(full):
            IGNORED.prune(dirpath, dirnames, isdir=True)
            IGNORED.prune(dirpath, filenames, isdir=False)
            for name in filenames:
                path = os.path.join(dirpath, name)
                if is_ours(path) and relative(path) not in EXCLUDE:
                    files.append(path)
    return sorted(files)


def check_version(formatter, any_version):
    """Stop unless `formatter`'s tool is the major the tree is formatted with."""
    major, version = formatter.version()
    if major == formatter.MAJOR:
        return
    problem = (
        f"this tree is formatted with {formatter.NAME} {formatter.MAJOR}.x, "
        f"but {version} is on PATH, and formatting differs between majors"
    )
    if not any_version:
        sys.exit(f"{problem}.\nPass --any-version to make this a warning.")
    print(f"warning: {problem}", file=sys.stderr)


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
        help="warn instead of stopping when a formatter is the wrong major",
    )
    ap.add_argument("paths", nargs="*")
    args = ap.parse_args()

    files = collect(args.paths or ["."])

    # collect() lets a file named outright through whatever the walk would filter,
    # so a name neither formatter takes is caught here, before anything is written.
    unknown = [relative(p) for p in files if not is_ours(p)]
    if unknown:
        sys.exit("nothing here formats " + ", ".join(unknown))

    # Only what this run needs: laying out the scripts does not ask for a clang-format
    # that is not installed, and laying out the C does not ask for black.
    if any(formatter_for(p) is C_FORMATTER for p in files):
        check_version(C_FORMATTER, args.any_version)
    if any(formatter_for(p) is PY_FORMATTER for p in files):
        check_version(PY_FORMATTER, args.any_version)

    failed = []
    for path in files:
        with open(path, "r", encoding="utf-8", newline="") as f:
            original = f.read()
        rel = relative(path)
        # A file may be checked out with CRLF. Format on LF, then hand the file back
        # with the endings it came with -- rewriting those would touch every line of
        # the file and swamp the actual formatting change.
        eol = "\r\n" if "\r\n" in original else NL
        formatted = formatter_for(path).format(original.replace("\r\n", NL), path)
        formatted = formatted.replace(NL, eol)
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
    except FormatterError as e:
        sys.exit(str(e))
