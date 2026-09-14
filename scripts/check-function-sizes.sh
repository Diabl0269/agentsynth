#!/usr/bin/env bash
#
# check-function-sizes.sh -- a hard per-function line-count cap with a strict ratchet baseline,
# sibling to check-file-sizes.sh but measuring functions instead of whole files. A function that
# needs 200+ lines to read is doing more than one thing -- the fix is almost always a named local
# step or a real collaborator class, not a bigger function. Same story as the file-size guard:
# splitting can't apply retroactively without freezing every legacy function mid-refactor, so a
# baseline (scripts/function-size-baseline.txt) grandfathers today's over-cap functions at their
# EXACT current line count and only ever tightens -- see that script's own header for the reasoning
# (mirrored here: strict ratchet, --update never raises or adds without --allow-growth, GIT_DIR/etc
# unset up front, ROOT fail-loud, an analogous vacuous-pass fail-safe).
#
# WHY AN AWK SCANNER, NOT clang-tidy's readability-function-size: this runs in the Lint job, which
# never configures CMake -- readability-function-size needs a compile_commands.json, i.e. the full
# JUCE FetchContent + configure + parsing every JUCE-heavy translation unit, in the one job meant to
# give fast feedback before a real build. A brace-depth awk scanner (like check-file-sizes.sh: no
# compiler, no network) runs identically here, in ci-local.sh, and in the pre-commit hook.
#
# The actual C++ scanner lives in function-size-scan.awk (own file, not inlined here, because a
# real brace/paren-depth parser needs several helper functions and embedding that much awk inside
# a bash single-quoted string is its own hazard -- the scanner's operator-name handling alone needs
# literal `'` characters that would terminate the quoting). See that file's header for the full
# detection method, and its own "KNOWN LIMITATIONS" section for what it deliberately does not
# handle. This script is everything AROUND the scanner: mode dispatch, the baseline/ratchet, and
# the same environment hardening as check-file-sizes.sh.
#
# Usage:
#   bash scripts/check-function-sizes.sh                  # check the tree against cap + baseline
#   bash scripts/check-function-sizes.sh --update         # rewrite the baseline from the current tree
#   bash scripts/check-function-sizes.sh --update --allow-growth  # ...and let a raised entry through
#   bash scripts/check-function-sizes.sh --list [N]       # N largest scanned functions, default 25
#   bash scripts/check-function-sizes.sh --root <dir>     # scan a different repo root (for tests)
#   bash scripts/check-function-sizes.sh -h|--help
#
# Environment:
#   FUNCTION_SIZE_CAP       line-count cap per function (default 200 -- root CLAUDE.md: "a new
#                            function must never be 200+ lines")
#   FUNCTION_SIZE_BASELINE  baseline file path (default: <root>/scripts/function-size-baseline.txt)
#                            -- lets scripts/tests/check-function-sizes.test.sh point this at a
#                            throwaway file
#
# Exit status (check mode only -- --update/--list/--help always exit 0 on success):
#   0  no violation
#   1  at least one over-cap-without-entry / grew-past-entry / shrank-without-update /
#      stale-baseline-entry violation
#
# Portable bash 3.2 (macOS default) + awk/sort/wc -- no python, no GNU-only flags.

set -uo pipefail

# Same reasoning as check-file-sizes.sh: a git hook (or a nested harness run) inherits GIT_DIR
# (and sometimes GIT_WORK_TREE/GIT_INDEX_FILE), which can point `git rev-parse --show-toplevel`
# at the wrong directory -- every scanned file then reads as missing, and the check would pass
# vacuously with nothing actually scanned. Strip it so every git call below does normal discovery.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-function-sizes.sh [--update] [--list [N]] [--root <dir>] [-h|--help]

