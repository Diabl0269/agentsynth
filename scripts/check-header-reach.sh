#!/usr/bin/env bash
#
# check-header-reach.sh -- a cap, with a strict ratchet baseline, on how many .cpp files reach a
# header. Reach is the number of .cpp files under Source/ and Tests/ that include the header directly
# or through other headers. One edit to a header that reaches a thousand files recompiles a thousand
# files, and CI waits for all of them: a widely included header is what makes a small change cost
# half an hour. It has been cut back before and grown again because nothing stopped it.
#
# A ratchet stops growth; it does not shrink anything. Cutting a header's reach is separate work
# (forward-declare, move a type into a narrower header, include the header only where it is used).
# What this guard does is make sure the next include added to a widely included header is a decision
# someone sees in review, not an accident.
#
# The count is a static scan of #include lines (scripts/lib/header-reach.py), so it needs no build
# and runs in about a second. It is lower than the number of object files a build recompiles, because
# one .cpp can build into several targets; it moves the same way.
#
# Headers over the cap (HEADER_REACH_CAP, default 100) are grandfathered in
# scripts/header-reach-baseline.txt at their current count, together with the total number of .cpp
# files at that time (the "# total-cpp:" line). A tree that gains .cpp files makes every header's
# reach grow in proportion, so each entry is scaled by total_now / total_baseline first; only growth
# beyond that is a violation. On top of the scaled entry a header may grow by the slack,
# max(5 files, 3%), so an ordinary new .cpp does not trip it. A header not in the baseline fails the
# moment it is over the cap: add the include somewhere narrower instead. --update only lowers
# entries and drops those that fell under the cap; raising one or adding one needs --allow-raise,
# the deliberate, reviewed exception.
#
# Usage:
#   bash scripts/check-header-reach.sh                  # check the tree against cap + baseline
#   bash scripts/check-header-reach.sh --update         # tighten the baseline from the current tree
#   bash scripts/check-header-reach.sh --update --allow-raise  # ...and let a raised entry through
#   bash scripts/check-header-reach.sh --list [N]       # N widest headers, default 25
#   bash scripts/check-header-reach.sh --root <dir>     # scan a different repo root (for tests)
#   bash scripts/check-header-reach.sh -h|--help
#
# Environment:
#   HEADER_REACH_CAP       reach cap (default 100)
#   HEADER_REACH_BASELINE  baseline file path (default: <root>/scripts/header-reach-baseline.txt)
#
# Exit status (check mode only): 0 no violation, 1 at least one header over its limit or a stale
# baseline entry.
#
# Needs python3 (scripts/lib/header-reach.py); otherwise bash 3.2 + awk/sort.

set -uo pipefail

unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-header-reach.sh [--update [--allow-raise]] [--list [N]] [--root <dir>] [-h|--help]

Caps how many .cpp files (under Source/ and Tests/) include each header under Source/, directly or
through other headers, with a strict ratchet baseline (scripts/header-reach-baseline.txt) for headers
already over the cap. See docs/development/local-ci.md ("Header reach guard").

  (no flags)     Check the tree against the cap + baseline. Exit 1 on any violation.
  --update       Rewrite the baseline from the current tree (headers over the cap only). Refuses to
                 raise an entry or add a header (exit 1, baseline untouched); pass --allow-raise.
  --allow-raise  Only with --update: let a raised or new entry through, with a ::warning:: each.
  --list [N]     Print the N (default 25) widest headers, widest first.
  --root <dir>   Repo root to scan (default: the root of the repo this script is in).
  -h, --help     Show this message and exit.

Environment:
  HEADER_REACH_CAP       Reach cap (default 100).
  HEADER_REACH_BASELINE  Baseline file path (default: <root>/scripts/header-reach-baseline.txt).
USAGE
}

MODE="check"
LIST_N=25
ROOT_OVERRIDE=""
ALLOW_RAISE=0

while [ $# -gt 0 ]; do
    case "$1" in
        --update)
            MODE="update"
            shift
            ;;
        --allow-raise)
            ALLOW_RAISE=1
            shift
            ;;
        --list)
            MODE="list"
            shift
            if [ $# -gt 0 ]; then
                case "$1" in
                    '' | *[!0-9]*) ;;
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
                echo "check-header-reach: --root requires an argument" >&2
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
            echo "check-header-reach: unknown argument '$1' (see --help)" >&2
            usage >&2
            exit 1
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -n "$ROOT_OVERRIDE" ]; then
    ROOT="$(cd "$ROOT_OVERRIDE" && pwd)"
