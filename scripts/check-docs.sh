#!/usr/bin/env bash
#
# check-docs.sh -- integrity checks for docs/, sibling to check-file-sizes.sh/check-function-sizes.sh
# but checking CONTENT correctness instead of size. A doc tree drifts silently: a file gets renamed
# (verify-scripts-miss-untracked-files.md-style lessons apply here too -- a NEW file a
# git-ls-files-based scan would miss is exactly the kind of rename that leaves a stale reference
# behind), a section gets renumbered or deleted, a link target moves -- and nothing catches it until
# a reader clicks a dead link or a stale `§N` pointer in a Source/** comment sends them to the wrong
# place (or nowhere). Six checks, all against the WORKING TREE via `find` (never `git ls-files` --
# that misses untracked new files, the exact trap that bit an earlier verify script in this repo):
#
#   A. Doc filename convention -- every docs/**/*.md basename must be lowercase kebab-case
#      (README.md exempt). Ratcheted: a violating legacy file must be in the baseline as a
#      `naming <path>` entry, or the check fails; a baseline entry for a file that no longer
#      violates is stale and must be removed via --update. This is the ONE ratcheted check --
#      grandfathers the pre-existing snake_case docs tree until the upcoming rename PR.
#   B. Markdown link targets resolve -- every `](target)` in every *.md whose target is a relative
#      .md path (optionally `#anchor`) must resolve to a real file, and the anchor (if any) must
#      match a GitHub-style heading slug in that file. NOT baselined -- a broken link is always a
#      hard failure. A link that normalizes to above this repo's root (e.g. `../CLAUDE.md` from the
#      workspace-root map) is skipped -- a repo-scoped checker has no way to verify a path outside
#      its own tree, and whether it resolves depends on where the repo happens to be checked out (a
#      linked worktree under .claude/worktrees/ sits one level deeper than a normal clone).
#   C. `docs/...` paths named anywhere resolve -- catches a rename that missed a Source/** comment,
#      a CLAUDE.md line, or another doc's prose (not just markdown link syntax). NOT baselined. The
#      pattern requires the character immediately before `docs/` to be neither `/` nor alphanumeric
#      (start-of-string, whitespace, punctuation) -- a qualified path like `synth-platform/docs/x.md`
#      does not match, so this never misidentifies a DIFFERENT repo's docs/ tree as our own.
#   D. `§`-section references resolve -- `docs/<path>.md` followed by up to 12 characters then
#      `§<N>`/`§<N.M>`/`§<N.M.K>` must name a section that exists in that doc (a heading
#      `## 5.3 Foo` satisfies both `§5.3` and the coarser `§5`). NOT baselined -- ZERO TOLERANCE:
#      every `§`-reference in this repo names a section that actually exists, always. There is no
#      grandfathering here (unlike check A) -- a stale section reference is actively misleading (it
#      sends a reader to the wrong place, or nowhere), so it is fixed at the point it goes stale,
#      not parked for later. FRO169 fixed the last 38 stale references that had accumulated; see
#      docs/docs-guard.md for the mechanism this enforces going forward.
#   E. `docs/README.md` map completeness -- every `docs/**/*.md` file except README.md itself must
#      be linked at least once from `docs/README.md`, and every link `docs/README.md` makes into
#      docs/ must resolve to a real file. NOT baselined -- the map is either complete or it isn't.
#   F. `docs/<path>.md#<slug>` mentions resolve OUTSIDE markdown link syntax too -- check B only
#      looks inside `](target)` syntax in *.md files, so a bare mention naming a real doc plus a
#      bogus anchor in a Source/**/*.cpp comment, a *.sh/*.yml file, or *.md prose that isn't a link
#      was invisible to every existing check. NOT baselined -- ZERO TOLERANCE, same as
#      B/C/D/E. FRO196: added ahead of the FRO166 docs restructure, which converts ~500 `§N`
#      references (hard-gated by check D) into `#anchor` references -- without this check, that
#      restructure would trade gated references for ungated ones. Reuses check B's own slug table
#      (never a second slug implementation) and check C's boundary rule (see awk_scan_docs_path) so
#      a qualified sibling-repo path like `synth-platform/docs/x.md#y` is never misread as ours
#      either. A `docs/<path>.md#<slug>` mention whose doc doesn't exist at all is check C's failure
#      to report, not this one's -- skipped here to avoid double-reporting the same mistake twice.
#
# Usage:
#   bash scripts/check-docs.sh                  # check the tree (A against baseline; B-F hard)
#   bash scripts/check-docs.sh --update         # rewrite the naming baseline from the current tree
#   bash scripts/check-docs.sh --update --allow-growth  # ...and let a new naming entry through
#   bash scripts/check-docs.sh --list           # summarize current violations in every check
#   bash scripts/check-docs.sh --root <dir>     # scan a different repo root (for tests)
#   bash scripts/check-docs.sh -h|--help
#
# Environment:
#   DOCS_BASELINE  baseline file path (default: <root>/scripts/docs-baseline.txt) -- lets
#                  scripts/tests/check-docs.test.sh point this at a throwaway file
#
# Exit status (check mode only -- --update/--list/--help always exit 0 on success):
#   0  no violation
#   1  any check-B/C/D/E/F failure, any check-A violation not (or no longer) covered by the
#      baseline, or the tree has zero .md files in scope (see the vacuous-pass fail-safe below)
#
# Portable bash 3.2 (macOS default) + grep/sed/find -- no python, no GNU-only flags. Every bash
# array is accessed only through an explicit length guard (`[ "${#arr[@]}" -gt 0 ]`) or by numeric
# index up to a separately tracked count -- bash 3.2 treats `"${arr[@]}"` on a zero-element array as
# an unbound-variable error under `set -u` (see scripts/ci-local.sh's own note on this).
#
# LC_ALL=C below, and every regex avoids a lazy quantifier: GNU grep (the ubuntu-latest CI runner)
# and BSD/ugrep (macOS) do not agree on `.{0,12}?`, and locale-dependent character classes can
# differ between them too -- greedy `.{0,12}` plus taking only the FIRST match downstream keeps the
# result identical on both.

