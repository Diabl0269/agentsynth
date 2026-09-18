#!/usr/bin/env bash
#
# check-docs.sh -- integrity checks for docs/, sibling to check-file-sizes.sh/check-function-sizes.sh
# but checking CONTENT correctness instead of size. A doc tree drifts silently: a file gets renamed
# (verify-scripts-miss-untracked-files.md-style lessons apply here too -- a NEW file a
# git-ls-files-based scan would miss is exactly the kind of rename that leaves a stale reference
# behind), a section gets renumbered or deleted, a link target moves -- and nothing catches it until
# a reader clicks a dead link or a stale `§N` pointer in a Source/** comment sends them to the wrong
# place (or nowhere). Seven checks, all against the WORKING TREE via `find` (never `git ls-files` --
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
#      docs/development/docs-guard.md for the mechanism this enforces going forward.
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
#   G. Bare basename references resolve -- checks C/D/F all require a literal `docs/` prefix before
#      a filename, so a reference written as a BARE backticked basename in prose (typically followed
#      by a `§N` section marker or `#anchor`, with no markdown link target at all) matched none of
#      them and was invisible to every check before FRO217. For every in-scope file, a backtick-
#      wrapped `` `<name>.md` `` token -- outside markdown link syntax (masked the same way check B
#      parses it) and with no `docs/` or other path prefix (the boundary rule below excludes both) --
#      must resolve to EXACTLY ONE real docs/**/*.md file by basename: matching zero is a doc that
#      is gone outright, matching more than one is itself a failure (a bare name a script cannot
#      resolve is one a reader cannot resolve either), checked EVERY time the bare name appears,
#      marker or none. Separately, whatever immediately follows the name -- whitespace only, up to 3
#      characters, narrower than check D's any-char budget; see check_g_violations' own comment for
#      why -- is checked as an OPTIONAL trailing `§N`/`§N.M`/`§N.M.K` marker or `#anchor`: present
#      and the basename resolves to exactly one doc, that marker must name a section/anchor that
#      actually exists in it; present and the basename resolves to zero docs, that is the "doc is
#      gone outright" failure above; absent, there is nothing more to check (a bare mention with no
#      marker is not a structured cross-reference this check gates). The fix for every failure here
#      is the same: write the full `docs/` path (converting it to a real link, or a full-path
#      mention checks C/D/F already cover). NOT baselined -- ZERO TOLERANCE, same as B/C/D/E/F.
#      Reuses check B's own slug table (for a trailing `#anchor`) and a new `build_headings_file`
#      shared table (for a trailing `§N`, also now used by check D) rather than adding a third
#      slug/section implementation -- see build_headings_file's own comment. A bare name that IS
#      inside proper markdown link syntax (`` [`name.md`](target) ``) is check B's business, not
#      this check's -- the whole `[text](target)` span is masked out of the line before this check
#      ever looks at it, so it is never double-reported here. FRO176's post-merge verification found
#      two live dead references of exactly this shape surviving a clean run (docs/timeline/scale-
#      assist.md and docs/control/plugin-card-layout.md, both fixed in the same PR that added this check) --
#      proof the gap was real, not theoretical.
#
# Usage:
#   bash scripts/check-docs.sh                  # check the tree (A against baseline; B-G hard)
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
#   1  any check-B/C/D/E/F/G failure, any check-A violation not (or no longer) covered by the
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
# `docs/...` reference or a `§`-section pointer, not just the docs themselves. FRO208 widened this
# from (md cpp h sh yml) to also cover txt/json/cmake/py: CMakeLists.txt alone carries five doc
# references (a section reference to docs/mixer.md, plus refs to docs/development/distribution.md,
# docs/architecture/architecture.md and docs/control/shortcuts.md) that no check could ever see, and a hand-authored
# Tools/**/Fixtures/*.json description field can cite a doc too (see the
# Tools/TimelineOpsHarness/Fixtures/ note below) -- verified to add ZERO new violations on the
# tree as of FRO208 landing, so it starts clean.
EXTENSIONS=(md cpp h sh yml txt json cmake py)

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
    "Tests/fixtures/"                      # recorded AI-patch JSON corpora -- these are RECORDED
                                            # MODEL OUTPUT (what a provider actually returned during
                                            # a captured session), not hand-authored docs, so a
                                            # docs-looking string that happens to appear inside one
                                            # is not ours to fix -- keep this excluded even though
                                            # FRO208 added .json to EXTENSIONS.
)
# NOTE: Tools/TimelineOpsHarness/Fixtures/ used to be excluded here too, on the same
# recorded-output reasoning as Tests/fixtures/ above -- but unlike Tests/fixtures/, those fixtures
# carry a hand-authored "description" field (not model output), so a stale doc reference inside one
# is exactly the kind of drift this guard exists to catch. FRO208 removed it from this list after
# it hid a real dead reference (Tools/TimelineOpsHarness/Fixtures/01-valid-three-op-envelope.json
# citing a section of a doc deleted by FRO172) behind BOTH the old extension list and this
# exclusion at once -- do not re-add it.

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-docs.sh [--update] [--list] [--root <dir>] [-h|--help]

