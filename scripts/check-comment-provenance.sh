#!/usr/bin/env bash
#
# check-comment-provenance.sh -- a comment-CONTENT guard with a strict ratchet baseline, sibling to
# check-file-sizes.sh / check-function-sizes.sh / check-header-comments.sh. Comments in Source/ and
# Tests/ state behaviour, invariants and reasons; they never record provenance -- ticket ids, dates,
# review rounds, who asked for a change. That history lives in `git log` / `git blame`, where it is
# accurate and free, whereas a provenance note in a comment goes stale the moment the code around
# it moves and teaches every reader to skim comments instead of trusting them.
#
# WHAT IS COUNTED. For every git-tracked Source/** and Tests/** C/C++/ObjC file (.h .hpp .cpp .mm
# .c), the number of LINES whose comment text matches PATTERN (extended regex, case-sensitive,
# word-bounded):
#
#   (FRO|BAC|WEB|MAR|AGE|PRO)[0-9]+   a ticket id
#   P[0-9]+-[0-9]+                    a phase/step id
#   T1[0-9][0-9]                      a T1xx task id
#   20[0-9][0-9]-[0-9][0-9]-[0-9][0-9]  an ISO date
#
# Only the comment text is matched. A `//` comment runs to the end of its line; a `/* ... */`
# comment may span lines, and every line inside it is comment text. Code and string/char literals
# on the same line are skipped, so a test fixture that builds a date string does not count. This is
# a deliberate HEURISTIC scanner, not a C++ parser: it tracks double-quoted strings and char
# literals (a quote directly after an alphanumeric is a digit separator, not a char literal) but
# not raw string literals that span lines.
#
# EXEMPTION (Tests/ only). A line whose comment text matches
#   Regression test for (FRO|BAC|WEB|MAR|AGE|PRO)[0-9]+:
# is not counted -- a test that pins one specific past bug may cite it in exactly that form. The
# same text under Source/ is counted like any other provenance.
#
# RATCHET. Legacy provenance can't be rewritten in one change, so scripts/comment-provenance-
# baseline.txt grandfathers each file at its EXACT current count and never lets it grow -- a STRICT
# ratchet exactly like the sibling guards: a file absent from the baseline must have 0; a baselined
# file may not exceed its entry; a shrunk entry must be tightened and a zeroed one removed (--update
# does both); --update refuses to raise an entry or add a file unless --allow-growth.
#
# Usage:
#   bash scripts/check-comment-provenance.sh                  # check the tree against the baseline
#   bash scripts/check-comment-provenance.sh --update         # rewrite the baseline from the tree
#   bash scripts/check-comment-provenance.sh --update --allow-growth  # ...and let growth through
#   bash scripts/check-comment-provenance.sh --list [N]       # N files with the most matching lines
#   bash scripts/check-comment-provenance.sh --root <dir>     # scan a different repo root (tests)
#   bash scripts/check-comment-provenance.sh -h|--help
#
# Environment:
#   COMMENT_PROVENANCE_BASELINE  baseline file path (default:
#                                <root>/scripts/comment-provenance-baseline.txt) -- lets
#                                scripts/tests/check-comment-provenance.test.sh use a throwaway file
#
# Exit status (check mode only -- --update/--list/--help always exit 0 on success):
#   0  no violation
#   1  at least one new-file / grew-past-entry / shrank-without-update / stale-entry violation
#
# Portable: bash 3.2 (macOS) + POSIX awk (mawk, gawk and BSD awk all work -- no \b, no {n}
# intervals, no gensub, no ENDFILE) + sort/wc. No GNU-only flags.

set -uo pipefail

# A git hook (or a nested harness run) exports GIT_DIR (and sometimes GIT_WORK_TREE/GIT_INDEX_FILE).
# With GIT_DIR set and no matching GIT_WORK_TREE, `git rev-parse --show-toplevel` stops doing normal
# discovery, so ROOT can resolve to the wrong directory, every scanned file reads as missing, and
# the check passes vacuously. Strip the inherited git env so every git call below does normal
# discovery, whether run standalone, from a hook, or from a linked worktree.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-comment-provenance.sh [--update] [--allow-growth] [--list [N]] [--root <dir>] [-h|--help]