set -uo pipefail
export LC_ALL=C

# Same reasoning as check-file-sizes.sh: a git hook (or nested harness run) inherits GIT_DIR (and
# sometimes GIT_WORK_TREE/GIT_INDEX_FILE), which can point `git rev-parse --show-toplevel` at the
# wrong directory. Strip it so every git call below does normal discovery.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

# Text file extensions this guard scans -- source comments, scripts and CI config can all carry a
# `docs/...` reference or a `§`-section pointer, not just the docs themselves.
EXTENSIONS=(md cpp h sh yml)

# Path prefixes that never count, regardless of extension -- generated/vendored/scratch trees, or
# (worktrees/, .claude/) Claude Code's own workspace scratch, whose contents have nothing to do
# with this repo's authored docs.
EXCLUDED_PREFIXES=(
    "build"                                # any local build output dir (build, build-ci-local, ...)
    ".claude/"                             # Claude Code workspace scratch (worktrees, etc.)
    "mockups/"                             # design mockup HTML, not shipped docs
    "assets/"                              # binary/media assets, tracked but not docs
    "worktrees/"                           # a loose worktree dropped outside .claude/ (shouldn't
                                            # exist per root CLAUDE.md, but never scan one if it does)
    "Tests/fixtures/"                      # recorded AI-patch JSON corpora, not authored docs
    "Tools/TimelineOpsHarness/Fixtures/"   # recorded TimelineOps JSON fixtures, not authored docs
)

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-docs.sh [--update] [--list] [--root <dir>] [-h|--help]

