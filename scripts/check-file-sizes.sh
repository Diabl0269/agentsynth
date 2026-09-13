#!/usr/bin/env bash
#
# check-file-sizes.sh -- a hard line-count cap with a strict ratchet baseline, so this repo's
# files never grow into the state a few of them already reached (GraphEditor.cpp crossed 9,000
# lines; some test files passed 6,000). A file that size stops being reviewable: every change is
# a scroll through unrelated concerns, a small fix's diff balloons because unrelated context lines
# get dragged along, and two people working on different features in the same file collide on
# merge far more than they would in two smaller ones. The structure a file that size is hiding --
# which parts are really separate concerns -- only gets harder to see the longer it grows.
#
# The cap can't apply retroactively without freezing every legacy file mid-refactor, so a baseline
# (scripts/file-size-baseline.txt) grandfathers today's over-cap files at their EXACT current line
# count and never lets them grow past it -- a STRICT ratchet, not a one-time snapshot: it only
# ever tightens. Shrink a baselined file and the entry must tighten to match (--update does that);
# get it under the cap and the entry must be removed entirely (--update does that too). No file is
# ever allowed to join the baseline as new -- split it into per-concern units instead. This is the
# mechanism that stops the next GraphEditor.cpp from happening while the existing ones get split
# over time.
#
# One cap for everything -- code, tests, docs, scripts -- because a 6,000-line test file is just
# as unreviewable as a 6,000-line source file; a per-directory cap would only move the goalposts.
#
# Usage:
#   bash scripts/check-file-sizes.sh                  # check the tree against cap + baseline
#   bash scripts/check-file-sizes.sh --update         # rewrite the baseline from the current tree
#   bash scripts/check-file-sizes.sh --list [N]       # N largest scanned files, default 25
#   bash scripts/check-file-sizes.sh --root <dir>     # scan a different repo root (for tests)
#   bash scripts/check-file-sizes.sh -h|--help
#
# Environment:
#   FILE_SIZE_CAP       line-count cap, measured with `wc -l` (default 1000)
#   FILE_SIZE_BASELINE  baseline file path (default: <root>/scripts/file-size-baseline.txt) --
#                       lets scripts/tests/check-file-sizes.test.sh point this at a throwaway file
#
# Exit status (check mode only -- --update/--list/--help always exit 0 on success):
#   0  no violation
#   1  at least one over-cap-without-entry / grew-past-entry / shrank-without-update /
#      stale-baseline-entry violation
#
# Portable bash 3.2 (macOS default) + awk/sort/wc -- no python, no GNU-only flags.

set -uo pipefail

# File extensions this guard cares about -- source, tests, scripts, docs, config. One cap for
# all of them; see the header above for why there's no per-directory exception.
EXTENSIONS=(
    h      # C/C++ headers
    hpp    # C++ headers
    cpp    # C/C++ sources
    c      # C sources
    mm     # Objective-C++ sources (JUCE macOS glue)
    cmake  # CMake modules
    md     # Markdown docs
    sh     # bash scripts
    py     # Python scripts
    yml    # YAML (GitHub workflows, etc.)
    yaml   # YAML, alternate spelling
    txt    # plain text (also covers CMakeLists.txt; the basename check below is belt-and-braces)
    mjs    # ES module JavaScript
    js     # JavaScript
    json   # JSON data/config
)

# Path prefixes that never count, regardless of extension -- generated, vendored, or recorded-data
# trees whose size has nothing to do with hand-authored code structure.
EXCLUDED_PREFIXES=(
    "assets/"                             # binary/media assets, tracked but not code
    "mockups/"                            # design mockup HTML, not shipped code
    "build"                               # any local build output dir (build, build-ci-local, ...)
    ".claude/"                            # Claude Code workspace scratch (worktrees, etc.)
    "Tests/fixtures/"                     # recorded AI-patch JSON corpora, not authored files
    "Tools/TimelineOpsHarness/Fixtures/"  # recorded TimelineOps JSON fixtures, not authored files
)

# Exact-path exclusions: the guard's own generated data must never guard itself.
EXCLUDED_FILES=(
    "scripts/file-size-baseline.txt"
)

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-file-sizes.sh [--update] [--list [N]] [--root <dir>] [-h|--help]