Counts, per git-tracked Source/** and Tests/** C/C++/ObjC file, the comment lines that carry
provenance (a ticket id or an ISO date) and enforces a strict ratchet baseline
(scripts/comment-provenance-baseline.txt): a file not in the baseline must have none, a baselined
file may not exceed its entry. Comments state behaviour and reasons; history lives in git. See
docs/development/comment-provenance-guard.md for the pattern, the rewrite rule and the
Tests/-only "Regression test for <ID>:" exception.

  (no flags)     Check the tree against the baseline. Exit 1 on any violation.
  --update       Rewrite the baseline from the current tree (files with provenance only, sorted)
                 and print what changed. Refuses to raise an existing entry or add a new file
                 (exit 1, baseline left untouched) -- pass --allow-growth to let it through as a
                 deliberate, reviewed exception.
  --allow-growth Only meaningful with --update: let a raised entry or new file through, printing a
                 ::warning:: per one so it stays visible in CI logs and PR review.
  --list [N]     Print the N (default 25) files with the most matching lines, as
                 "<count> <path>", regardless of baseline.
  --root <dir>   Repo root to scan (default: `git rev-parse --show-toplevel` from this script's
                 own directory). Lets tests point this at a throwaway fixture repo.
  -h, --help     Show this message and exit.

Environment:
  COMMENT_PROVENANCE_BASELINE  Baseline file path (default: <root>/scripts/comment-provenance-baseline.txt).
USAGE
}

MODE="check"
LIST_N=25
ROOT_OVERRIDE=""
ALLOW_GROWTH=0

while [ $# -gt 0 ]; do
    case "$1" in
        --update)
            MODE="update"
            shift
            ;;
        --allow-growth)
            ALLOW_GROWTH=1
            shift
            ;;
        --list)
            MODE="list"
            shift
            if [ $# -gt 0 ]; then
                case "$1" in
                    ''|*[!0-9]*) ;; # not a plain integer -- leave LIST_N at the default
                    *)
                        LIST_N="$1"
                        shift
                        ;;
                esac
            fi
            ;;
        --root)
            shift
            if [ $# -eq 0 ]; then
                echo "check-comment-provenance: --root requires an argument" >&2
                exit 1
            fi
            ROOT_OVERRIDE="$1"
            shift
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "check-comment-provenance: unknown argument '$1' (see --help)" >&2
            usage >&2
            exit 1
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -n "$ROOT_OVERRIDE" ]; then
    ROOT="$(cd "$ROOT_OVERRIDE" && pwd)"
else
    # Fail loudly rather than guess: a wrong/empty ROOT must never reach the scan below.
    if ! ROOT="$(cd "$SCRIPT_DIR" && git rev-parse --show-toplevel)"; then
        echo "::error::check-comment-provenance: 'git rev-parse --show-toplevel' failed from $SCRIPT_DIR -- refusing to guess ROOT (pass --root explicitly to override)" >&2
        exit 1
    fi
fi

BASELINE_FILE="${COMMENT_PROVENANCE_BASELINE:-$ROOT/scripts/comment-provenance-baseline.txt}"

# list_scanned_paths -- every git-tracked Source/** or Tests/** C/C++/ObjC path under $ROOT.
list_scanned_paths() {
    git -C "$ROOT" ls-files | while IFS= read -r path; do
        case "$path" in
            Source/*.h | Source/*.hpp | Source/*.cpp | Source/*.mm | Source/*.c) ;;
            Tests/*.h | Tests/*.hpp | Tests/*.cpp | Tests/*.mm | Tests/*.c) ;;
            *) continue ;;
        esac
        printf '%s\n' "$path"
    done
}

# The awk scanner. Reads any number of files (paths relative to ROOT, so the prefix decides whether
# the Tests/ exemption applies) and, per file, prints "H <path> <line-number> <trimmed source line>"
# for every line whose comment text matches the provenance pattern, then "S <path> <hit-count>
# <line-count>". Per-file state is reset when a new file starts (FNR == 1); an EMPTY file never
# starts, so it prints no S line and callers treat a missing S line as "0 0". Word boundaries are
# spelled out with [^A-Za-z0-9_] because \b is not portable across awk implementations.
SCAN_AWK='
function flush() {
    if (cur != "") printf "S %s %d %d\n", cur, hits, last
}
BEGIN {
    inblk = 0
    pat = "(^|[^A-Za-z0-9_])((FRO|BAC|WEB|MAR|AGE|PRO)[0-9]+|P[0-9]+-[0-9]+|T1[0-9][0-9]|20[0-9][0-9]-[0-9][0-9]-[0-9][0-9])([^A-Za-z0-9_]|$)"
    exempt = "Regression test for (FRO|BAC|WEB|MAR|AGE|PRO)[0-9]+:"
    hits = 0
}
FNR == 1 {
    flush()
    cur = FILENAME
    hits = 0
    inblk = 0
    is_tests = (substr(FILENAME, 1, 6) == "Tests/") ? 1 : 0
}
{
    last = FNR
    line = $0
    # Fast paths. Outside a block comment, a line with no "//" or "/*" has no comment text. And a
    # line that does not match the pattern anywhere cannot have matching comment text, so it only
    # needs the full scan when it opens or closes a block comment (to keep inblk right).
    if (!inblk && index(line, "//") == 0 && index(line, "/*") == 0) next
    if (line !~ pat && index(line, "/*") == 0 && index(line, "*/") == 0) next
    n = length(line)
    txt = ""
    i = 1
    while (i <= n) {
        if (inblk) {
            j = index(substr(line, i), "*/")
            if (j == 0) { txt = txt " " substr(line, i); i = n + 1 }
            else { txt = txt " " substr(line, i, j - 1); i = i + j + 1; inblk = 0 }
            continue
        }
        c = substr(line, i, 1)
        if (c == "\"" || c == "\047") {
            # A quote glued to an alphanumeric is a digit separator (1\0470000), not a char literal.
            if (c == "\047" && i > 1 && substr(line, i - 1, 1) ~ /[A-Za-z0-9_]/) { i++; continue }
            i++
            while (i <= n) {
                d = substr(line, i, 1)
                if (d == "\\") i += 2
                else if (d == c) { i++; break }
                else i++
            }
            continue
        }
        if (c == "/") {
            d = substr(line, i + 1, 1)
            if (d == "/") { txt = txt " " substr(line, i + 2); break }
            if (d == "*") { inblk = 1; i += 2; continue }
        }
        i++
    }
    if (txt == "") next
    if (txt !~ pat) next
    if (is_tests == 1 && txt ~ exempt) next
    shown = line
    sub(/^[ \t]+/, "", shown)
    printf "H %s %d %s\n", cur, FNR, shown
    hits++
}
END { flush() }
'