Six checks against docs/ and every in-scope *.cpp/*.h/*.sh/*.yml file in the repo (Source/,
Tests/, Tools/, scripts/, .github/ -- see EXTENSIONS/EXCLUDED_PREFIXES below):
doc filename convention (A), markdown link targets (B), `docs/...` path mentions (C), `§`-section
references (D), docs/README.md map completeness (E), `docs/...#anchor` mentions outside markdown
link syntax (F). Only A is ratcheted, against scripts/docs-baseline.txt; B, C, D, E, F are always a
hard failure (zero tolerance, never baselined). See this script's own header comment for the full
mechanism.

  (no flags)     Check the tree. Exit 1 on any violation.
  --update       Rewrite the naming baseline from the current tree's check-A violations and print
                 what changed. Refuses to add a new `naming` entry -- pass --allow-growth to let it
                 through as a deliberate, reviewed exception.
  --allow-growth Only meaningful with --update: let a new naming entry through, printing a
                 ::warning:: so it stays visible in CI logs and PR review.
  --list         Summarize current violations in every check (not just what's un-baselined).
  --root <dir>   Repo root to scan (default: `git rev-parse --show-toplevel` from this script's
                 own directory). Lets tests point this at a throwaway fixture repo.
  -h, --help     Show this message and exit.

Environment:
  DOCS_BASELINE  Baseline file path (default: <root>/scripts/docs-baseline.txt).
USAGE
}

MODE="check"
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
            ;;
        --root)
            shift
            if [ $# -eq 0 ]; then
                echo "check-docs: --root requires an argument" >&2
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
            echo "check-docs: unknown argument '$1' (see --help)" >&2
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
        echo "::error::check-docs: 'git rev-parse --show-toplevel' failed from $SCRIPT_DIR -- refusing to guess ROOT (pass --root explicitly to override)" >&2
        exit 1
    fi
fi

DOCS_BASELINE="${DOCS_BASELINE:-$ROOT/scripts/docs-baseline.txt}"

# --- scanning -------------------------------------------------------------------------------

# list_scanned_paths -- every working-tree file under $ROOT matching EXTENSIONS, minus excluded
# prefixes and anything under a `.git/` directory, one relative path per line. Deliberately `find`,
# not `git ls-files`: an untracked new file (e.g. a doc mid-rename, not yet `git add`ed) is exactly
# what this guard exists to catch, and a git-index-based scan would silently skip it.
list_scanned_paths() {
    local -a name_args
    name_args=("(")
    local ext first=1
    for ext in "${EXTENSIONS[@]}"; do
        if [ "$first" -eq 1 ]; then
            first=0
        else
            name_args+=("-o")
        fi
        name_args+=("-name" "*.$ext")
    done
    name_args+=(")")

    find "$ROOT" \
        \( -type d \( -name .git -o -name 'build*' -o -name .claude \) -prune \) -o \
        \( -type f "${name_args[@]}" -print \) |
        sed "s|^$ROOT/||" | while IFS= read -r path; do
        case "$path" in
            .git/* | */.git/*) continue ;;
        esac
        excluded=0
        for prefix in "${EXCLUDED_PREFIXES[@]}"; do
            case "$path" in
                "$prefix"*)
                    excluded=1
                    break
                    ;;
            esac
        done
        [ "$excluded" -eq 1 ] && continue
        printf '%s\n' "$path"
    done
}

# --- path normalization ----------------------------------------------------------------------

# normalize_rel_path <path> -- collapse `.`/`..` components in a `/`-separated relative path.
# Index-based (never `unset`+re-expand an array) so it never trips the bash-3.2 empty-array
# unbound-variable trap under `set -u`: `n` tracks the logical length and every access is by
# explicit index `0..n-1`, so an empty result never requires expanding `"${parts[@]}"`.
normalize_rel_path() {
    local input="$1" comp
    local -a parts
    parts=()
    local n=0
    local IFS='/'
    for comp in $input; do
        case "$comp" in
            '' | '.') continue ;;
            '..')
                if [ "$n" -gt 0 ] && [ "${parts[$((n - 1))]}" != ".." ]; then
                    n=$((n - 1))
                else
                    parts[$n]=".."
                    n=$((n + 1))
                fi
                ;;
            *)
                parts[$n]="$comp"
                n=$((n + 1))
                ;;
        esac
    done
    local out="" i=0
    while [ "$i" -lt "$n" ]; do
        if [ -z "$out" ]; then out="${parts[$i]}"; else out="$out/${parts[$i]}"; fi
        i=$((i + 1))
    done
    printf '%s' "$out"
}

# --- heading helpers --------------------------------------------------------------------------

# build_slugs_file <pairs-file (md-only)> <out-file> -- "relpath\tslug" (GitHub-style: lowercase,
# strip everything not alnum/space/hyphen, spaces->hyphens) for every heading line in every *.md
# file in <pairs-file>. One awk process for every file's headings, computed ONCE regardless of how
# many links reference them -- check_b_violations used to call a 5-process grep|sed|tr|sed|tr
# pipeline PER anchored link (this repo has ~95 of them), which dominated this script's runtime
# under this environment's per-process overhead; a single upfront table plus a plain lookup fixed
# it. Same getline-per-file technique as awk_scan/build_headings_file.
build_slugs_file() {
    local pairs="$1" out="$2"
    awk -v filelist="$pairs" '
        BEGIN {
            while ((getline pairline < filelist) > 0) {
                n = split(pairline, cols, "\t")
                if (n < 2) continue
                relpath = cols[1]
                abspath = cols[2]
                while ((getline line < abspath) > 0) {
                    if (line !~ /^#{1,6}[ \t]/) continue
                    text = line
                    sub(/^#{1,6}[ \t]+/, "", text)
                    text = tolower(text)
                    gsub(/[^a-z0-9 \t-]/, "", text)
                    gsub(/[ \t]/, "-", text)
                    print relpath "\t" text
                }
                close(abspath)
            }
            close(filelist)
            exit
        }
    ' >"$out"
}

# slug_exists <relpath> <anchor> <slugs-file> -- true if <relpath> has a heading whose slug is
# exactly <anchor>.
slug_exists() {
    local doc="$1" anchor="$2" slugs_file="$3"
    awk -F'\t' -v d="$doc" -v a="$anchor" '$1 == d && $2 == a { found = 1 } END { exit !found }' "$slugs_file"
}

# --- scan materialization ---------------------------------------------------------------------
#
# Every check below needs the same per-line regex extraction over potentially hundreds of files.
# Doing that with nested `grep | while read | grep | while read` pipelines (one bash subshell per
# FILE, sometimes a second per LINE) is what an earlier version of this script did -- and it's
# fragile: that many nested subshells/pipes intermittently crashed outright in this sandbox rather
# than just running slow. `awk_scan` below does the whole extraction as ONE awk process per check
# (awk opens every file itself via `getline < file`, so there's no per-file subshell at all), and
# every downstream step reads the materialized result back from a real FILE with a single, flat
# `while read` loop -- no pipe deep enough to recreate the problem.

# build_pairs_file <scanned-file> <out-file> [md-only] -- "relpath\tabspath" for every path listed
# in <scanned-file> (one relpath per line, e.g. from list_scanned_paths), one per line in <out-file>.
# "md-only" (any non-empty 3rd arg) restricts to *.md. Takes an already-computed scan rather than
# calling list_scanned_paths itself: `find` over the whole tree is the single most expensive part
# of this script (see the perf note above run_check), so every caller in a single invocation
# shares ONE scan instead of re-walking the tree per check.
build_pairs_file() {
    local scanned="$1" out="$2" md_only="${3:-}"
    : >"$out"
    while IFS= read -r path; do
        [ -n "$path" ] || continue
        if [ -n "$md_only" ]; then
            case "$path" in
                *.md) ;;
                *) continue ;;
            esac
        fi
        printf '%s\t%s/%s\n' "$path" "$ROOT" "$path"
    done <"$scanned" >"$out"
}

# awk_scan <pairs-file> <ere-pattern> -- "relpath\tfnr\tmatch" for every non-overlapping match of
# <ere-pattern> in every file named in <pairs-file>. One awk process total. Every such script in
# this file does all its work inside BEGIN and ends with an explicit `exit` -- without it, awk
# falls through to its normal main loop and tries to read a record from stdin (no filename operand
# was given), which is harmless when stdin is already closed/empty (every run in this script's own
# tests and in CI) but would BLOCK waiting for input if this ever runs with a real tty on stdin
# (e.g. the pre-commit hook during an interactive `git commit`). `exit` makes that impossible.
awk_scan() {
    local pairs="$1" pattern="$2"
    # awk applies escape-sequence processing to a `-v var=value` assignment (POSIX: same rules as
    # a string constant in the program text) -- an UNESCAPED `\]`/`\(`/`\.` etc in <pattern> can
    # silently lose its backslash (implementation-defined for an unrecognized escape), turning
    # `\]\([^)]*\)` into the unanchored `][^)]*` and matching from the wrong `]` entirely. Doubling
    # every backslash here survives that processing and restores the caller's literal pattern.
    local escaped_pattern="${pattern//\\/\\\\}"
    awk -v filelist="$pairs" -v pat="$escaped_pattern" '
        BEGIN {
            while ((getline pairline < filelist) > 0) {
                n = split(pairline, cols, "\t")
                if (n < 2) continue
                relpath = cols[1]
                abspath = cols[2]
                fnr = 0
                while ((getline line < abspath) > 0) {
                    fnr++
                    remaining = line
                    while ((idx = match(remaining, pat)) > 0) {
                        text = substr(remaining, idx, RLENGTH)
                        print relpath "\t" fnr "\t" text
                        if (RLENGTH == 0) { remaining = substr(remaining, idx + 1) } else { remaining = substr(remaining, idx + RLENGTH) }
                    }
                }
                close(abspath)
            }
            close(filelist)
            exit
        }
    '
}

# --- check B: markdown link targets ------------------------------------------------------------

# check_b_violations <pairs-file (md-only)> <slugs-file (from build_slugs_file)> --
# "relpath:line: message" per broken link. <slugs-file> is built once by the caller (run_check /
# run_list) and shared with check_f_violations, so B and F can never disagree about what counts as
# a valid slug -- there is exactly one slug implementation in this file.
check_b_violations() {
    local md_pairs="$1" slugs_file="$2"
    awk_scan "$md_pairs" '\]\([^)]*\)' | while IFS="$(printf '\t')" read -r path line rest; do
        [ -n "$path" ] || continue
        # Pure bash `dirname` equivalent -- the real /usr/bin/dirname is an external process, and
        # this loop runs it once per markdown link (500+ across the repo); at this environment's
        # per-process overhead that alone was a meaningful chunk of check B's runtime.
        dir="${path%/*}"
        [ "$dir" = "$path" ] && dir="."
        target="${rest#](}"
        target="${target%)}"
        case "$target" in
            http:* | https:* | mailto:* | '#'*) continue ;;
        esac
        case "$target" in
            *.md | *.md'#'*) ;;
            *) continue ;;
        esac
        anchor=""
        tpath="$target"
        case "$target" in
            *'#'*)
                tpath="${target%%#*}"
                anchor="${target#*#}"
                ;;
        esac
        if [ "$dir" = "." ]; then
            resolved="$(normalize_rel_path "$tpath")"
        else
            resolved="$(normalize_rel_path "$dir/$tpath")"
        fi
        # A link that still starts with ".." after normalization points above this repo's root
        # (e.g. the workspace-root map link) -- out of scope for a repo-scoped checker; see the
        # header comment.
        case "$resolved" in
            '..' | '../'*) continue ;;
        esac
        if [ ! -f "$ROOT/$resolved" ]; then
            printf '%s:%s: link target '\''%s'\'' resolves to '\''%s'\'', which does not exist\n' "$path" "$line" "$target" "$resolved"
            continue
        fi
        if [ -n "$anchor" ]; then
            if ! slug_exists "$resolved" "$anchor" "$slugs_file"; then
                printf '%s:%s: link target '\''%s'\'' has no heading matching anchor '\''#%s'\''\n' "$path" "$line" "$target" "$anchor"
            fi
        fi
    done
}

# --- check C: docs/... path mentions -----------------------------------------------------------

# awk_scan_docs_path <pairs-file> -- "relpath\tfnr\tmatch" for every `docs/<path>.md` mention,
# BOUNDARY-CHECKED: the character immediately before "docs/" must be neither `/` nor alphanumeric.
# Without this, a qualified path like `synth-platform/docs/billing.md` would be misread as naming
# OUR docs/ tree (FRO169: synth-platform is a private sibling repo, and even after the cross-repo
# prose in this repo stopped naming its paths outright, the regex itself needed tightening so a
# FUTURE qualified mention can't slip through either). Same offset-tracking technique as
# check_d_violations' pass 3 below -- see that function's comment for why.
awk_scan_docs_path() {
    local pairs="$1"
    awk -v filelist="$pairs" '
        BEGIN {
            pat = "docs\\/[A-Za-z0-9_.\\/-]+\\.md"
            while ((getline pairline < filelist) > 0) {
                n = split(pairline, cols, "\t")
                if (n < 2) continue
                relpath = cols[1]
                abspath = cols[2]
                fnr = 0
                while ((getline line < abspath) > 0) {
                    fnr++
                    remaining = line
                    offset = 0
                    while ((idx = match(remaining, pat)) > 0) {
                        abs_idx = offset + idx
                        ok = 1
                        if (abs_idx > 1) {
                            prevchar = substr(line, abs_idx - 1, 1)
                            if (prevchar ~ /[A-Za-z0-9\/]/) { ok = 0 }
                        }
                        if (ok) {
                            text = substr(remaining, idx, RLENGTH)
                            print relpath "\t" fnr "\t" text
                        }
                        offset += idx + RLENGTH - 1
                        remaining = substr(remaining, idx + RLENGTH)
                    }
                }
                close(abspath)
            }
            close(filelist)
            exit
        }
    '
}

# check_c_violations <pairs-file> -- "relpath:line: message" per docs/... mention that doesn't
# resolve. Scans every in-scope file, not just *.md -- this is what catches a rename that missed a
# Source/** comment.
check_c_violations() {
    local pairs="$1"
    awk_scan_docs_path "$pairs" | while IFS="$(printf '\t')" read -r path line match; do
        [ -n "$path" ] || continue
        if [ ! -f "$ROOT/$match" ]; then
            printf '%s:%s: '\''%s'\'' does not exist\n' "$path" "$line" "$match"
        fi
    done
}

# --- check D: §-section references -------------------------------------------------------------

# check_d_violations <pairs-file> -- "relpath:line: message" per stale §-reference in the current
# tree. ZERO TOLERANCE -- unlike check A this is never baselined; see the header comment for why.
#
# ONE awk process does everything check D needs: loads every docs/**/*.md heading number into an
# in-memory table, then scans every in-scope file's content for `docs/<path>.md ... §N` mentions
# and checks each one against that table directly -- no per-occurrence subshell or external `awk`
# call. An earlier version split this into three pieces (a headings-table builder, an occurrence
# extractor, and a `section_exists` helper invoked once per occurrence via its own `awk`
# subprocess) for clarity; correct, but with 500+ §-references in this repo, spawning a process per
# occurrence was the single largest cost in this script under this environment's per-process
# overhead. Same boundary check as check C (awk_scan_docs_path) -- the character immediately
# before "docs/" must be neither `/` nor alphanumeric, so a qualified path like
# `synth-platform/docs/foo.md §3` is never misread as naming OUR docs/ tree -- and the same
# greedy-`.{0,12}`-then-first-`§` rule for what one occurrence means (see check C's own comment on
# the GNU-vs-BSD lazy-quantifier mismatch that greedy works around).
check_d_violations() {
    local pairs="$1"
    awk -v filelist="$pairs" '
        BEGIN {
            # Pass 1: which relpaths actually exist (a docpath not in this set is check C''s to
            # report, not ours -- skip it here rather than double-report).
            while ((getline pairline < filelist) > 0) {
                split(pairline, cols, "\t")
                exists[cols[1]] = 1
            }
            close(filelist)

            # Pass 2: every h2-h6 heading number in every docs/**/*.md file, keyed by doc, as a
            # SOH-separated list (awk has no portable 2D array iteration, so a query does its own
            # split()+loop below rather than a hash lookup -- cheap: a doc has a handful of
            # headings, not hundreds).
            while ((getline pairline < filelist) > 0) {
                split(pairline, cols, "\t")
                relpath = cols[1]; abspath = cols[2]
                if (relpath !~ /^docs\/.*\.md$/) continue
                while ((getline line < abspath) > 0) {
                    if (line !~ /^#{2,6}[ \t]/) continue
                    rest = line
                    sub(/^#{2,6}[ \t]+/, "", rest)
                    if (match(rest, /^[0-9]+(\.[0-9]+)*/)) {
                        num = substr(rest, RSTART, RLENGTH)
                        after = substr(rest, RLENGTH + 1)
                        if (after == "" || after ~ /^\.?[ \t]/ || after ~ /^\.$/) {
                            headings[relpath] = (relpath in headings) ? headings[relpath] "\x01" num : num
                        }
                    }
                }
                close(abspath)
            }
            close(filelist)

            outer = "docs\\/[A-Za-z0-9_.\\/-]+\\.md.{0,12}§[ \t]*[0-9]+(\\.[0-9]+){0,2}"
            docre = "^docs\\/[A-Za-z0-9_.\\/-]+\\.md"
            secre = "§[ \t]*[0-9]+(\\.[0-9]+){0,2}"

            # Pass 3: scan every in-scope file'"'"'s content for occurrences and check each one
            # in-memory against the headings table built above.
            while ((getline pairline < filelist) > 0) {
                split(pairline, cols, "\t")
                relpath = cols[1]; abspath = cols[2]
                fnr = 0
                while ((getline line < abspath) > 0) {
                    fnr++
                    remaining = line
                    offset = 0
                    while ((idx = match(remaining, outer)) > 0) {
                        abs_idx = offset + idx
                        ok = 1
                        if (abs_idx > 1) {
                            prevchar = substr(line, abs_idx - 1, 1)
                            if (prevchar ~ /[A-Za-z0-9\/]/) { ok = 0 }
                        }
                        if (ok) {
                            text = substr(remaining, idx, RLENGTH)
                            match(text, docre)
                            docpath = substr(text, RSTART, RLENGTH)
                            spos = index(text, "§")
                            secpart = substr(text, spos)
                            match(secpart, secre)
                            secnum = substr(secpart, RSTART, RLENGTH)
                            sub(/^§[ \t]*/, "", secnum)
                            if (docpath != "" && secnum != "" && (docpath in exists)) {
                                found = 0
                                if (docpath in headings) {
                                    ns = split(headings[docpath], nums, "\x01")
                                    qq = secnum "."
                                    for (i = 1; i <= ns; i++) {
                                        if (nums[i] == secnum || index(nums[i], qq) == 1) { found = 1; break }
                                    }
                                }
                                if (!found) {
                                    print relpath ":" fnr ": " docpath " §" secnum " -- no such section in " docpath
                                }
                            }
                        }
                        offset += idx + RLENGTH - 1
                        remaining = substr(remaining, idx + RLENGTH)
                    }
                }
                close(abspath)
            }
            close(filelist)
            exit
        }
    '
}

# --- check E: docs/README.md map completeness --------------------------------------------------

# readme_linked_docs -- "docs/<name>.md" (sorted, deduped) for every markdown-link target
# docs/README.md points at within docs/ itself (relative targets ending in .md, resolved against
# README.md's own directory -- i.e. docs/). Reuses awk_scan's generic extractor on a one-line
# pairs file built just for docs/README.md.
readme_linked_docs() {
    local readme_pairs
    readme_pairs="$(mktemp)"
    printf 'docs/README.md\t%s/docs/README.md\n' "$ROOT" >"$readme_pairs"
    awk_scan "$readme_pairs" '\]\([^)]*\)' | while IFS="$(printf '\t')" read -r path line rest; do
        [ -n "$path" ] || continue
        target="${rest#](}"
        target="${target%)}"
        case "$target" in
            http:* | https:* | mailto:* | '#'*) continue ;;
        esac
        case "$target" in
            *.md | *.md'#'*) ;;
            *) continue ;;
        esac
        tpath="${target%%#*}"
        resolved="$(normalize_rel_path "docs/$tpath")"
        printf '%s\n' "$resolved"
    done | sort -u
    rm -f "$readme_pairs"
}

