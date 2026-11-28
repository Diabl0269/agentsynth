#!/usr/bin/env bash
#
# check-wording.sh -- guard for "casual UI wording bypasses the shared vocabulary".
#
# A vocabulary header (Source/**/*Words.h) is the one place the words of a setting or gesture live, so the menu,
# the hover tip, the Cmd+K row and the Preferences row cannot drift. Two rules keep wording in it:
#
#   (1) In every source file that includes a vocabulary header, a tooltip or a choice (setTooltip, setSearchTooltip,
#       addItem) is read from the vocabulary, never written as a string literal. A literal there is a violation
#       unless its line ends with `// wording-ok: <reason>` (the text belongs to no vocabulary). Files that already
#       hold such a literal are counted in scripts/wording-informal-baseline.txt, a strict ratchet: the count of a
#       file may fall, never rise, and a file may not be added. After cutting one, run `--update`.
#   (2) No string literal anywhere under Source/ holds a phrase from scripts/wording-informal-phrases.txt, the
#       list of casual phrases that were retired. No baseline: the tree has none, add a phrase when one is retired.
#
# Comments are not checked. Usage:
#   bash scripts/check-wording.sh [--root <dir>] [--baseline <file>] [--phrases <file>] [--update]
# Exit status 1 on a violation. No compiler, ~1 s.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$REPO_ROOT"
BASELINE=""
PHRASES=""
UPDATE=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --root) ROOT="$2"; shift 2 ;;
        --baseline) BASELINE="$2"; shift 2 ;;
        --phrases) PHRASES="$2"; shift 2 ;;
        --update) UPDATE=1; shift ;;
        -h | --help) sed -n 2,22p "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "check-wording: unknown argument $1" >&2; exit 2 ;;
    esac
done
BASELINE="${BASELINE:-$ROOT/scripts/wording-informal-baseline.txt}"
PHRASES="${PHRASES:-$ROOT/scripts/wording-informal-phrases.txt}"

python3 - "$ROOT" "$BASELINE" "$PHRASES" "$UPDATE" <<'PY'
import os
import re
import sys

root, baseline_path, phrases_path, update = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] == "1"
SUFFIXES = (".cpp", ".h", ".mm")
LITERAL_TIP = re.compile(r"\b(?:setTooltip|setSearchTooltip|addItem)\s*\(\s*(?:juce::String\s*\(\s*)?\"")
MARKER = re.compile(r"//\s*wording-ok:\s*\S")
INCLUDE_WORDS = re.compile(r'#\s*include\s*[<"][^>"]*Words\.h[>"]')


def literals_of(line, in_block):
    """(code outside literals, the string literals, in_block) for one line; comments dropped."""
    code, lits, cur = [], [], []
    in_str = in_chr = esc = False
    i, n = 0, len(line)
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
                cur.append(ch)
            elif ch == "\\":
                esc = True
                cur.append(ch)
            elif ch == '"':
                in_str = False
                lits.append("".join(cur))
                cur = []
            else:
                cur.append(ch)
        elif in_chr:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == "'":
                in_chr = False
        elif line.startswith("//", i):
            break
        elif line.startswith("/*", i):
            in_block = True
            i += 2
            continue
        elif ch == '"':
            in_str = True
            code.append('"')
        elif ch == "'":
            in_chr = True
        else:
            code.append(ch)
        i += 1
    return "".join(code), lits, in_block


def files(base):
    for dirpath, _d, names in os.walk(base):
        for name in sorted(names):
            if name.endswith(SUFFIXES):
                yield os.path.join(dirpath, name)


source = os.path.join(root, "Source")
phrases = []
if os.path.isfile(phrases_path):
    for line in open(phrases_path, encoding="utf-8"):
        line = line.strip()
        if line and not line.startswith("#"):
            phrases.append(line.lower())

scanned = 0
phrase_hits = []
literal_counts = {}
literal_lines = {}
for path in files(source):
    scanned += 1
    rel = os.path.relpath(path, root)
    text = open(path, encoding="utf-8", errors="replace").read()
    consumer = bool(INCLUDE_WORDS.search(text)) and not path.endswith("Words.h")
    in_block = False
    for number, line in enumerate(text.split("\n"), 1):
        code, lits, in_block = literals_of(line, in_block)
        if "R\"" in line:
            continue
        for lit in lits:
            low = lit.lower()
            for phrase in phrases:
                if phrase in low:
                    phrase_hits.append("%s:%d: casual phrase '%s' is retired: %s" % (rel, number, phrase, line.strip()[:120]))
        if consumer and LITERAL_TIP.search(code) and not MARKER.search(line):
            literal_counts[rel] = literal_counts.get(rel, 0) + 1
            literal_lines.setdefault(rel, []).append((number, line.strip()[:120]))

if scanned == 0:
    print("check-wording: 0 files scanned under %s -- wrong root; refusing a vacuous pass" % source, file=sys.stderr)
    sys.exit(1)

baseline = {}
if os.path.isfile(baseline_path):
    for line in open(baseline_path, encoding="utf-8"):
        line = line.strip()
        if line and not line.startswith("#"):
            path, _, count = line.partition("\t")
            baseline[path] = int(count)

if update:
    kept = {p: min(baseline[p], literal_counts.get(p, 0)) for p in baseline}
    kept = {p: c for p, c in kept.items() if c > 0}
    header = [
        "# Source files that include a vocabulary header (*Words.h) and still write a tooltip or choice as a string literal.",
        "# STRICT ratchet: a count may fall, never rise, and a file may never be added; read the words from the",
        "# vocabulary instead (or end a line that belongs to no vocabulary with `// wording-ok: <reason>`).",
        "# After cutting one, run `bash scripts/check-wording.sh --update` so the entry tightens.",
    ]
    with open(baseline_path, "w", encoding="utf-8") as out:
        out.write("\n".join(header) + "\n")
        for p in sorted(kept):
            out.write("%s\t%d\n" % (p, kept[p]))
    print("check-wording: baseline rewritten with %d file(s)." % len(kept))
    sys.exit(0)

problems = list(phrase_hits)
for rel in sorted(literal_counts):
    allowed = baseline.get(rel, 0)
    if literal_counts[rel] > allowed:
        for number, text in literal_lines[rel]:
            problems.append("%s:%d: tooltip or choice is a string literal, not read from the vocabulary header: %s" % (rel, number, text))
for rel, allowed in sorted(baseline.items()):
    if literal_counts.get(rel, 0) < allowed:
        problems.append("%s: baseline says %d literal(s) but %d remain: run `bash scripts/check-wording.sh --update`" % (rel, allowed, literal_counts.get(rel, 0)))

for line in problems:
    print(line)
if problems:
    print()
    print("Tooltips and choices come from the vocabulary header (Source/**/*Words.h), so every surface that shows a")
    print("setting reads the same words. Add the text there, or end the line with `// wording-ok: <reason>` when it")
    print("belongs to no vocabulary. A retired casual phrase is replaced by the vocabulary's wording.")
    sys.exit(1)
print("check-wording: OK (%d files)." % scanned)
PY
