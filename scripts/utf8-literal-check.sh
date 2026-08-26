#!/usr/bin/env bash
#
# utf8-literal-check.sh -- catch the mojibake class of bug at review time instead of
# screenshot-in-chat time.
#
# WHY: juce::String(const char*) decodes its input as CharPointer_ASCII -- one byte per code
# point, NOT UTF-8 -- even though this codebase's convention for non-ASCII text (an em dash, an
# emoji, an ellipsis) is to write it as UTF-8 byte escapes, e.g. "\xe2\x80\x94" for an em dash.
# Passed straight to a String-taking API (setText, operator+, the String(const char*) constructor
# itself), those three UTF-8 bytes decode as three separate Latin-1 code points instead of one em
# dash -- it doesn't fail to compile, and in a Release build it doesn't even assert, it just
# silently renders as "Thanks \xc3\xa2\xe2\x82\xac" on screen (P6-16, 2026-08-26 -- the same bug
# was already sitting unnoticed in two other, unrelated files). The only correct spelling in this
# codebase is juce::String::fromUTF8("...") or juce::CharPointer_UTF8("...") -- already used
# correctly in a dozen other places. This catches the next place someone forgets.
#
# Heuristic: a high-byte \x escape (\x80-\xFF -- the UTF-8 lead/continuation-byte range; no
# legitimate ASCII string needs one) must be preceded, within its own statement, by fromUTF8( or
# CharPointer_UTF8(. "Within its own statement" is approximated by scanning backward from the
# escape's line until a ';' closes the PRECEDING statement -- adjacent string literals and a
# wrapping call can span several lines, but never cross a ';'. // and /* */ comments are stripped
# first (an escape mentioned only in prose, e.g. documenting what a label looks like, is not a
# bug), and a line that's purely a `const char*`/`constexpr const char* NAME = "...";` declaration
# is skipped outright -- the raw bytes aren't wrong, only a would-be juce::String construction from
# them would be, and that's checked independently at whatever line actually does that. Not a real
# parser: it can't see a ';' or a comment marker hiding inside an unrelated string literal on the
# same line, but that combination hasn't come up in this codebase and isn't worth a real C++ parse
# just to guard against.
#
# Scope: Source/ and Tests/ only, mirroring the Lint job's clang-format sweep -- Tools/ and
# build/_deps (JUCE's own sources) are out of scope.
#
# UTF8_CHECK_ROOT: override the repo root to scan (default: this script's own repo) -- lets
# scripts/tests/utf8-literal-check.test.sh point this at a throwaway fixture repo.
#
# Usage: bash scripts/utf8-literal-check.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="${UTF8_CHECK_ROOT:-$SCRIPT_DIR}"

python3 - "$REPO_DIR" <<'PYEOF'
import re
import subprocess
import sys

repo_dir = sys.argv[1]

# The UTF-8 lead/continuation-byte range -- no legitimate ASCII string literal needs one of these.
ESCAPE_RE = re.compile(r'\\x[89A-Fa-f][0-9A-Fa-f]')
GUARD_RE = re.compile(r'fromUTF8\s*\(|CharPointer_UTF8\s*\(')
# A pure C-string constant declaration -- the escape lives in raw byte data here, not yet
# constructed into a juce::String. Whatever *uses* the constant is checked independently, at
# its own call site.
RAW_DECL_RE = re.compile(r'^\s*(constexpr\s+|static\s+)*const\s+char\s*\*\s*\w+\s*=\s*"')


def strip_comments(raw_lines):
    """Blanks out //... and /*...*/ spans so an escape mentioned only in prose, or inside a raw
    string constant's own trailing comment, never gets mistaken for a live statement."""
    stripped = []
    in_block = False
    for raw in raw_lines:
        out = raw
        if in_block:
            end = out.find("*/")
            if end == -1:
                stripped.append("")
                continue
            out = out[end + 2 :]
            in_block = False
        while True:
            start = out.find("/*")
            if start == -1:
                break
            end = out.find("*/", start + 2)
            if end == -1:
                out = out[:start]
                in_block = True
                break
            out = out[:start] + out[end + 2 :]
        line_start = out.find("//")
        if line_start != -1:
            out = out[:line_start]
        stripped.append(out)
    return stripped


result = subprocess.run(
    ["git", "-C", repo_dir, "ls-files", "Source", "Tests"],
    capture_output=True,
    text=True,
    check=True,
)
files = result.stdout.splitlines()

violations = []
for rel_path in files:
    if not (rel_path.endswith(".cpp") or rel_path.endswith(".h")):
        continue
    with open(f"{repo_dir}/{rel_path}", encoding="utf-8", errors="replace") as f:
        raw_lines = f.readlines()
    lines = strip_comments(raw_lines)

    for i, line in enumerate(lines):
        if not ESCAPE_RE.search(line):
            continue
        if RAW_DECL_RE.search(line):
            continue
        guarded = False
        for j in range(i, -1, -1):
            if j != i and ";" in lines[j]:
                break
            if GUARD_RE.search(lines[j]):
                guarded = True
                break
        if not guarded:
            violations.append(f"{rel_path}:{i + 1}: {raw_lines[i].strip()}")

if violations:
    print(
        "Non-ASCII \\x escape(s) not wrapped in juce::String::fromUTF8(...) / "
        "juce::CharPointer_UTF8(...):\n"
    )
    for v in violations:
        print(f"  {v}")
    print(
        "\njuce::String(const char*) decodes as ASCII (one byte per code point), not UTF-8 -- "
        'these will render as mojibake. Wrap the literal in juce::String::fromUTF8("...").'
    )
    sys.exit(1)
PYEOF