# scan_paths -- read repo-relative paths on stdin, run the scanner over the ones that exist as
# files (batched through xargs so a tree of thousands of files stays fast without nearing the
# argument-length limit) and print its H/S lines. The scan runs from ROOT so paths stay relative.
scan_paths() {
    (
        cd "$ROOT" || exit 1
        while IFS= read -r path; do
            [ -f "$path" ] && printf '%s\n' "$path"
        done | xargs -n 150 awk "$SCAN_AWK"
    )
}

# baseline_data -- the baseline's data lines ("<count> <path>"), comments and blanks stripped.
baseline_data() {
    [ -f "$BASELINE_FILE" ] || return 0
    grep -v '^#' "$BASELINE_FILE" | grep -v '^[[:space:]]*$'
}

write_step_summary() {
    [ -n "${GITHUB_STEP_SUMMARY:-}" ] && printf '%s\n' "$1" >>"$GITHUB_STEP_SUMMARY"
}

# build_current_data -- "<hits> <lines> <path>" for every scanned file, unsorted. A scanned file
# with no S line (empty or missing on disk) reads as "0 0".
build_current_data() {
    local paths scan
    paths="$(mktemp)"
    scan="$(mktemp)"
    list_scanned_paths >"$paths"
    scan_paths <"$paths" | grep '^S ' >"$scan"
    awk -v scan_file="$scan" '
        FILENAME == scan_file { hits[$2] = $3; lines[$2] = $4; next }
        { printf "%d %d %s\n", hits[$0] + 0, lines[$0] + 0, $0 }
    ' "$scan" "$paths"
    rm -f "$paths" "$scan"
}

