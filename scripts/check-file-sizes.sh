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
# ever allowed to join the baseline as new -- split it into per-concern units instead. --update
# itself enforces this: it never raises an existing entry or adds a new one, and --allow-growth is
# the deliberate, reviewed exception for the rare case that really needs one. This is the
# mechanism that stops the next GraphEditor.cpp from happening while the existing ones get split
# over time.
#
# One cap for everything -- code, tests, docs, scripts -- because a 6,000-line test file is just
# as unreviewable as a 6,000-line source file; a per-directory cap would only move the goalposts.
#
# Usage:
#   bash scripts/check-file-sizes.sh                  # check the tree against cap + baseline
#   bash scripts/check-file-sizes.sh --update         # rewrite the baseline from the current tree
#   bash scripts/check-file-sizes.sh --update --allow-growth  # ...and let a raised entry through
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

# Git hooks run with GIT_DIR (and sometimes GIT_WORK_TREE/GIT_INDEX_FILE) exported into the
# environment. With GIT_DIR set and no matching GIT_WORK_TREE, `git rev-parse --show-toplevel`
# stops doing normal discovery from the current directory, so ROOT below can resolve to the wrong
# path (e.g. this script's own directory) -- every scanned file then reads as missing (n=0), and
# the check passes vacuously with nothing actually scanned. Strip the inherited git env so every
# git call below does normal discovery, whether run standalone, from the pre-commit hook, or from
# a linked worktree.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

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
                 print what changed. Refuses to raise an existing entry or add a new over-cap
                 file to the baseline (exit 1, baseline left untouched) -- pass --allow-growth
                 to let it through as a deliberate, reviewed exception.
  --allow-growth Only meaningful with --update: let a raised entry or new over-cap file through,
                 printing a ::warning:: per one so it stays visible in CI logs and PR review.
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
    # Fail loudly rather than silently: if this can't resolve, a wrong/empty ROOT must never
    # reach the scan below (see the awk fail-safe further down for the belt-and-braces backstop).
    if ! ROOT="$(cd "$SCRIPT_DIR" && git rev-parse --show-toplevel)"; then
        echo "::error::check-file-sizes: 'git rev-parse --show-toplevel' failed from $SCRIPT_DIR -- refusing to guess ROOT (pass --root explicitly to override)" >&2
        exit 1
    fi
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
            if ($1 + 0 == 0) {
                zero_count++
            }
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
            # Fail safe: reading EVERY scanned file as 0 lines (see the GIT_DIR note near the top
            # of this script -- a wrong ROOT makes every `$ROOT/$path` lookup miss the filesystem)
            # looks identical to a clean pass: 0 legacy files, no errors. A real tree never has
            # every one of several tracked, non-empty-by-construction files read back as empty --
            # an audit that silently matches nothing is the same blind spot it exists to close, so
            # this shape fails loudly instead of reporting a vacuous pass. (Scanning zero files is
            # NOT itself flagged here -- a repo or --root with no matching tracked files is legitimate,
            # e.g. everything under it happens to be excluded.)
            if (total + 0 > 1 && zero_count + 0 == total + 0) {
                printf "::error::check-file-sizes read every one of %d scanned files as 0 lines -- ROOT is almost certainly wrong (a stray GIT_DIR/GIT_WORK_TREE in the environment is the usual cause); refusing to report a vacuous pass\n", total + 0
                errors++
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

    baseline_data | sort -k2,2 >"$workdir/old.txt"
    build_current_data | awk -v cap="$CAP" '($1 + 0) > cap' | sort -k2,2 >"$workdir/new.txt"

    old_data="$(cat "$workdir/old.txt")"
    new_data="$(cat "$workdir/new.txt")"

    # The ratchet only makes sense once a baseline actually exists -- the very first --update
    # ever run against a repo (no baseline file on disk yet) is a one-time bootstrap that grand-
    # fathers whatever is over cap today, not a "new file joining the baseline" violation. Every
    # entry looks "new" against a nonexistent baseline, so skip the growth/new-file checks below
    # entirely in that case; once the baseline exists, every later --update is checked normally.
    baseline_existed=0
    [ -f "$BASELINE_FILE" ] && baseline_existed=1

    # Rename pairs ("<old-path> <new-path>") git can confirm for THIS tree: a staged `git mv`
    # (add the move, run --update before committing) via `git diff --cached -M`, plus anything
    # `git status --porcelain`'s own rename detection already found. Best-effort only -- a rename
    # git can't confirm falls through to the ordinary new-file check below and needs
    # --allow-growth like any other addition.
    : >"$workdir/renames.txt"
    git -C "$ROOT" diff --cached -M --name-status 2>/dev/null |
        awk -F'\t' '$1 ~ /^R/ { print $2, $3 }' >>"$workdir/renames.txt"
    git -C "$ROOT" status --porcelain 2>/dev/null |
        awk '
            /^R/ {
                line = $0
                sub(/^R[ MDACU?!]?[ \t]+/, "", line)
                n = split(line, parts, " -> ")
                if (n == 2) print parts[1], parts[2]
            }
        ' >>"$workdir/renames.txt"

    growth_errors=0
    growth_warnings=0

    if [ "$baseline_existed" -eq 1 ]; then
        # (a) a path scanned both before and after whose count rose -- the plain "grew" case. The
        # ratchet only tightens; --allow-growth is the one deliberate escape hatch.
        while IFS=' ' read -r new_count path; do
            [ -n "$path" ] || continue
            old_count="$(awk -v p="$path" '$2 == p { print $1 }' "$workdir/old.txt")"
            [ -n "$old_count" ] || continue
            if [ "$new_count" -gt "$old_count" ]; then
                if [ "$ALLOW_GROWTH" -eq 1 ]; then
                    echo "::warning::$path raised from $old_count to $new_count lines -- allowed via --allow-growth (reviewed exception)"
                    growth_warnings=$((growth_warnings + 1))
                else
                    echo "::error::$path would raise the baseline from $old_count to $new_count lines -- the ratchet only tightens; split the file / move the addition into a new unit, or pass --allow-growth only for a deliberate, reviewed exception"
                    growth_errors=$((growth_errors + 1))
                fi
            fi
        done <"$workdir/new.txt"

        # (b) a path scanned now that the OLD baseline didn't cover at all -- a genuinely new
        # over-cap file, UNLESS it's a same-size `git mv` of a path the old baseline did cover
        # (confirmed via the rename pairs above; otherwise treated like any other new file, since
        # a same-size coincidence proves nothing on its own).
        while IFS=' ' read -r new_count path; do
            [ -n "$path" ] || continue
            if awk -v p="$path" '$2 == p { f = 1 } END { exit !f }' "$workdir/old.txt"; then
                continue   # already handled by (a) above
            fi

            candidate=""
            is_rename=0
            while IFS=' ' read -r old_count old_path; do
                [ -n "$old_path" ] || continue
                [ "$old_count" = "$new_count" ] || continue
                # still scanned under its old path at the same size? then it didn't move.
                awk -v p="$old_path" '$2 == p' "$workdir/new.txt" | grep -q . && continue
                candidate="$old_path"
                if grep -qF -- "$old_path $path" "$workdir/renames.txt"; then
                    is_rename=1
                    break
                fi
            done <"$workdir/old.txt"

            if [ "$is_rename" -eq 1 ]; then
                continue   # confirmed git mv, same size -- ratchet unaffected, accept silently
            fi

            if [ "$ALLOW_GROWTH" -eq 1 ]; then
                if [ -n "$candidate" ]; then
                    echo "::warning::$path is a new over-cap file ($new_count lines, cap $CAP) -- same size as removed path $candidate but git couldn't confirm a rename; allowed via --allow-growth (reviewed exception)"
                else
                    echo "::warning::$path is a new over-cap file ($new_count lines, cap $CAP) -- allowed via --allow-growth (reviewed exception)"
                fi
                growth_warnings=$((growth_warnings + 1))
            else
                if [ -n "$candidate" ]; then
                    echo "::error::$path is a new over-cap file ($new_count lines, cap $CAP) -- same size as removed path $candidate, but git couldn't confirm a rename; if this is a git mv, stage it as one so \`git status\`/\`git diff --cached -M --name-status\` show it as a rename, or pass --allow-growth only for a deliberate, reviewed exception"
                else
                    echo "::error::$path is a new over-cap file ($new_count lines, cap $CAP) -- the baseline never grows by adding files; split it by concern, or pass --allow-growth only for a deliberate, reviewed exception"
                fi
                growth_errors=$((growth_errors + 1))
            fi
        done <"$workdir/new.txt"
    fi

    if [ "$growth_errors" -gt 0 ]; then
        echo "check-file-sizes --update: refusing to write the baseline -- $growth_errors entr$( [ "$growth_errors" -eq 1 ] && echo y || echo ies ) would raise it or add to it (see errors above). The ratchet only tightens; pass --allow-growth only for a deliberate, reviewed exception." >&2
        return 1
    fi

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
        if [ "$growth_warnings" -gt 0 ]; then
            echo "check-file-sizes --update: baseline rewritten ($entry_count entries, $growth_warnings raised via --allow-growth). Changes:"
        else
            echo "check-file-sizes --update: baseline rewritten ($entry_count entries). Changes:"
        fi
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