else
    if ! ROOT="$(cd "$SCRIPT_DIR" && git rev-parse --show-toplevel)"; then
        echo "::error::check-header-reach: 'git rev-parse --show-toplevel' failed from $SCRIPT_DIR -- refusing to guess ROOT (pass --root explicitly to override)" >&2
        exit 1
    fi
fi

CAP="${HEADER_REACH_CAP:-100}"
BASELINE_FILE="${HEADER_REACH_BASELINE:-$ROOT/scripts/header-reach-baseline.txt}"
HELPER="$SCRIPT_DIR/lib/header-reach.py"

# baseline_total -- the .cpp total recorded in the baseline; 0 when absent (then nothing is scaled).
baseline_total() {
    [ -f "$BASELINE_FILE" ] || { echo 0; return; }
    t="$(sed -n 's/^# total-cpp: *\([0-9][0-9]*\).*$/\1/p' "$BASELINE_FILE" | head -n 1)"
    echo "${t:-0}"
}

# build_current_data -- "<count> <header>" for every header that any .cpp reaches, widest first.
build_current_data() {
    python3 "$HELPER" "$ROOT"
}

baseline_data() {
    [ -f "$BASELINE_FILE" ] || return 0
    grep -v '^#' "$BASELINE_FILE" | grep -v '^[[:space:]]*$'
}

write_step_summary() {
    [ -n "${GITHUB_STEP_SUMMARY:-}" ] && printf '%s\n' "$1" >>"$GITHUB_STEP_SUMMARY"
}

# explain_header <header> -- the direct includers that carry the most .cpp files, which is where to
# cut. Each line is "<files that reach through it> <includer>".
explain_header() {
    echo "    direct includers of $1, widest first (cut one of these to cut the reach):" >&2
    python3 "$HELPER" "$ROOT" --includers "$1" | head -n 6 | sed 's/^/      /' >&2
}

run_check() {
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT

    baseline_data >"$workdir/baseline.txt"
    if ! build_current_data >"$workdir/current.txt"; then
        echo "::error::check-header-reach: the scan failed (python3 missing?)" >&2
        return 1
    fi

    total_now="$(python3 "$HELPER" "$ROOT" --total)"
    total_base="$(baseline_total)"
    [ "$total_base" -gt 0 ] || total_base="$total_now"
    # A tree that lost .cpp files keeps its recorded limits; they only scale up.
    [ "$total_now" -ge "$total_base" ] || total_base="$total_now"

    awk_out="$(awk -v cap="$CAP" -v tn="$total_now" -v tb="$total_base" -v baseline_file="$workdir/baseline.txt" '
        FILENAME == baseline_file { entry[$2] = $1 + 0; next }
        {
            count[$2] = $1 + 0
            total++
            if ($1 + 0 > max_count) { max_count = $1 + 0; max_path = $2 }
            if ($1 + 0 > cap) over++
        }
        END {
            errors = 0
            for (p in count) {
                if (p in entry) {
                    scaled = entry[p] * tn / tb
                    scaled = (scaled == int(scaled)) ? scaled : int(scaled) + 1
                    slack = int(scaled * 0.03)
                    if (slack < 5) slack = 5
                    limit = scaled + slack
                    if (count[p] > limit) {
                        printf "ERR %s reaches %d .cpp files, up from %d in the baseline (scaled for %d .cpp files now vs %d then: %d, allowed up to %d) -- add the include where it is used, or forward-declare, instead of in a widely included header\n", p, count[p], entry[p], tn, tb, scaled, limit
                        printf "EXPLAIN %s\n", p
                        errors++
                    } else if (count[p] < scaled - slack) {
                        printf "WARN %s fell from %d (scaled %d) to %d -- run --update to tighten the baseline\n", p, entry[p], scaled, count[p]
                    }
                } else if (count[p] > cap) {
                    printf "ERR %s reaches %d .cpp files (cap %d) and is not in the baseline -- the baseline never grows by adding headers; include it only where used, or split it so most files need only a small part\n", p, count[p], cap
                    printf "EXPLAIN %s\n", p
                    errors++
                }
            }
            for (p in entry) {
                if (!(p in count) && entry[p] > 0) {
                    printf "ERR %s is a stale baseline entry (header missing or no longer reached) -- run --update\n", p
                    errors++
                }
            }
            if (max_path == "") { max_path = "(none)"; max_count = 0 }
            printf "SUMMARY %d %d %d %s\n", total + 0, over + 0, max_count, max_path
            exit (errors > 0 ? 1 : 0)
        }
    ' "$workdir/baseline.txt" "$workdir/current.txt")"
    status=$?

    summary_line=""
    explained=0
    while IFS= read -r line; do
        case "$line" in
            "SUMMARY "*) summary_line="$line" ;;
            "ERR "*) echo "::error::${line#ERR }" ;;
            "WARN "*) echo "::warning::${line#WARN }" ;;
            "EXPLAIN "*)
                # One wide include fans out to dozens of headers; the first few tell the story.
                explained=$((explained + 1))
                [ "$explained" -le 5 ] && explain_header "${line#EXPLAIN }"
                ;;
            *) [ -n "$line" ] && echo "$line" ;;
        esac
    done <<<"$awk_out"

    scanned_total=0
    over_count=0
    max_count=0
    max_path="(none)"
    if [ -n "$summary_line" ]; then
        read -r _ scanned_total over_count max_count max_path <<<"$summary_line"
    fi

    echo "check-header-reach: $scanned_total headers reached, $over_count over the ${CAP}-file cap, widest: $max_path ($max_count files)"

    write_step_summary "### Header reach guard"
    write_step_summary "- headers reached by a .cpp: $scanned_total"
    write_step_summary "- over the ${CAP}-file cap (legacy, baselined): $over_count"
    write_step_summary "- widest header: $max_path ($max_count files)"

    return "$status"
}

