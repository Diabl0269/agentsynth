#!/usr/bin/env bash
#
# check-header-comments.sh -- a comment-PLACEMENT guard with a strict ratchet baseline, sibling to
# check-file-sizes.sh/check-function-sizes.sh but measuring WHERE a comment lives instead of how
# many lines a file or function has. Root CLAUDE.md's "Code structure" rule: a shared class header
# carries declarations plus short, caller-facing constraints (nullability, thread affinity,
# call-ordering, units, ownership); the detailed maintainer-facing rationale for a member -- why it
# is implemented this way, the edge case that forced the design, the invariants the body must keep
# -- belongs beside its out-of-line definition in the owning .cpp unit, not in the header. A header
# comment recompiles every including translation unit on every edit, and only a comment sitting
# inside the diff hunk of the change that invalidates it reliably stays true. Several headers in
# this repo violate that badly -- more comment lines than code lines -- because moving detail out of
# a header as it grew was never enforced, only asked for.
#
# The cap can't apply retroactively without a from-hero migration, so a baseline
# (scripts/header-comment-baseline.txt) grandfathers today's over-threshold headers at their EXACT
# current EXCESS (see below) and never lets it grow past that -- a STRICT ratchet, exactly like the
# file-size and function-size guards: it only ever tightens, and --update refuses to raise an entry
# or add a new one without --allow-growth.
#
# TRACKED QUANTITY: EXCESS = (comment lines) - (code lines), a single integer that may be negative.
# Excess, not the raw comment count, is what's ratcheted -- because the rule this guard enforces is
# satisfied by adding a member with a short one-line caller-facing contract IN the header (+1
# comment, +1 code -- excess unchanged) just as much as by leaving a compliant header alone. A raw
# comment-line ratchet would reject that exact, mandated edit outright; tracking excess instead
# means only piling MORE comment than code moves the needle, which is the actual failure mode this
# guard exists to catch.
#
# A header is FLAGGED when ALL THREE hold:
#   1. excess (comment lines minus code lines) is positive, AND
#   2. comment lines >= HEADER_COMMENT_FLOOR (default 60) -- a small type whose invariant genuinely
#      runs longer than its declaration is legitimate and must never be flagged, AND
#   3. it has at least one sibling .cpp translation unit -- for header <dir>/<base>.h(pp), at least
#      one tracked <dir>/<base>*.cpp file exists (a PREFIX match scoped to the header's own
#      directory -- e.g. Source/AudioEngine/AudioEngine.h is NOT exempt just because there is no
#      exact Source/AudioEngine/AudioEngine.cpp: its eight AudioEngine*.cpp units still match, and
#      it must be flagged. This is deliberately narrower than "any .cpp in the directory": that
#      broader test would wrongly un-exempt a genuinely header-only class -- e.g.
#      Source/Timeline/EpochExchange.h -- just because unrelated classes' .cpp files happen to share
#      its directory). A header-only component (no owning .cpp at all) is EXEMPT: its rationale has
#      no out-of-line home, and this repo deliberately keeps that content where it is.
#
# Counting comment vs. code lines is a deliberate HEURISTIC, not a C++ parser -- no attempt is made
# to recognise a `//` or `/*` that appears inside a string or char literal. Per line, after
# stripping leading whitespace: blank counts as neither; a line starting `//` is a comment line; a
# line starting `/*` opens (or, if it also contains `*/`, closes on the spot) a block comment, and
# every line until the closing `*/` (inclusive) counts as a comment line; everything else is a code
# line -- including a code line carrying a trailing `// why` comment, which counts as CODE on
# purpose: a trailing note already sits beside the thing it explains, so it isn't a placement
# problem this guard cares about.
#
# SCOPE: git-tracked Source/**/*.h and Source/**/*.hpp only. Tests/ is deliberately OUT OF SCOPE --
# that tree already runs a healthy comment-to-code ratio, and its fixtures/mocks are not the
# maintainer-facing contracts this guard exists to relocate.
#
# NOTE ON RENAMES: unlike check-file-sizes.sh, this script does NOT special-case a git-confirmed
# rename in --update. A header split or renamed almost never keeps an identical excess (the whole
# point of a split is to move comments OUT), so the rename machinery there would add real
# complexity for a case that essentially never fires here; a genuine same-name/same-excess rename
# just needs --allow-growth once, the same as any other new entry.
#
# PORTABILITY: runs in the Lint job on ubuntu-latest (GNU bash 5 / gawk) as well as macOS bash 3.2 /
# BSD awk during local development -- no bash-4-isms (declare -A, mapfile, ${var,,}, &>>), no GNU-
# only sed/grep/sort flags. Every construct here also appears in check-file-sizes.sh, which is
# green on Ubuntu today.
#
# Usage:
#   bash scripts/check-header-comments.sh                  # check the tree against threshold + baseline
#   bash scripts/check-header-comments.sh --update         # rewrite the baseline from the current tree
#   bash scripts/check-header-comments.sh --update --allow-growth  # ...and let a raised entry through
#   bash scripts/check-header-comments.sh --list [N]       # N most comment-heavy scanned headers, default 25
#   bash scripts/check-header-comments.sh --root <dir>     # scan a different repo root (for tests)
#   bash scripts/check-header-comments.sh -h|--help
#
# Environment:
#   HEADER_COMMENT_FLOOR     minimum comment-line count to ever flag a header (default 60)
#   HEADER_COMMENT_BASELINE  baseline file path (default: <root>/scripts/header-comment-baseline.txt)
#                            -- lets scripts/tests/check-header-comments.test.sh point this at a
#                            throwaway file
#
# Exit status (check mode only -- --update/--list/--help always exit 0 on success):
#   0  no violation
#   1  at least one over-threshold-without-entry / grew-past-entry / shrank-without-update /
#      stale-baseline-entry violation
#
# Portable bash 3.2 (macOS default) + awk/sort/wc -- no python, no GNU-only flags.