# check_e_violations <scanned-file> -- "relpath: message" per docs/README.md map-completeness
# violation: a real docs/**/*.md file (other than README.md) that the map never links, or a map
# link that resolves to a file that doesn't exist (check B already catches the latter as a broken
# link -- this is belt-and-braces so check E stands on its own without depending on check B having
# run first). <scanned-file> is list_scanned_paths' own output, shared across every check in this
# run -- see the perf note above run_check.
check_e_violations() {
    local scanned="$1"
    local linked_file real_file
    linked_file="$(mktemp)"
    real_file="$(mktemp)"
    readme_linked_docs >"$linked_file"
    while IFS= read -r path; do
        case "$path" in
            docs/*.md) [ "$path" = "docs/README.md" ] || printf '%s\n' "$path" ;;
        esac
    done <"$scanned" | sort >"$real_file"

    while IFS= read -r path; do
        [ -n "$path" ] || continue
        if ! grep -qxF -- "$path" "$linked_file"; then
            printf '%s: not linked from docs/README.md\n' "$path"
        fi
    done <"$real_file"

    while IFS= read -r linked; do
        [ -n "$linked" ] || continue
        if [ ! -f "$ROOT/$linked" ]; then
            printf 'docs/README.md: links to %s, which does not exist\n' "$linked"
        fi
    done <"$linked_file"

    rm -f "$linked_file" "$real_file"
}

# --- check F: docs/...#anchor mentions outside markdown link syntax ---------------------------

# check_f_violations <pairs-file (all)> <slugs-file (from build_slugs_file, md-only)> --
# "relpath:line: message" per `docs/<path>.md#<anchor>` mention, in ANY in-scope file, whose anchor
# doesn't match a real heading slug in the target doc. NOT baselined -- zero tolerance, same as
# B/C/D/E. This is what stops the FRO166 docs restructure (converting ~500 `§N` references, hard-
# gated by check D, into `#anchor` references) from trading gated references for ungated ones: an
# anchor mention written as plain prose or a Source/**/*.cpp comment -- not `](target)` markdown
# link syntax -- is invisible to check B, which only looks inside that syntax in *.md files.
#
# ONE awk process does everything, same reasoning as check_d_violations above it: with ~500 anchor
# references coming in the wake of this ticket, a per-occurrence subshell/subprocess (as an earlier
# version of this script paid for elsewhere) would dominate runtime the same way it used to for
# check B. Pass 1 loads which relpaths actually exist (a docpath that doesn't exist at all is check
# C's failure to report, not ours -- skipped here to avoid double-reporting the same mistake under
# two different checks). Pass 2 loads <slugs-file> -- the EXACT SAME table check_b_violations uses,
# built once by build_slugs_file and shared by both callers in run_check/run_list -- into an
# in-memory set, so check B and check F can never disagree about what counts as a valid slug (no
# second slug implementation exists anywhere in this file). Pass 3 scans every in-scope file for the
# `docs/<path>.md#<anchor>` pattern with the SAME boundary rule as check C's awk_scan_docs_path (the
# character immediately before "docs/" must be neither `/` nor alphanumeric, so a qualified path
# like `synth-platform/docs/x.md#y` is never misread as naming OUR docs/ tree either), and checks
# each occurrence in-memory against the two tables built above.
check_f_violations() {
    local pairs="$1" slugs_file="$2"
    awk -v filelist="$pairs" -v slugsfile="$slugs_file" '
        BEGIN {
            # Pass 1: which relpaths actually exist (a docpath not in this set is check C'"'"'s to
            # report, not ours -- skip it here rather than double-report).
            while ((getline pairline < filelist) > 0) {
                split(pairline, cols, "\t")
                exists[cols[1]] = 1
            }
            close(filelist)

            # Pass 2: check B'"'"'s own slug table, keyed "relpath\x01slug" for an O(1) lookup.
            while ((getline sline < slugsfile) > 0) {
                split(sline, scols, "\t")
                slugs[scols[1] "\x01" scols[2]] = 1
            }
            close(slugsfile)

            pat = "docs\\/[A-Za-z0-9_.\\/-]+\\.md#[A-Za-z0-9_-]+"

            # Pass 3: scan every in-scope file'"'"'s content for occurrences and check each one
            # in-memory against the tables built above.
            while ((getline pairline < filelist) > 0) {
                n = split(pairline, cols, "\t")
                if (n < 2) continue
                relpath = cols[1]; abspath = cols[2]
                fnr = 0
                while ((getline line < abspath) > 0) {
                    fnr++
                    remaining = line
                    offset = 0
                    while ((idx = match(remaining, pat)) > 0) {
                        abs_idx = offset + idx
                        ok = 1
                        if (abs_idx > 1) {
                            prevchar = substr(line, abs_idx - 1, 1)
                            if (prevchar ~ /[A-Za-z0-9\/]/) { ok = 0 }
                        }
                        if (ok) {
                            text = substr(remaining, idx, RLENGTH)
                            hashpos = index(text, "#")
                            docpath = substr(text, 1, hashpos - 1)
                            anchor = substr(text, hashpos + 1)
                            if ((docpath in exists) && !((docpath "\x01" anchor) in slugs)) {
                                printf "%s:%d: anchor mention '"'"'%s'"'"' has no heading matching anchor '"'"'#%s'"'"'\n", relpath, fnr, text, anchor
                            }
                        }
                        offset += idx + RLENGTH - 1
                        remaining = substr(remaining, idx + RLENGTH)
                    }
                }
                close(abspath)
            }
            close(filelist)
            exit
        }
    '
}

# --- naming (check A) ---------------------------------------------------------------------------

# current_naming_violations <scanned-file> -- every docs/**/*.md relpath whose basename isn't
# lowercase kebab (README.md exempt at any depth), sorted. <scanned-file> as above.
current_naming_violations() {
    local scanned="$1"
    while IFS= read -r path; do
        case "$path" in
            docs/*.md) ;;
            *) continue ;;
        esac
        base="${path##*/}"
        [ "$base" = "README.md" ] && continue
        if ! printf '%s' "$base" | grep -Eq '^[a-z0-9]+(-[a-z0-9]+)*\.md$'; then
            printf '%s\n' "$path"
        fi
    done <"$scanned" | sort
}