run_update() {
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT

    baseline_data | sort -k2,2 >"$workdir/old.txt"
    if ! build_current_data >"$workdir/all.txt"; then
        echo "::error::check-header-reach: the scan failed (python3 missing?)" >&2
        return 1
    fi
    awk -v cap="$CAP" '($1 + 0) > cap' "$workdir/all.txt" | sort -k2,2 >"$workdir/new.txt"
    total_now="$(python3 "$HELPER" "$ROOT" --total)"
    total_base="$(baseline_total)"
    [ "$total_base" -gt 0 ] || total_base="$total_now"
    # A tree that lost .cpp files keeps its recorded limits; they only scale up.
    [ "$total_now" -ge "$total_base" ] || total_base="$total_now"

    baseline_existed=0
    [ -f "$BASELINE_FILE" ] && baseline_existed=1
    raise_errors=0
    raise_warnings=0

    if [ "$baseline_existed" -eq 1 ]; then
        while IFS=' ' read -r new_count path; do
            [ -n "$path" ] || continue
            old_count="$(awk -v p="$path" '$2 == p { print $1 }' "$workdir/old.txt")"
            if [ -z "$old_count" ]; then
                what="is a new over-cap header ($new_count files, cap $CAP)"
            elif [ "$new_count" -gt $(((old_count * total_now + total_base - 1) / total_base)) ]; then
                what="would raise the baseline from $old_count to $new_count (more than its $(((old_count * total_now + total_base - 1) / total_base)) scaled for the tree's growth)"
            else
                continue
            fi
            if [ "$ALLOW_RAISE" -eq 1 ]; then
                echo "::warning::$path $what -- allowed via --allow-raise (reviewed exception)"
                raise_warnings=$((raise_warnings + 1))
            else
                echo "::error::$path $what -- the ratchet only tightens; cut the reach, or pass --allow-raise only for a deliberate, reviewed exception"
                raise_errors=$((raise_errors + 1))
            fi
        done <"$workdir/new.txt"
    fi

    if [ "$raise_errors" -gt 0 ]; then
        echo "check-header-reach --update: refusing to write the baseline -- $raise_errors entr$( [ "$raise_errors" -eq 1 ] && echo y || echo ies ) would raise it or add to it (see errors above)." >&2
        return 1
    fi

    {
        echo "# total-cpp: $total_now"
        echo "# Headers reached by more .cpp files than the cap. STRICT ratchet: an entry is the current count."
        echo "# Each entry is scaled by total-cpp now / above; a header may then grow by max(5 files, 3%); when it shrinks, run \`bash scripts/check-header-reach.sh --update\`"
        echo "# so the entry tightens; once under the cap the entry is removed (--update does that)."
        echo "# Never add a NEW header here -- include it only where it is used instead."
        sort -k1,1nr -k2,2 "$workdir/new.txt"
    } >"$BASELINE_FILE"

    entry_count="$(wc -l <"$workdir/new.txt" | tr -d '[:space:]')"
    if [ "$(sort -k2,2 "$workdir/old.txt")" = "$(cat "$workdir/new.txt")" ]; then
        echo "check-header-reach --update: baseline unchanged ($entry_count entries)."
    else
        echo "check-header-reach --update: baseline rewritten ($entry_count entries, $raise_warnings raised via --allow-raise). Changes:"
        diff "$workdir/old.txt" "$workdir/new.txt" || true
    fi
}

run_list() {
    build_current_data | head -n "$LIST_N"
}

case "$MODE" in
    check) run_check ;;
    update) run_update ;;
    list) run_list ;;
esac