set -uo pipefail

# Same reasoning as check-file-sizes.sh: a git hook (or a nested harness run) inherits GIT_DIR (and
# sometimes GIT_WORK_TREE/GIT_INDEX_FILE), which can point `git rev-parse --show-toplevel` at the
# wrong directory -- every scanned file then reads as missing, and the check would pass vacuously
# with nothing actually scanned. Strip it so every git call below does normal discovery.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-header-comments.sh [--update] [--list [N]] [--root <dir>] [-h|--help]

Enforces comment PLACEMENT (not volume) on every git-tracked Source/**/*.h(pp) header: a header
whose EXCESS (comment lines minus code lines) is positive, past a floor, with a sibling .cpp to
relocate the detail into, must have its maintainer-facing rationale moved beside the out-of-line
definition it describes. Tracking excess rather than a raw comment count means the mandated fix --
adding a member with its own short one-line caller-facing contract in the header -- never trips the
ratchet (comment and code both grow by one; excess is unchanged). Strict ratchet baseline
(scripts/header-comment-baseline.txt) grandfathers legacy headers. See docs/testing.md "Header
comment placement" for the full mechanism.

  (no flags)     Check the tree against the threshold + baseline. Exit 1 on any violation.
  --update       Rewrite the baseline from the current tree (flagged headers only, sorted) and
                 print what changed. Refuses to raise an existing entry or add a new flagged
                 header to the baseline (exit 1, baseline left untouched) -- pass --allow-growth
                 to let it through as a deliberate, reviewed exception.
  --allow-growth Only meaningful with --update: let a raised entry or new flagged header through,
                 printing a ::warning:: per one so it stays visible in CI logs and PR review.
  --list [N]     Print the N (default 25) most comment-heavy scanned headers, highest excess
                 first, regardless of threshold or baseline -- for planning what to relocate next.
  --root <dir>   Repo root to scan (default: `git rev-parse --show-toplevel` from this script's
                 own directory). Lets tests point this at a throwaway fixture repo.
  -h, --help     Show this message and exit.