# --- baseline I/O --------------------------------------------------------------------------------

baseline_naming_entries() {
    [ -f "$DOCS_BASELINE" ] || return 0
    grep -E '^naming[[:space:]]+' "$DOCS_BASELINE" | awk '{print $2}' | sort
}

write_step_summary() {
    [ -n "${GITHUB_STEP_SUMMARY:-}" ] && printf '%s\n' "$1" >>"$GITHUB_STEP_SUMMARY"
}

# --- check mode -----------------------------------------------------------------------------

run_check() {
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT

    # ONE `find` over the whole tree, shared by every check below -- see build_pairs_file's own
    # comment on why (this alone took check-docs.sh from ~4.2s to well under 2s on the real tree:
    # `find` was being re-walked 4 times per run, once per consumer, before this).
    list_scanned_paths >"$workdir/scanned.txt"
    build_pairs_file "$workdir/scanned.txt" "$workdir/pairs_all.txt"
    build_pairs_file "$workdir/scanned.txt" "$workdir/pairs_md.txt" md_only

    md_count="$(wc -l <"$workdir/pairs_md.txt" | tr -d '[:space:]')"
    # Vacuous-pass fail-safe: scanning zero .md files is NOT a legitimate "nothing to check" here
    # (unlike check-file-sizes.sh, where an empty --root is fine) -- this repo always has docs/,
    # so zero almost certainly means ROOT resolved wrong. Fail loudly rather than report success.
    if [ "$md_count" -eq 0 ]; then
        echo "::error::check-docs: 0 .md files scanned under $ROOT -- ROOT is almost certainly wrong; refusing to report a vacuous pass" >&2
        return 1
    fi

    errors=0

    # Built once, shared by check B and check F -- see check_b_violations' own comment on why.
    build_slugs_file "$workdir/pairs_md.txt" "$workdir/slugs.txt"

    b_out="$(check_b_violations "$workdir/pairs_md.txt" "$workdir/slugs.txt")"
    if [ -n "$b_out" ]; then
        while IFS= read -r line; do
            [ -n "$line" ] || continue
            echo "::error::check-docs (link): $line"
            errors=$((errors + 1))
        done <<<"$b_out"
    fi

    c_out="$(check_c_violations "$workdir/pairs_all.txt")"
    if [ -n "$c_out" ]; then
        while IFS= read -r line; do
            [ -n "$line" ] || continue
            echo "::error::check-docs (docs/ path): $line"
            errors=$((errors + 1))
        done <<<"$c_out"
    fi

    d_out="$(check_d_violations "$workdir/pairs_all.txt")"
    d_current_n=0
    if [ -n "$d_out" ]; then
        while IFS= read -r line; do
            [ -n "$line" ] || continue
            echo "::error::check-docs (section): $line"
            errors=$((errors + 1))
            d_current_n=$((d_current_n + 1))
        done <<<"$d_out"
    fi

    e_out="$(check_e_violations "$workdir/scanned.txt")"
    if [ -n "$e_out" ]; then
        while IFS= read -r line; do
            [ -n "$line" ] || continue
            echo "::error::check-docs (map): $line"
            errors=$((errors + 1))
        done <<<"$e_out"
    fi

    f_out="$(check_f_violations "$workdir/pairs_all.txt" "$workdir/slugs.txt")"
    if [ -n "$f_out" ]; then
        while IFS= read -r line; do
            [ -n "$line" ] || continue
            echo "::error::check-docs (anchor): $line"
            errors=$((errors + 1))
        done <<<"$f_out"
    fi

    current_naming_violations "$workdir/scanned.txt" >"$workdir/naming_current.txt"
    baseline_naming_entries >"$workdir/naming_baseline.txt"
    naming_current_n=0
    while IFS= read -r p; do
        [ -n "$p" ] || continue
        if ! grep -qxF -- "$p" "$workdir/naming_baseline.txt"; then
            echo "::error::check-docs (naming): $p -- basename is not lowercase kebab-case and isn't in the baseline; run --update"
            errors=$((errors + 1))
        fi
        naming_current_n=$((naming_current_n + 1))
    done <"$workdir/naming_current.txt"
    while IFS= read -r p; do
        [ -n "$p" ] || continue
        if ! grep -qxF -- "$p" "$workdir/naming_current.txt"; then
            echo "::error::check-docs (naming): $p is a stale baseline entry (no longer violates, or file missing) -- run --update"
            errors=$((errors + 1))
        fi
    done <"$workdir/naming_baseline.txt"

    echo "check-docs: $md_count .md files scanned, $naming_current_n naming violations, $d_current_n stale section references, $errors total error(s)"

    write_step_summary "### Docs integrity guard"
    write_step_summary "- .md files scanned: $md_count"
    write_step_summary "- naming violations: $naming_current_n"
    write_step_summary "- stale section references: $d_current_n"
    write_step_summary "- total errors: $errors"

    [ "$errors" -eq 0 ]
}