Enforces a hard line-count cap (FILE_SIZE_CAP, default 1000) on every git-tracked source/test/
docs/config file, with a strict ratchet baseline (scripts/file-size-baseline.txt) grandfathering
legacy files already over the cap. See docs/testing.md "File-size cap (Lint job)" for the full
mechanism and how to split an over-cap file.

  (no flags)     Check the tree against the cap + baseline. Exit 1 on any violation.
  --update       Rewrite the baseline from the current tree (files over cap only, sorted) and
                 print what changed.
  --list [N]     Print the N (default 25) largest scanned files, largest first, regardless of
                 cap or baseline -- for planning a split.
  --root <dir>   Repo root to scan (default: `git rev-parse --show-toplevel` from this script's
                 own directory). Lets tests point this at a throwaway fixture repo.
  -h, --help     Show this message and exit.

Environment:
  FILE_SIZE_CAP       Line-count cap, via `wc -l` (default 1000).
  FILE_SIZE_BASELINE  Baseline file path (default: <root>/scripts/file-size-baseline.txt).
USAGE
}

MODE="check"
LIST_N=25
ROOT_OVERRIDE=""

while [ $# -gt 0 ]; do
    case "$1" in
        --update)
            MODE="update"
            shift
            ;;
        --list)
            MODE="list"
            shift
            if [ $# -gt 0 ]; then
                case "$1" in
                    ''|*[!0-9]*) ;; # not a plain positive integer -- leave LIST_N at the default
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
                echo "check-file-sizes: --root requires an argument" >&2
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
            echo "check-file-sizes: unknown argument '$1' (see --help)" >&2
            usage >&2
            exit 1
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -n "$ROOT_OVERRIDE" ]; then
    ROOT="$(cd "$ROOT_OVERRIDE" && pwd)"
else
    ROOT="$(cd "$SCRIPT_DIR" && git rev-parse --show-toplevel)"
fi

CAP="${FILE_SIZE_CAP:-1000}"
BASELINE_FILE="${FILE_SIZE_BASELINE:-$ROOT/scripts/file-size-baseline.txt}"

# list_scanned_paths -- every git-tracked path under $ROOT matching EXTENSIONS (or literally named
# CMakeLists.txt) and not excluded, one per line.
list_scanned_paths() {
    git -C "$ROOT" ls-files | while IFS= read -r path; do
        base="${path##*/}"
        ext="${path##*.}"

        is_match=0
        if [ "$base" = "CMakeLists.txt" ]; then
            is_match=1
        elif [ "$ext" != "$path" ]; then
            for e in "${EXTENSIONS[@]}"; do
                if [ "$ext" = "$e" ]; then
                    is_match=1
                    break
                fi
            done
        fi
        [ "$is_match" -eq 1 ] || continue

        excluded=0
        for prefix in "${EXCLUDED_PREFIXES[@]}"; do
            case "$path" in
                "$prefix"*)
                    excluded=1
                    break
                    ;;
            esac
        done
        if [ "$excluded" -eq 0 ]; then
            for exact in "${EXCLUDED_FILES[@]}"; do
                if [ "$path" = "$exact" ]; then
                    excluded=1
                    break
                fi
            done
        fi
        [ "$excluded" -eq 1 ] && continue

        printf '%s\n' "$path"
    done
}

# build_current_data -- "<lines> <path>" for every scanned path, unsorted.
build_current_data() {
    list_scanned_paths | while IFS= read -r path; do
        full="$ROOT/$path"
        if [ -f "$full" ]; then
            n="$(wc -l <"$full" | tr -d '[:space:]')"
        else
            n=0
        fi
        printf '%s %s\n' "$n" "$path"
    done
}

# baseline_data -- the baseline file's data lines ("<lines> <path>"), comments and blanks
# stripped. Empty (not an error) when the baseline file doesn't exist yet.
baseline_data() {
    [ -f "$BASELINE_FILE" ] || return 0
    grep -v '^#' "$BASELINE_FILE" | grep -v '^[[:space:]]*$'
}

write_step_summary() {
    [ -n "${GITHUB_STEP_SUMMARY:-}" ] && printf '%s\n' "$1" >>"$GITHUB_STEP_SUMMARY"
}