Environment:
  HEADER_COMMENT_FLOOR     Minimum comment-line count to ever flag a header (default 60).
  HEADER_COMMENT_BASELINE  Baseline file path (default: <root>/scripts/header-comment-baseline.txt).
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
                echo "check-header-comments: --root requires an argument" >&2
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
            echo "check-header-comments: unknown argument '$1' (see --help)" >&2
            usage >&2
            exit 1
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -n "$ROOT_OVERRIDE" ]; then
    ROOT="$(cd "$ROOT_OVERRIDE" && pwd)"
else
    # Fail loudly rather than silently: if this can't resolve, a wrong/empty ROOT must never reach
    # the scan below (see the awk fail-safe further down for the belt-and-braces backstop).
    if ! ROOT="$(cd "$SCRIPT_DIR" && git rev-parse --show-toplevel)"; then
        echo "::error::check-header-comments: 'git rev-parse --show-toplevel' failed from $SCRIPT_DIR -- refusing to guess ROOT (pass --root explicitly to override)" >&2
        exit 1
    fi
fi

FLOOR="${HEADER_COMMENT_FLOOR:-60}"
BASELINE_FILE="${HEADER_COMMENT_BASELINE:-$ROOT/scripts/header-comment-baseline.txt}"

# The guard's own baseline never counts toward itself -- computed relative to ROOT so a --root
# override pointing the baseline somewhere unusual still excludes it correctly. If BASELINE_FILE
# isn't under ROOT this expansion is a no-op (no path will ever equal it).
BASELINE_REL="${BASELINE_FILE#"$ROOT"/}"