# --- update mode ----------------------------------------------------------------------------

run_update() {
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT

    list_scanned_paths >"$workdir/scanned.txt"
    build_pairs_file "$workdir/scanned.txt" "$workdir/pairs_all.txt"

    current_naming_violations "$workdir/scanned.txt" >"$workdir/naming_new.txt"
    baseline_naming_entries >"$workdir/naming_old.txt"

    baseline_existed=0
    [ -f "$DOCS_BASELINE" ] && baseline_existed=1

    growth_errors=0
    growth_warnings=0

    if [ "$baseline_existed" -eq 1 ]; then
        while IFS= read -r p; do
            [ -n "$p" ] || continue
            if ! grep -qxF -- "$p" "$workdir/naming_old.txt"; then
                if [ "$ALLOW_GROWTH" -eq 1 ]; then
                    echo "::warning::$p is a new naming violation -- allowed via --allow-growth (reviewed exception)"
                    growth_warnings=$((growth_warnings + 1))
                else
                    echo "::error::$p is a new naming violation, not yet in the baseline -- rename it to kebab-case, or pass --allow-growth only for a deliberate, reviewed exception"
                    growth_errors=$((growth_errors + 1))
                fi
            fi
        done <"$workdir/naming_new.txt"
    fi

    if [ "$growth_errors" -gt 0 ]; then
        echo "check-docs --update: refusing to write the baseline -- $growth_errors entr$([ "$growth_errors" -eq 1 ] && echo y || echo ies) would add to it (see errors above). The ratchet only tightens; pass --allow-growth only for a deliberate, reviewed exception." >&2
        return 1
    fi

    {
        echo "# Docs-integrity ratchet baseline (scripts/check-docs.sh). ONE entry kind, STRICT ratchet:"
        echo "#   naming <path>  -- a legacy docs/**/*.md file whose basename isn't lowercase kebab-case."
        echo "#                     Presence-only: fixed (renamed) means the entry is simply gone from a"
        echo "#                     future --update. Never add a NEW entry -- rename the file instead."
        echo "# --allow-growth is the deliberate, reviewed exception. See check-docs.sh's own header comment"
        echo "# for the full mechanism, and docs/docs-guard.md for why only naming is grandfathered -- every"
        echo "# other check (B/C/D/E) is zero-tolerance and never baselined."
        sort "$workdir/naming_new.txt" | sed 's/^/naming /'
    } >"$DOCS_BASELINE"

    naming_n="$(wc -l <"$workdir/naming_new.txt" | tr -d '[:space:]')"

    if [ "$growth_warnings" -gt 0 ]; then
        echo "check-docs --update: baseline rewritten ($naming_n naming entries; $growth_warnings raised via --allow-growth)."
    else
        echo "check-docs --update: baseline rewritten ($naming_n naming entries)."
    fi
}