run_check() {
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT

    baseline_data >"$workdir/baseline.txt"
    build_current_data | sort -k2,2 >"$workdir/current.txt"

    # NOTE: deliberately NOT the classic `FNR==NR` two-file idiom -- that trick breaks when the
    # FIRST file (the baseline) is empty, which it legitimately is before the first --update: with
    # zero lines contributed by file 1, NR and FNR stay in lockstep for the ENTIRE second file too,
    # so every current-tree line would be misread as a baseline entry. Compare FILENAME instead.
    awk_out="$(awk -v cap="$CAP" -v baseline_file="$workdir/baseline.txt" '
        FILENAME == baseline_file {
            entry_count[$2] = $1 + 0
            next
        }
        {
            count[$2] = $1 + 0
            scanned[$2] = 1
            total++
            if ($1 + 0 > max_count) {
                max_count = $1 + 0
                max_path = $2
            }
        }
        END {
            errors = 0
            legacy = 0
            for (p in scanned) {
                if (count[p] > cap) {
                    legacy++
                    if (!(p in entry_count)) {
                        printf "::error::%s is %d lines (cap %d) and not in the baseline -- split it by concern (see docs/testing.md, section \"File-size cap\")\n", p, count[p], cap
                        errors++
                    } else if (count[p] > entry_count[p]) {
                        printf "::error::%s grew from %d to %d lines; the ratchet only tightens -- move new code into a new <Class><Concern>.cpp unit instead\n", p, entry_count[p], count[p]
                        errors++
                    } else if (count[p] < entry_count[p]) {
                        printf "::error::%s shrank from %d to %d lines -- run --update to tighten the baseline\n", p, entry_count[p], count[p]
                        errors++
                    }
                }
            }
            for (p in entry_count) {
                if (!(p in scanned)) {
                    printf "::error::%s is a stale baseline entry (file missing or no longer scanned) -- run --update\n", p
                    errors++
                } else if (count[p] <= cap) {
                    printf "::error::%s is a stale baseline entry (now %d lines, at or under the %d-line cap) -- run --update\n", p, count[p], cap
                    errors++
                }
            }
            if (max_path == "") {
                max_path = "(none)"
                max_count = 0
            }
            printf "SUMMARY %d %d %d %s\n", total + 0, legacy, max_count, max_path
            exit (errors > 0 ? 1 : 0)
        }
    ' "$workdir/baseline.txt" "$workdir/current.txt")"
    status=$?

    summary_line=""
    while IFS= read -r line; do
        case "$line" in
            "SUMMARY "*) summary_line="$line" ;;
            *) [ -n "$line" ] && echo "$line" ;;
        esac
    done <<<"$awk_out"

    scanned_total=0
    legacy_count=0
    max_count=0
    max_path="(none)"
    if [ -n "$summary_line" ]; then
        read -r _ scanned_total legacy_count max_count max_path <<<"$summary_line"
    fi

    echo "check-file-sizes: $scanned_total files scanned, $legacy_count over the ${CAP}-line cap, largest: $max_path ($max_count lines)"

    write_step_summary "### File-size guard"
    write_step_summary "- files scanned: $scanned_total"
    write_step_summary "- over the ${CAP}-line cap (legacy, baselined): $legacy_count"
    write_step_summary "- largest scanned file: $max_path ($max_count lines)"

    return "$status"
}

run_update() {
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT

    old_data="$(baseline_data | sort -k2,2)"
    build_current_data | awk -v cap="$CAP" '($1 + 0) > cap' | sort -k2,2 >"$workdir/new.txt"
    new_data="$(cat "$workdir/new.txt")"

    {
        echo "# Legacy files over the cap. STRICT ratchet: an entry is the exact current line count."
        echo "# A file may never grow past its entry; when it shrinks, run \`bash scripts/check-file-sizes.sh --update\`"
        echo "# so the entry tightens; once under the cap the entry must be removed (--update does that)."
        echo "# Never add a NEW file here -- split it instead."
        cat "$workdir/new.txt"
    } >"$BASELINE_FILE"

    entry_count="$(wc -l <"$workdir/new.txt" | tr -d '[:space:]')"

    if [ "$old_data" = "$new_data" ]; then
        echo "check-file-sizes --update: baseline unchanged ($entry_count entries)."
    else
        echo "check-file-sizes --update: baseline rewritten ($entry_count entries). Changes:"
        diff <(printf '%s\n' "$old_data") <(printf '%s\n' "$new_data") || true
    fi
}

run_list() {
    build_current_data | sort -k1,1nr | head -n "$LIST_N"
}

case "$MODE" in
    check) run_check ;;
    update) run_update ;;
    list) run_list ;;
esac