Seven checks against docs/ and every in-scope *.cpp/*.h/*.sh/*.yml/*.txt/*.json/*.cmake/*.py file in
the repo (Source/, Tests/, Tools/, scripts/, .github/ -- see EXTENSIONS/EXCLUDED_PREFIXES below):
doc filename convention (A), markdown link targets (B), `docs/...` path mentions (C), `§`-section
references (D), docs/README.md map completeness (E), `docs/...#anchor` mentions outside markdown
link syntax (F), bare backticked basename references outside markdown link syntax and outside any
`docs/` path (G). Only A is ratcheted, against scripts/docs-baseline.txt; B, C, D, E, F, G are
always a hard failure (zero tolerance, never baselined). See this script's own header comment for
the full mechanism.

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

# check_b_violations through check_g_violations (and the awk helpers they share) live in
# scripts/lib/check-docs-checks.sh -- split out once this file grew past the repo file-size cap;
# see that file's own header comment for why.
# shellcheck source=scripts/lib/check-docs-checks.sh
source "$SCRIPT_DIR/lib/check-docs-checks.sh"
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
    # Built once, shared by check D and check G -- see build_headings_file's own comment on why.
    build_headings_file "$workdir/pairs_all.txt" "$workdir/headings.txt"

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

    d_out="$(check_d_violations "$workdir/pairs_all.txt" "$workdir/headings.txt")"
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

    g_out="$(check_g_violations "$workdir/pairs_all.txt" "$workdir/slugs.txt" "$workdir/headings.txt")"
    if [ -n "$g_out" ]; then
        while IFS= read -r line; do
            [ -n "$line" ] || continue
            echo "::error::check-docs (bare ref): $line"
            errors=$((errors + 1))
        done <<<"$g_out"
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
        echo "# for the full mechanism, and docs/development/docs-guard.md for why only naming is grandfathered -- every"
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
    build_headings_file "$workdir/pairs_all.txt" "$workdir/headings.txt"

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
    check_d_violations "$workdir/pairs_all.txt" "$workdir/headings.txt"
    echo
    echo "=== docs/README.md map completeness (check E) ==="
    check_e_violations "$workdir/scanned.txt"
    echo
    echo "=== anchor mentions outside markdown link syntax (check F) ==="
    check_f_violations "$workdir/pairs_all.txt" "$workdir/slugs.txt"
    echo
    echo "=== bare basename references (check G) ==="
    check_g_violations "$workdir/pairs_all.txt" "$workdir/slugs.txt" "$workdir/headings.txt"
}

case "$MODE" in
    check) run_check ;;
    update) run_update ;;
    list) run_list ;;
esac