# list_scanned_paths -- every git-tracked Source/**/*.h or Source/**/*.hpp path under $ROOT, one
# per line, minus the guard's own baseline file (belt-and-braces; a .txt baseline never matches
# these extensions anyway, but this keeps the exclusion explicit per the guard's own contract).
list_scanned_paths() {
    git -C "$ROOT" ls-files | while IFS= read -r path; do
        case "$path" in
            Source/*.h | Source/*.hpp) ;;
            *) continue ;;
        esac
        [ "$path" = "$BASELINE_REL" ] && continue
        printf '%s\n' "$path"
    done
}

# list_cpp_files -- every git-tracked Source/**/*.cpp path under $ROOT, one per line. Used by
# has_sibling_cpp to decide whether a flagged header has somewhere to relocate its comments to.
list_cpp_files() {
    git -C "$ROOT" ls-files | while IFS= read -r path; do
        case "$path" in
            Source/*.cpp) printf '%s\n' "$path" ;;
        esac
    done
}

# has_sibling_cpp <header-path> <cpp-list-file> -- true (exit 0) iff <cpp-list-file> (one tracked
# Source/**/*.cpp path per line) contains a path in the SAME directory as <header-path> whose
# filename starts with the header's basename (extension stripped) and ends ".cpp" -- e.g.
# Source/UI/Foo/Foo.h matches Source/UI/Foo/FooCanvas.cpp. This is a PREFIX match scoped to the
# header's own directory: it is deliberately NOT a stem-exact test (only <dir>/<base>.cpp), which
# would wrongly exempt a class split into per-concern units with no <Class>.cpp of its own (e.g.
# Source/AudioEngine/AudioEngine.h has no AudioEngine.cpp but eight AudioEngine*.cpp units, and
# must still be flagged); and it is deliberately NOT "any .cpp in the directory", which would
# wrongly un-exempt a genuinely header-only class sharing a directory with unrelated classes' .cpp
# files (e.g. Source/Timeline/EpochExchange.h). dir/base are compared with substr, never
# interpolated into an awk regex, so no name in this codebase can break the match via regex
# metacharacters.
has_sibling_cpp() {
    local header="$1" cpp_list="$2" dir filename base
    dir="${header%/*}"
    filename="${header##*/}"
    base="${filename%.*}"
    awk -v dir="$dir" -v base="$base" '
        {
            path = $0
            if (substr(path, 1, length(dir)) == dir && substr(path, length(dir) + 1, 1) == "/") {
                fn = substr(path, length(dir) + 2)
                if (fn !~ /\// && substr(fn, 1, length(base)) == base && fn ~ /\.cpp$/) {
                    found = 1
                    exit
                }
            }
        }
        END { exit !found }
    ' "$cpp_list"
}

# count_comment_code <file> -- prints "<comment-lines> <code-lines>" for <file>, per the counting
# heuristic in this script's header comment (leading whitespace stripped; blank counts as neither;
# `//`-led lines and `/* ... */` block spans, including their closing line, count as comment;
# everything else -- including a code line with a trailing `// note` -- counts as code).
count_comment_code() {
    awk '
        BEGIN { c = 0; code = 0; inblk = 0 }
        {
            line = $0
            gsub(/^[ \t]+/, "", line)
            if (inblk) { c++; if (line ~ /\*\//) inblk = 0; next }
            if (line == "") next
            if (line ~ /^\/\//) { c++; next }
            if (line ~ /^\/\*/) { c++; if (line !~ /\*\//) inblk = 1; next }
            code++
        }
        END { printf "%d %d\n", c, code }
    ' "$1"
}

# baseline_data -- the baseline file's data lines ("<excess> <path>"), comments and blanks
# stripped. Empty (not an error) when the baseline file doesn't exist yet.
baseline_data() {
    [ -f "$BASELINE_FILE" ] || return 0
    grep -v '^#' "$BASELINE_FILE" | grep -v '^[[:space:]]*$'
}

write_step_summary() {
    [ -n "${GITHUB_STEP_SUMMARY:-}" ] && printf '%s\n' "$1" >>"$GITHUB_STEP_SUMMARY"
}

# build_current_data -- "<comment> <code> <excess> <flagged> <path>" for every scanned header,
# unsorted. excess = comment - code (may be negative). <flagged> is 1/0, precomputed here so
# downstream awk never needs FLOOR/sibling-.cpp logic.
build_current_data() {
    local cpp_list
    cpp_list="$(mktemp)"
    list_cpp_files >"$cpp_list"

    list_scanned_paths | while IFS= read -r path; do
        full="$ROOT/$path"
        if [ -f "$full" ]; then
            counts="$(count_comment_code "$full")"
        else
            counts="0 0"
        fi
        comment="${counts%% *}"
        code="${counts##* }"
        excess=$((comment - code))

        flagged=0
        if [ "$excess" -gt 0 ] && [ "$comment" -ge "$FLOOR" ] && has_sibling_cpp "$path" "$cpp_list"; then
            flagged=1
        fi

        printf '%s %s %s %s %s\n' "$comment" "$code" "$excess" "$flagged" "$path"
    done

    rm -f "$cpp_list"
}

run_check() {
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT

    baseline_data >"$workdir/baseline.txt"
    build_current_data | sort -k5,5 >"$workdir/current.txt"

    # NOTE: deliberately NOT the classic `FNR==NR` two-file idiom -- see check-file-sizes.sh's own
    # comment on this: it breaks when the FIRST file (the baseline) is legitimately empty, which it
    # is before the first --update. Compare FILENAME instead.
    awk_out="$(awk -v baseline_file="$workdir/baseline.txt" '
        FILENAME == baseline_file {
            entry_excess[$2] = $1 + 0
            next
        }
        {
            comment_count[$5] = $1 + 0
            code_count[$5] = $2 + 0
            excess_of[$5] = $3 + 0
            flagged_ok[$5] = $4 + 0
            scanned[$5] = 1
            total++
            if (($1 + 0) == 0 && ($2 + 0) == 0) {
                zero_count++
            }
            if (excess_of[$5] > max_excess || max_path == "") {
                max_excess = excess_of[$5]
                max_comment = $1 + 0
                max_code = $2 + 0
                max_path = $5
            }
        }
        END {
            errors = 0
            over = 0
            for (p in scanned) {
                if (!flagged_ok[p]) continue
                over++
                if (!(p in entry_excess)) {
                    printf "::error::%s has %d comment lines vs %d code lines (+%d excess, over the placement threshold) and is not in the baseline -- move each member'\''s detailed contract beside its out-of-line definition in the owning .cpp unit -- see docs/testing.md\n", p, comment_count[p], code_count[p], excess_of[p]
                    errors++
                } else if (excess_of[p] > entry_excess[p]) {
                    printf "::error::%s grew from +%d to +%d excess comment lines (comments minus code); the ratchet only tightens -- move the addition beside its out-of-line definition instead\n", p, entry_excess[p], excess_of[p]
                    errors++
                } else if (excess_of[p] < entry_excess[p]) {
                    printf "::error::%s shrank from +%d to +%d excess comment lines -- run --update to tighten the baseline\n", p, entry_excess[p], excess_of[p]
                    errors++
                }
            }
            for (p in entry_excess) {
                if (!(p in scanned)) {
                    printf "::error::%s is a stale baseline entry (file missing or no longer scanned) -- run --update\n", p
                    errors++
                } else if (!flagged_ok[p]) {
                    printf "::error::%s is a stale baseline entry (no longer over the placement threshold) -- run --update\n", p
                    errors++
                }
            }
            if (max_path == "") {
                max_path = "(none)"
                max_excess = 0
                max_comment = 0
                max_code = 0
            }
            # Fail safe, same shape as check-file-sizes.sh: reading EVERY scanned header as 0/0
            # looks identical to a clean pass. A real tree never has every one of several tracked
            # headers read back as empty -- fail loudly instead of reporting a vacuous pass.
            if (total + 0 > 1 && zero_count + 0 == total + 0) {
                printf "::error::check-header-comments read every one of %d scanned headers as 0 comment / 0 code lines -- ROOT is almost certainly wrong (a stray GIT_DIR/GIT_WORK_TREE in the environment is the usual cause); refusing to report a vacuous pass\n", total + 0
                errors++
            }
            printf "SUMMARY %d %d %d %d %d %s\n", total + 0, over + 0, max_excess + 0, max_comment + 0, max_code + 0, max_path
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
    over_count=0
    max_excess=0
    max_comment=0
    max_code=0
    max_path="(none)"
    if [ -n "$summary_line" ]; then
        read -r _ scanned_total over_count max_excess max_comment max_code max_path <<<"$summary_line"
    fi

    if [ "$max_excess" -ge 0 ] 2>/dev/null; then
        max_excess_disp="+$max_excess"
    else
        max_excess_disp="$max_excess"
    fi

    echo "check-header-comments: $scanned_total headers scanned, $over_count over the placement threshold, heaviest: $max_path ($max_excess_disp: $max_comment comments / $max_code code)"

    write_step_summary "### Header-comment-placement guard"
    write_step_summary "- headers scanned: $scanned_total"
    write_step_summary "- over the placement threshold (legacy, baselined): $over_count"
    write_step_summary "- heaviest scanned header: $max_path ($max_excess_disp: $max_comment comments / $max_code code)"

    return "$status"
}

run_update() {
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT

    baseline_data | sort -k2,2 >"$workdir/old.txt"
    build_current_data | awk '$4 == 1 { print $3, $5 }' | sort -k2,2 >"$workdir/new.txt"

    old_data="$(cat "$workdir/old.txt")"
    new_data="$(cat "$workdir/new.txt")"

    # The ratchet only makes sense once a baseline actually exists -- the very first --update ever
    # run against a repo (no baseline file on disk yet) is a one-time bootstrap that grandfathers
    # whatever is over threshold today, not a "new header joining the baseline" violation. See
    # check-file-sizes.sh for the same carve-out; unlike that script, there is no rename-detection
    # machinery here at all (see this script's own header comment for why).
    baseline_existed=0
    [ -f "$BASELINE_FILE" ] && baseline_existed=1

    growth_errors=0
    growth_warnings=0

    if [ "$baseline_existed" -eq 1 ]; then
        # (a) a path flagged both before and after whose excess rose.
        while IFS=' ' read -r new_excess path; do
            [ -n "$path" ] || continue
            old_excess="$(awk -v p="$path" '$2 == p { print $1 }' "$workdir/old.txt")"
            [ -n "$old_excess" ] || continue
            if [ "$new_excess" -gt "$old_excess" ]; then
                if [ "$ALLOW_GROWTH" -eq 1 ]; then
                    echo "::warning::$path raised from +$old_excess to +$new_excess excess comment lines -- allowed via --allow-growth (reviewed exception)"
                    growth_warnings=$((growth_warnings + 1))
                else
                    echo "::error::$path would raise the baseline from +$old_excess to +$new_excess excess comment lines -- the ratchet only tightens; relocate the addition beside its out-of-line definition, or pass --allow-growth only for a deliberate, reviewed exception"
                    growth_errors=$((growth_errors + 1))
                fi
            fi
        done <"$workdir/new.txt"

        # (b) a path flagged now that the OLD baseline didn't cover at all -- a genuinely new
        # over-threshold header (no rename detection here -- see the header comment above).
        while IFS=' ' read -r new_excess path; do
            [ -n "$path" ] || continue
            if awk -v p="$path" '$2 == p { f = 1 } END { exit !f }' "$workdir/old.txt"; then
                continue   # already handled by (a) above
            fi
            if [ "$ALLOW_GROWTH" -eq 1 ]; then
                echo "::warning::$path is a new header over the placement threshold (+$new_excess excess comment lines) -- allowed via --allow-growth (reviewed exception)"
                growth_warnings=$((growth_warnings + 1))
            else
                echo "::error::$path is a new header over the placement threshold (+$new_excess excess comment lines) -- the baseline never grows by adding headers; relocate the comments beside their out-of-line definitions, or pass --allow-growth only for a deliberate, reviewed exception"
                growth_errors=$((growth_errors + 1))
            fi
        done <"$workdir/new.txt"
    fi

    if [ "$growth_errors" -gt 0 ]; then
        echo "check-header-comments --update: refusing to write the baseline -- $growth_errors entr$( [ "$growth_errors" -eq 1 ] && echo y || echo ies ) would raise it or add to it (see errors above). The ratchet only tightens; pass --allow-growth only for a deliberate, reviewed exception." >&2
        return 1
    fi

    {
        echo "# Headers over the comment-placement threshold. STRICT ratchet: an entry is the exact"
        echo "# current EXCESS (comment lines minus code lines). A header may never grow its excess past"
        echo "# its entry; when it shrinks, run \`bash scripts/check-header-comments.sh --update\` so the"
        echo "# entry tightens; once it reads as compliant the entry must be removed (--update does that)."
        echo "# Never add a NEW header here -- relocate its comments beside their out-of-line definitions"
        echo "# instead. Adding a member with its own short one-line caller-facing contract in the header"
        echo "# (+1 comment, +1 code) never moves excess, so it never trips this ratchet."
        cat "$workdir/new.txt"
    } >"$BASELINE_FILE"

    entry_count="$(wc -l <"$workdir/new.txt" | tr -d '[:space:]')"

    if [ "$old_data" = "$new_data" ]; then
        echo "check-header-comments --update: baseline unchanged ($entry_count entries)."
    else
        if [ "$growth_warnings" -gt 0 ]; then
            echo "check-header-comments --update: baseline rewritten ($entry_count entries, $growth_warnings raised via --allow-growth). Changes:"
        else
            echo "check-header-comments --update: baseline rewritten ($entry_count entries). Changes:"
        fi
        diff <(printf '%s\n' "$old_data") <(printf '%s\n' "$new_data") || true
    fi
}

run_list() {
    build_current_data | awk '
        {
            comment = $1 + 0
            code = $2 + 0
            excess = $3 + 0
            path = $5
            printf "%d %d %d %s\n", comment, code, excess, path
        }
    ' | sort -k3,3nr | head -n "$LIST_N"
}

case "$MODE" in
    check) run_check ;;
    update) run_update ;;
    list) run_list ;;
esac
