#!/usr/bin/env bash
#
# Guard: UI text is never written in ALL CAPS.
#
# Fails on, under Source/UI/ (or the paths given):
#   (a) any call to toUpperCase() / toupper() in code, and
#   (b) any string literal containing a word of 4+ consecutive capital letters that is not a known
#       abbreviation (the list below; add to it deliberately).
# Comments are not checked. A line that is not UI text (a hex colour code, a card title awaiting its
# own change) opts out with a trailing marker that states why:
#
#     auto hex = s.toUpperCase(); // not-ui-text: hex colour code
#
# Usage: bash scripts/check-ui-caps.sh [<file-or-dir> ...]     (default: Source/UI)
# Exit status 1 when anything is flagged. Runs in the Lint job; no compiler, ~1 s.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ "$#" -eq 0 ]; then
    set -- "$REPO_ROOT/Source/UI"
fi

python3 - "$@" <<'PY'
import os
import re
import sys

SUFFIXES = (".cpp", ".h", ".mm")

# Words that are written in capitals everywhere (formats, protocols, units, standards). Only 4+ letter
# words can be flagged at all; the 3-letter ones are listed so the intent is on record.
ABBREVIATIONS = {
    "ADSR", "AIFF", "ASCII", "BPM", "CPU", "DAW", "FFT", "FLAC", "HTTP", "HTTPS", "JSON", "LFO", "LUFS",
    "MIDI", "MPE", "NRPN", "OSC", "RMS", "UTF", "USB", "VCA", "VST", "WAV",
}

CAPS_WORD = re.compile(r"(?<![A-Za-z])[A-Z]{4,}(?![a-z])")
UPPER_CALL = re.compile(r"\b(?:toUpperCase|toupper)\s*\(")
MARKER = re.compile(r"//\s*not-ui-text:\s*\S")


def scan_line(line, in_block):
    """Returns (code_without_literals, literals, in_block_after) for one source line."""
    code = []
    literals = []
    i = 0
    n = len(line)
    in_str = False
    in_chr = False
    esc = False
    current = []
    while i < n:
        ch = line[i]
        if in_block:
            if line.startswith("*/", i):
                in_block = False
                i += 2
            else:
                i += 1
            continue
        if in_str:
            if esc:
                esc = False
                current.append(ch)
            elif ch == "\\":
                esc = True
                current.append(ch)
            elif ch == '"':
                in_str = False
                literals.append("".join(current))
                current = []
            else:
                current.append(ch)
            i += 1
            continue
        if in_chr:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == "'":
                in_chr = False
            i += 1
            continue
        if line.startswith("//", i):
            break
        if line.startswith("/*", i):
            in_block = True
            i += 2
            continue
        if ch == '"':
            in_str = True
            i += 1
            continue
        if ch == "'":
            in_chr = True
            i += 1
            continue
        code.append(ch)
        i += 1
    return "".join(code), literals, in_block


def scan_file(path):
    hits = []
    with open(path, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().split("\n")
    in_block = False
    for number, line in enumerate(lines, 1):
        code, literals, in_block = scan_line(line, in_block)
        if MARKER.search(line) or 'R"' in line:  # raw string literals have their own quoting
            continue
        if UPPER_CALL.search(code):
            hits.append((number, "toUpperCase() makes UI text all caps", line.strip()))
            continue
        for literal in literals:
            bad = [w for w in CAPS_WORD.findall(literal) if w not in ABBREVIATIONS]
            if bad:
                hits.append((number, "all-caps word " + ", ".join(sorted(set(bad))), line.strip()))
                break
    return hits


def walk(root):
    if os.path.isfile(root):
        yield root
        return
    for dirpath, _dirs, files in os.walk(root):
        for name in sorted(files):
            if name.endswith(SUFFIXES):
                yield os.path.join(dirpath, name)


total = 0
for root in sys.argv[1:]:
    for path in walk(root):
        for number, kind, text in scan_file(path):
            print("%s:%d: %s: %s" % (path, number, kind, text[:140]))
            total += 1

if total:
    print()
    print("UI text is never all caps: write it in normal case (Title Case for titles, sentence case for")
    print("buttons) and drop toUpperCase(). See docs/development/accessibility.md#no-all-caps-ui-text.")
    print("Not UI text (hex code, identifier)? End the line with: // not-ui-text: <reason>")
    sys.exit(1)
PY