Enforces a hard per-function line-count cap (FUNCTION_SIZE_CAP, default 200) over every git-tracked
*.cpp/*.h/*.mm file under Source/, Tests/, Tools/, with a strict ratchet baseline
(scripts/function-size-baseline.txt) grandfathering functions already over the cap. See
docs/testing.md "Function-size cap (Lint job)" for the full mechanism and how to split a function.

  (no flags)     Check the tree against the cap + baseline. Exit 1 on any violation.
  --update       Rewrite the baseline from the current tree (functions over cap only, sorted) and
                 print what changed. Refuses to raise an existing entry or add a new over-cap
                 function to the baseline (exit 1, baseline left untouched) -- pass --allow-growth
                 to let it through as a deliberate, reviewed exception.
  --allow-growth Only meaningful with --update: let a raised entry or new over-cap function
                 through, printing a ::warning:: per one so it stays visible in CI logs/PR review.
  --list [N]     Print the N (default 25) largest scanned functions, largest first, regardless of
                 cap or baseline -- for planning what to extract next.
  --root <dir>   Repo root to scan (default: `git rev-parse --show-toplevel` from this script's
                 own directory). Lets tests point this at a throwaway fixture repo.
  -h, --help     Show this message and exit.

Environment:
  FUNCTION_SIZE_CAP       Line-count cap per function (default 200).
  FUNCTION_SIZE_BASELINE  Baseline file path (default: <root>/scripts/function-size-baseline.txt).
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
                echo "check-function-sizes: --root requires an argument" >&2
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
            echo "check-function-sizes: unknown argument '$1' (see --help)" >&2
            usage >&2
            exit 1
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -n "$ROOT_OVERRIDE" ]; then
    ROOT="$(cd "$ROOT_OVERRIDE" && pwd)"
else
    # Fail loudly rather than silently -- see check-file-sizes.sh's identical comment.
    if ! ROOT="$(cd "$SCRIPT_DIR" && git rev-parse --show-toplevel)"; then
        echo "::error::check-function-sizes: 'git rev-parse --show-toplevel' failed from $SCRIPT_DIR -- refusing to guess ROOT (pass --root explicitly to override)" >&2
        exit 1
    fi
fi

AWK_SCANNER="$SCRIPT_DIR/function-size-scan.awk"
CAP="${FUNCTION_SIZE_CAP:-200}"
BASELINE_FILE="${FUNCTION_SIZE_BASELINE:-$ROOT/scripts/function-size-baseline.txt}"

# list_scanned_paths -- every git-tracked *.cpp/*.h/*.mm path under Source/, Tests/, Tools/. Git's
# pathspec globs match across "/" (unlike a shell glob), so these three patterns per extension
# already recurse into every nested directory.
list_scanned_paths() {
    git -C "$ROOT" ls-files -- \
        'Source/*.cpp' 'Source/*.h' 'Source/*.mm' \
        'Tests/*.cpp' 'Tests/*.h' 'Tests/*.mm' \
        'Tools/*.cpp' 'Tools/*.h' 'Tools/*.mm'
}

# build_current_data -- "<lines>\t<path>::<name>\t<start-line>" for every function found in the
# scanned tree, unsorted. Deliberately has no side-channel state (e.g. a "files scanned" count set
# as a variable): every call site below pipes this straight into another command, and a bash
# variable assigned inside the LEFT side of a pipe (or inside a `$(...)`) runs in a subshell and is
# lost the moment that subshell exits -- count_scanned_files() below exists precisely so run_check
# doesn't need one.
build_current_data() {
    local paths=() existing=() p
    while IFS= read -r p; do
        [ -n "$p" ] && paths+=("$p")
    done < <(list_scanned_paths)
    [ "${#paths[@]}" -eq 0 ] && return 0
    # Only hand awk files that actually exist on disk: git can list a path that isn't checked out
    # (e.g. a `--no-checkout` clone, or the ROOT-resolution bug the fail-safe in run_check exists
    # to catch), and awk treats a missing input file as fatal rather than "0 functions".
    for p in "${paths[@]}"; do
        [ -f "$ROOT/$p" ] && existing+=("$p")
    done
    [ "${#existing[@]}" -eq 0 ] && return 0
    (cd "$ROOT" && awk -f "$AWK_SCANNER" -- "${existing[@]}")
}

# count_scanned_files -- how many paths list_scanned_paths returns, for run_check's vacuous-pass
# fail-safe. A plain command substitution (unlike a bash variable set inside build_current_data's
# pipeline) captures stdout correctly regardless of subshells, so this is intentionally its own
# tiny, separate call rather than a side effect of build_current_data.
count_scanned_files() {
    list_scanned_paths | wc -l | tr -d '[:space:]'
}

# baseline_data -- the baseline file's data lines, comments and blanks stripped, TAB-separated as
# "<lines>\t<path>::<name>". The committed file itself uses a plain space (matching
# file-size-baseline.txt's own convention -- see run_update's final write), but a TEST_F/TEST_P
# name legitimately contains a space of its own ("TEST_F(Suite, Case)"), so every downstream
# comparison below keys off a TAB, the one separator guaranteed not to appear inside a name.
baseline_data() {
    [ -f "$BASELINE_FILE" ] || return 0
    grep -v '^#' "$BASELINE_FILE" | grep -v '^[[:space:]]*$' | sed -E 's/^([0-9]+) /\1\t/'
}

write_step_summary() {
    [ -n "${GITHUB_STEP_SUMMARY:-}" ] && printf '%s\n' "$1" >>"$GITHUB_STEP_SUMMARY"
}

run_check() {
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT

    baseline_data >"$workdir/baseline.txt"
    build_current_data | sort -t $'\t' -k2,2 >"$workdir/current.txt"

    # NOTE: not the classic FNR==NR idiom -- see check-file-sizes.sh's identical note (breaks when
    # the baseline, file 1, is legitimately empty before the first --update). Compare FILENAME.
    # -F'\t': the key (field 2) can itself contain a space (a TEST_F/TEST_P name), so TAB is the
    # only separator that safely isolates it from the surrounding size/line fields -- see
    # baseline_data's comment.
    awk_out="$(awk -F'\t' -v cap="$CAP" -v baseline_file="$workdir/baseline.txt" '
        FILENAME == baseline_file {
            entry_count[$2] = $1 + 0
            next
        }
        {
            count[$2] = $1 + 0
            line[$2] = $3 + 0
            scanned[$2] = 1
            total++
            if ($1 + 0 > max_count) {
                max_count = $1 + 0
                max_key = $2
            }
        }
        function name_of(key,    p) { p = index(key, "::"); return substr(key, p + 2) }
        function path_of(key,    p) { p = index(key, "::"); return substr(key, 1, p - 1) }
        END {
            errors = 0
            legacy = 0
            for (k in scanned) {
                if (count[k] > cap) {
                    legacy++
                    if (!(k in entry_count)) {
                        printf "::error::%s (%s:%d) is %d lines (cap %d) and not in the baseline -- extract a named step / collaborator (see docs/testing.md, section \"Function-size cap\")\n", name_of(k), path_of(k), line[k], count[k], cap
                        errors++
                    } else if (count[k] > entry_count[k]) {
                        printf "::error::%s (%s:%d) grew from %d to %d lines; the ratchet only tightens -- extract a named step / collaborator instead\n", name_of(k), path_of(k), line[k], entry_count[k], count[k]
                        errors++
                    } else if (count[k] < entry_count[k]) {
                        printf "::error::%s (%s:%d) shrank from %d to %d lines -- run --update to tighten the baseline\n", name_of(k), path_of(k), line[k], entry_count[k], count[k]
                        errors++
                    }
                }
            }
            for (k in entry_count) {
                if (!(k in scanned)) {
                    printf "::error::%s is a stale baseline entry (function missing or no longer scanned) -- run --update\n", k
                    errors++
                } else if (count[k] <= cap) {
                    printf "::error::%s is a stale baseline entry (now %d lines, at or under the %d-line cap) -- run --update\n", k, count[k], cap
                    errors++
                }
            }
            if (max_key == "") {
                max_key = "(none)"
                max_count = 0
            }
            printf "SUMMARY %d %d %d %s\n", total + 0, legacy, max_count, max_key
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
    max_key="(none)"
    if [ -n "$summary_line" ]; then
        read -r _ scanned_total legacy_count max_count max_key <<<"$summary_line"
    fi

    # Fail safe, analogous to check-file-sizes.sh's all-zero backstop: a real tree of this size
    # never has every scanned FILE contribute zero functions -- that shape means ROOT resolved
    # wrong (files unreadable at $ROOT) and the scan above quietly ran over nothing. Files with
    # genuinely zero functions (a pure-declarations header, say) are legitimate on their own; the
    # backstop only fires when ALL of them come back empty. Computed via count_scanned_files
    # (its own tiny command substitution) rather than a variable build_current_data would have set
    # -- build_current_data always runs on the left side of a pipe above, and a bash variable
    # assigned inside a pipeline's subshell never reaches this scope.
    scanned_file_count="$(count_scanned_files)"
    if [ "${scanned_file_count:-0}" -gt 1 ] && [ "$scanned_total" -eq 0 ]; then
        echo "::error::check-function-sizes scanned $scanned_file_count files and found zero functions in any of them -- ROOT is almost certainly wrong (a stray GIT_DIR/GIT_WORK_TREE in the environment is the usual cause); refusing to report a vacuous pass" >&2
        status=1
    fi

    echo "check-function-sizes: $scanned_total functions scanned, $legacy_count over the ${CAP}-line cap, largest: $max_key ($max_count lines)"

    write_step_summary "### Function-size guard"
    write_step_summary "- functions scanned: $scanned_total"
    write_step_summary "- over the ${CAP}-line cap (legacy, baselined): $legacy_count"
    write_step_summary "- largest scanned function: $max_key ($max_count lines)"

    return "$status"
}

run_update() {
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT

    # old.txt / new.txt stay TAB-separated ("<size>\t<key>") throughout this function for the same
    # reason as baseline_data/run_check -- a TEST_F/TEST_P key can contain a space of its own.
    baseline_data | sort -t $'\t' -k2,2 >"$workdir/old.txt"
    build_current_data | awk -F'\t' -v cap="$CAP" '($1 + 0) > cap { print $1 "\t" $2 }' | sort -t $'\t' -k2,2 >"$workdir/new.txt"

    # Human-readable (space-separated, matching the persisted baseline file) for the unchanged
    # check and the final diff -- everything ABOVE this still reads old.txt/new.txt's real TABs.
    old_data="$(sed 's/\t/ /' "$workdir/old.txt")"
    new_data="$(sed 's/\t/ /' "$workdir/new.txt")"

    # First-ever --update (no baseline on disk yet) is a one-time bootstrap that grandfathers
    # whatever is over cap today -- see check-file-sizes.sh's identical reasoning.
    baseline_existed=0
    [ -f "$BASELINE_FILE" ] && baseline_existed=1

    # File-level renames git can confirm for THIS tree -- same two sources check-file-sizes.sh
    # uses. A function's baseline KEY is "<path>::<name>", so a same-name, same-size function that
    # moved counts as a rename exactly when its FILE move does.
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
        # (a) a key (path::name) scanned both before and after whose count rose.
        while IFS=$'\t' read -r new_count key; do
            [ -n "$key" ] || continue
            old_count="$(awk -F'\t' -v k="$key" '$2 == k { print $1 }' "$workdir/old.txt")"
            [ -n "$old_count" ] || continue
            if [ "$new_count" -gt "$old_count" ]; then
                if [ "$ALLOW_GROWTH" -eq 1 ]; then
                    echo "::warning::$key raised from $old_count to $new_count lines -- allowed via --allow-growth (reviewed exception)"
                    growth_warnings=$((growth_warnings + 1))
                else
                    echo "::error::$key would raise the baseline from $old_count to $new_count lines -- the ratchet only tightens; extract a named step / collaborator, or pass --allow-growth only for a deliberate, reviewed exception"
                    growth_errors=$((growth_errors + 1))
                fi
            fi
        done <"$workdir/new.txt"

        # (b) a key scanned now the OLD baseline didn't cover -- a genuinely new over-cap function,
        # UNLESS it's the SAME function name at the SAME size whose FILE git confirms was renamed.
        while IFS=$'\t' read -r new_count key; do
            [ -n "$key" ] || continue
            if awk -F'\t' -v k="$key" '$2 == k { f = 1 } END { exit !f }' "$workdir/old.txt"; then
                continue   # already handled by (a) above
            fi

            new_path="${key%%::*}"
            new_name="${key#*::}"
            candidate=""
            is_rename=0
            while IFS=$'\t' read -r old_count old_key; do
                [ -n "$old_key" ] || continue
                [ "$old_count" = "$new_count" ] || continue
                old_path="${old_key%%::*}"
                old_name="${old_key#*::}"
                [ "$old_name" = "$new_name" ] || continue
                # still scanned under its old key at the same size? then it didn't move.
                awk -F'\t' -v k="$old_key" '$2 == k' "$workdir/new.txt" | grep -q . && continue
                candidate="$old_key"
                if grep -qF -- "$old_path $new_path" "$workdir/renames.txt"; then
                    is_rename=1
                    break
                fi
            done <"$workdir/old.txt"

            if [ "$is_rename" -eq 1 ]; then
                continue   # confirmed git mv of the file, same function/size -- accept silently
            fi

            if [ "$ALLOW_GROWTH" -eq 1 ]; then
                if [ -n "$candidate" ]; then
                    echo "::warning::$key is a new over-cap function ($new_count lines, cap $CAP) -- same name/size as removed entry $candidate but git couldn't confirm the file rename; allowed via --allow-growth (reviewed exception)"
                else
                    echo "::warning::$key is a new over-cap function ($new_count lines, cap $CAP) -- allowed via --allow-growth (reviewed exception)"
                fi
                growth_warnings=$((growth_warnings + 1))
            else
                if [ -n "$candidate" ]; then
                    echo "::error::$key is a new over-cap function ($new_count lines, cap $CAP) -- same name/size as removed entry $candidate, but git couldn't confirm the file rename; if this is a git mv, stage it as one, or pass --allow-growth only for a deliberate, reviewed exception"
                else
                    echo "::error::$key is a new over-cap function ($new_count lines, cap $CAP) -- the baseline never grows by adding functions; extract a named step / collaborator, or pass --allow-growth only for a deliberate, reviewed exception"
                fi
                growth_errors=$((growth_errors + 1))
            fi
        done <"$workdir/new.txt"
    fi

    if [ "$growth_errors" -gt 0 ]; then
        echo "check-function-sizes --update: refusing to write the baseline -- $growth_errors entr$( [ "$growth_errors" -eq 1 ] && echo y || echo ies ) would raise it or add to it (see errors above). The ratchet only tightens; pass --allow-growth only for a deliberate, reviewed exception." >&2
        return 1
    fi

    {
        echo "# Legacy functions over the cap. STRICT ratchet: an entry is the exact current line count."
        echo "# A function may never grow past its entry; when it shrinks, run"
        echo "# \`bash scripts/check-function-sizes.sh --update\` so the entry tightens; once under the cap"
        echo "# the entry must be removed (--update does that)."
        echo "# Never add a NEW function here -- extract a named step / collaborator instead."
        # The persisted file uses a plain space (matching file-size-baseline.txt), not the
        # internal TAB -- see baseline_data's comment for why processing uses TAB at all.
        sed 's/\t/ /' "$workdir/new.txt"
    } >"$BASELINE_FILE"

    entry_count="$(wc -l <"$workdir/new.txt" | tr -d '[:space:]')"

    if [ "$old_data" = "$new_data" ]; then
        echo "check-function-sizes --update: baseline unchanged ($entry_count entries)."
    else
        if [ "$growth_warnings" -gt 0 ]; then
            echo "check-function-sizes --update: baseline rewritten ($entry_count entries, $growth_warnings raised via --allow-growth). Changes:"
        else
            echo "check-function-sizes --update: baseline rewritten ($entry_count entries). Changes:"
        fi
        diff <(printf '%s\n' "$old_data") <(printf '%s\n' "$new_data") || true
    fi
}

run_list() {
    # Write sorted output to a real file before `head` trims it: piping straight into `head -n`
    # lets `head` close the pipe once it has enough lines, which SIGPIPEs `sort` mid-write on a
    # list this size (10,000+ functions) and -- under `set -o pipefail` -- fails the whole
    # pipeline (exit 141) even though the printed output was already correct.
    listdir="$(mktemp -d)"
    trap 'rm -rf "$listdir"' RETURN
    # -F'\t' / print $1,$2: same reason as everywhere else here -- $2 (the key) can contain a
    # space of its own (TEST_F/TEST_P), so it must stay one TAB-delimited field, not re-split on
    # whitespace. `print $1, $2` re-joins with a single space for display, same as the file format.
    build_current_data | awk -F'\t' '{print $1, $2}' | sort -k1,1nr >"$listdir/sorted.txt"
    head -n "$LIST_N" "$listdir/sorted.txt"
}

case "$MODE" in
    check) run_check ;;
    update) run_update ;;
    list) run_list ;;
esac