# --- list mode ------------------------------------------------------------------------------

run_list() {
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT
    list_scanned_paths >"$workdir/scanned.txt"
    build_pairs_file "$workdir/scanned.txt" "$workdir/pairs_all.txt"
    build_pairs_file "$workdir/scanned.txt" "$workdir/pairs_md.txt" md_only
    build_slugs_file "$workdir/pairs_md.txt" "$workdir/slugs.txt"

    echo "=== naming violations (check A) ==="
    current_naming_violations "$workdir/scanned.txt"
    echo
    echo "=== broken markdown links (check B) ==="
    check_b_violations "$workdir/pairs_md.txt" "$workdir/slugs.txt"
    echo
    echo "=== unresolved docs/ path mentions (check C) ==="
    check_c_violations "$workdir/pairs_all.txt"
    echo
    echo "=== stale §-section references (check D) ==="
    check_d_violations "$workdir/pairs_all.txt"
    echo
    echo "=== docs/README.md map completeness (check E) ==="
    check_e_violations "$workdir/scanned.txt"
    echo
    echo "=== anchor mentions outside markdown link syntax (check F) ==="
    check_f_violations "$workdir/pairs_all.txt" "$workdir/slugs.txt"
}

case "$MODE" in
    check) run_check ;;
    update) run_update ;;
    list) run_list ;;
esac