# show_hits <path> -- print the offending lines of one file, indented "path:line: text".
show_hits() {
    printf '%s\n' "$1" | scan_paths | grep '^H ' | while IFS= read -r hit; do
        rest="${hit#H * }"
        num="${rest%% *}"
        text="${rest#* }"
        printf '    %s:%s: %s\n' "$1" "$num" "$text"
    done
}

FIX_HINT="rewrite each comment to state the behaviour or reason without the ticket id / date (history lives in git) -- see docs/development/comment-provenance-guard.md"

run_check() {
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT

    baseline_data >"$workdir/baseline.txt"
    build_current_data | sort -k3,3 >"$workdir/current.txt"

    # Compare FILENAME rather than the FNR==NR idiom: that idiom breaks when the FIRST file (the
    # baseline) is legitimately empty, which it is until the first --update.
    awk_out="$(awk -v baseline_file="$workdir/baseline.txt" -v hint="$FIX_HINT" '
        FILENAME == baseline_file {
            entry[$2] = $1 + 0
            next
        }
        {
            hits[$3] = $1 + 0
            nlines[$3] = $2 + 0
            scanned[$3] = 1
            total++
            if (($2 + 0) == 0) empty_count++
            if (($1 + 0) > 0) { flagged++; sum += $1 + 0 }
        }
        END {
            errors = 0
            for (p in scanned) {
                h = hits[p]
                if (h == 0) continue
                if (!(p in entry)) {
                    printf "::error::%s has %d comment line(s) carrying provenance (ticket id / date) and is not in the baseline -- %s\n", p, h, hint
                    printf "SHOW %s\n", p
                    errors++
                } else if (h > entry[p]) {
                    printf "::error::%s grew from %d to %d provenance comment line(s); the ratchet only tightens -- %s\n", p, entry[p], h, hint
                    printf "SHOW %s\n", p
                    errors++
                } else if (h < entry[p]) {
                    printf "::error::%s shrank from %d to %d provenance comment line(s) -- run --update to tighten the baseline\n", p, entry[p], h
                    errors++
                }
            }
            for (p in entry) {
                if (!(p in scanned)) {
                    printf "::error::%s is a stale baseline entry (file missing or no longer scanned) -- run --update\n", p
                    errors++
                } else if (hits[p] == 0) {
                    printf "::error::%s is a stale baseline entry (no provenance left) -- run --update\n", p
                    errors++
                }
            }
            # Fail safe: every scanned file reading back as zero lines looks identical to a clean
            # pass, but a real tree never does -- ROOT is wrong (a stray GIT_DIR is the usual cause).
            if (total + 0 > 1 && empty_count + 0 == total + 0) {
                printf "::error::check-comment-provenance read every one of %d scanned files as empty -- ROOT is almost certainly wrong (a stray GIT_DIR/GIT_WORK_TREE in the environment is the usual cause); refusing to report a vacuous pass\n", total + 0
                errors++
            }
            printf "SUMMARY %d %d %d\n", total + 0, flagged + 0, sum + 0
            exit (errors > 0 ? 1 : 0)
        }
    ' "$workdir/baseline.txt" "$workdir/current.txt")"
    status=$?

    summary_line=""
    while IFS= read -r line; do
        case "$line" in
            "SUMMARY "*) summary_line="$line" ;;
            "SHOW "*) show_hits "${line#SHOW }" ;;
            *) [ -n "$line" ] && echo "$line" ;;
        esac
    done <<<"$awk_out"

    scanned_total=0
    flagged_count=0
    hit_sum=0
    if [ -n "$summary_line" ]; then
        read -r _ scanned_total flagged_count hit_sum <<<"$summary_line"
    fi

    echo "check-comment-provenance: $scanned_total files scanned, $flagged_count with provenance comments (baselined), $hit_sum provenance comment lines in total"

    write_step_summary "### Comment-provenance guard"
    write_step_summary "- files scanned: $scanned_total"
    write_step_summary "- files with provenance comments (legacy, baselined): $flagged_count"
    write_step_summary "- provenance comment lines in total: $hit_sum"

    return "$status"
}

run_update() {
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT

    baseline_data | sort -k2,2 >"$workdir/old.txt"
    build_current_data | awk '$1 > 0 { print $1, $3 }' | sort -k2,2 >"$workdir/new.txt"

    old_data="$(cat "$workdir/old.txt")"
    new_data="$(cat "$workdir/new.txt")"

    # The first --update ever (no baseline on disk yet) is a one-time bootstrap, not growth.
    baseline_existed=0
    [ -f "$BASELINE_FILE" ] && baseline_existed=1

    growth_errors=0
    growth_warnings=0

    if [ "$baseline_existed" -eq 1 ]; then
        # (a) a file already baselined whose count rose.
        while IFS=' ' read -r new_count path; do
            [ -n "$path" ] || continue
            old_count="$(awk -v p="$path" '$2 == p { print $1 }' "$workdir/old.txt")"
            [ -n "$old_count" ] || continue
            if [ "$new_count" -gt "$old_count" ]; then
                if [ "$ALLOW_GROWTH" -eq 1 ]; then
                    echo "::warning::$path raised from $old_count to $new_count provenance comment lines -- allowed via --allow-growth (reviewed exception)"
                    growth_warnings=$((growth_warnings + 1))
                else
                    echo "::error::$path would raise the baseline from $old_count to $new_count provenance comment lines -- the ratchet only tightens; rewrite the new comments without provenance, or pass --allow-growth only for a deliberate, reviewed exception"
                    growth_errors=$((growth_errors + 1))
                fi
            fi
        done <"$workdir/new.txt"

        # (b) a file with provenance that the old baseline did not cover at all.
        while IFS=' ' read -r new_count path; do
            [ -n "$path" ] || continue
            if awk -v p="$path" '$2 == p { f = 1 } END { exit !f }' "$workdir/old.txt"; then
                continue # handled by (a)
            fi
            if [ "$ALLOW_GROWTH" -eq 1 ]; then
                echo "::warning::$path is a new file with provenance comments ($new_count lines) -- allowed via --allow-growth (reviewed exception)"
                growth_warnings=$((growth_warnings + 1))
            else
                echo "::error::$path is a new file with provenance comments ($new_count lines) -- the baseline never grows by adding files; rewrite the comments without provenance, or pass --allow-growth only for a deliberate, reviewed exception"
                growth_errors=$((growth_errors + 1))
            fi
        done <"$workdir/new.txt"
    fi

    if [ "$growth_errors" -gt 0 ]; then
        echo "check-comment-provenance --update: refusing to write the baseline -- $growth_errors entr$( [ "$growth_errors" -eq 1 ] && echo y || echo ies ) would raise it or add to it (see errors above). The ratchet only tightens; pass --allow-growth only for a deliberate, reviewed exception." >&2
        return 1
    fi

    {
        echo "# Files with provenance (ticket ids / dates) in code comments. STRICT ratchet: an entry is"
        echo "# the exact current number of comment lines matching the pattern. A file may never exceed"
        echo "# its entry; when it shrinks, run \`bash scripts/check-comment-provenance.sh --update\` so"
        echo "# the entry tightens; once it reaches zero the entry must be removed (--update does that)."
        echo "# Never add a NEW file here -- rewrite its comments instead. Format: <count> <path>."
        cat "$workdir/new.txt"
    } >"$BASELINE_FILE"

    entry_count="$(wc -l <"$workdir/new.txt" | tr -d '[:space:]')"

    if [ "$old_data" = "$new_data" ]; then
        echo "check-comment-provenance --update: baseline unchanged ($entry_count entries)."
    else
        if [ "$growth_warnings" -gt 0 ]; then
            echo "check-comment-provenance --update: baseline rewritten ($entry_count entries, $growth_warnings raised via --allow-growth). Changes:"
        else
            echo "check-comment-provenance --update: baseline rewritten ($entry_count entries). Changes:"
        fi
        diff <(printf '%s\n' "$old_data") <(printf '%s\n' "$new_data") || true
    fi
}

run_list() {
    build_current_data | awk '{ print $1, $3 }' | sort -k1,1nr | head -n "$LIST_N"
}

case "$MODE" in
    check) run_check ;;
    update) run_update ;;
    list) run_list ;;
esac
