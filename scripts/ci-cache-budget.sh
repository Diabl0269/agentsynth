#!/usr/bin/env bash
#
# ci-cache-budget.sh -- static budget check for the Actions caches ci.yml and build-artifacts.yml
# commit to writing, for ONE cache generation (FRO341).
#
# WHY: docs/development/ci-caching.md's rule 6 (and its FRO341 amendment) keeps the repo down to
# a single cache generation per OS/job family instead of relying on GitHub's own eviction, but that
# alone doesn't bound the TOTAL — nothing stopped a future `max_size` bump (or a new job) from
# quietly pushing one generation itself past the repo's cache budget again, the way two generations
# did on 2026-09-28 (10.4 GB, evicted a `deps3` entry, failed an unrelated PR's
# scripts/ci-cache-check.sh). A ccache cap alone can't show this from inside a single job either:
# every entry runs near-full by design (see the saturation warning in ci-cache-check.sh), so growth
# past a cap shows up locally as misses, never as bytes -- nothing in any one job's own logs adds
# up the REPO-WIDE total the caps commit to. This script does that addition at review time, before
# a cap change merges, instead of after an eviction.
#
# What it sums, for one generation:
#   - every literal `ccache --set-config=max_size=<N><M|G>` in ci.yml, EXCEPT the label-gated ASAN
#     job's: that job never runs on push:main, so nothing seeds a main-scoped cache for it and its
#     cache is not part of "one generation on main" -- it is a PR-scoped, self-contained family
#     (pruned to one generation per PR, deleted outright when the PR closes) that the runtime usage
#     alarm (scripts/ci-cache-check.sh's caller in the workflows, >8.5 GB actual usage) covers
#     instead of a static sum.
#   - every literal `ccache_max_size: <N><M|G>` in build-artifacts.yml's release build matrix.
#   - a fixed DEPS_ALLOWANCE_MB for the build/_deps entries in both workflows (ci.yml's deps6,
#     build-artifacts.yml's release-deps-ninja6, one entry per OS each) -- NOT capped by ccache, so
#     it can't be read off a config value; see the constant's own comment below for how it's
#     derived.
#
# `G` is treated as 1024 (binary), matching ccache's own suffix semantics -- see `ccache --help`.
#
# Usage:
#   bash scripts/ci-cache-budget.sh              # check the real repo's workflows
#   bash scripts/ci-cache-budget.sh --list       # print every parsed contributor and the total
#   bash scripts/ci-cache-budget.sh --root <dir> # scan workflows under a different repo root
#                                                 # (scripts/tests/ci-cache-budget.test.sh's fixtures)
#
# Environment:
#   CI_YML                path to ci.yml (default <root>/.github/workflows/ci.yml)
#   BUILD_ARTIFACTS_YML    path to build-artifacts.yml (default <root>/.github/workflows/build-artifacts.yml)
#   EXCLUDED_CI_JOBS       space-separated ci.yml job ids to exclude from the ci.yml sum
#                          (default: build-and-test-asan)
#   DEPS_ALLOWANCE_MB      see the constant below
#   BUDGET_CAP_MB          fail if the total exceeds this many MB (default 8192 = 8 GiB)
#   MIN_CI_MAX_SIZE_LINES  fail (parser looks broken) if fewer than this many non-excluded
#                          max_size lines are found in ci.yml (default 3)
#   MIN_BA_MAX_SIZE_LINES  fail (parser looks broken) if fewer than this many ccache_max_size
#                          lines are found in build-artifacts.yml (default 3)
#
# Exit status: 0 total <= BUDGET_CAP_MB (or --list/--help); 1 over budget, or the parser found
# fewer contributors than expected -- silently matching nothing and passing is the one failure mode
# this script exists to avoid, the same "fails safe" stance as ci-cache-check.sh's launcher audit.

set -euo pipefail
export LC_ALL=C

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIST=0

while [ $# -gt 0 ]; do
    case "$1" in
        --list) LIST=1; shift ;;
        --root) ROOT="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,/^set -euo/p' "$0" | sed '$d'
            exit 0
            ;;
        *) echo "ci-cache-budget: unrecognised argument: $1" >&2; exit 1 ;;
    esac
done

CI_YML="${CI_YML:-$ROOT/.github/workflows/ci.yml}"
BUILD_ARTIFACTS_YML="${BUILD_ARTIFACTS_YML:-$ROOT/.github/workflows/build-artifacts.yml}"
EXCLUDED_CI_JOBS="${EXCLUDED_CI_JOBS:-build-and-test-asan}"
# Measured from the Actions cache listing on 2026-10-30: ci.yml's deps6 entries 192 + 185 + 142 MB,
# build-artifacts.yml's release-deps-ninja6 entries 188 + 182 + 139 MB, 1028 MB for one generation.
# The allowance leaves about a quarter of margin for a dependency that grows.
DEPS_ALLOWANCE_MB="${DEPS_ALLOWANCE_MB:-1280}"
BUDGET_CAP_MB="${BUDGET_CAP_MB:-8192}"
MIN_CI_MAX_SIZE_LINES="${MIN_CI_MAX_SIZE_LINES:-3}"
MIN_BA_MAX_SIZE_LINES="${MIN_BA_MAX_SIZE_LINES:-3}"

to_mb() { # to_mb <value like 512M|2G> -- prints the size in MB, or exits 1 on an unknown suffix
    local v="$1" num unit
    unit="${v: -1}"
    num="${v%[MG]}"
    case "$unit" in
        G) echo $((num * 1024)) ;;
        M) echo "$num" ;;
        *)
            echo "ci-cache-budget: unrecognised size unit in '$v' (expected an M or G suffix)" >&2
            exit 1
            ;;
    esac
}

is_excluded() { # is_excluded <job-id>
    local job="$1" x
    for x in $EXCLUDED_CI_JOBS; do
        [ "$x" = "$job" ] && return 0
    done
    return 1
}

[ -f "$CI_YML" ] || { echo "ci-cache-budget: $CI_YML not found" >&2; exit 1; }
[ -f "$BUILD_ARTIFACTS_YML" ] || { echo "ci-cache-budget: $BUILD_ARTIFACTS_YML not found" >&2; exit 1; }

# --- ci.yml: literal `max_size=<N><M|G>`, tagged with the enclosing job id --------------------
# Job ids are the top-level (2-space-indented) keys under `jobs:` -- unambiguous only from that
# point on, since `on:` above it has its own 2-space-indented keys (`pull_request:`, `push:`) that
# are not job ids.
ci_matches="$(awk '
    /^jobs:[[:space:]]*$/ { in_jobs = 1; next }
    in_jobs && /^  [A-Za-z][A-Za-z0-9_-]*:[[:space:]]*$/ {
        job = $1
        sub(/:$/, "", job)
        next
    }
    in_jobs && /ccache --set-config=max_size=[0-9]/ {
        line = $0
        sub(/.*max_size=/, "", line)
        gsub(/[[:space:]].*/, "", line)
        print job, line
    }
' "$CI_YML")"

# --- build-artifacts.yml: literal `ccache_max_size: <N><M|G>` in the release matrix -----------
ba_matches="$(awk '
    /ccache_max_size:[[:space:]]*[0-9]/ {
        line = $0
        sub(/.*ccache_max_size:[[:space:]]*/, "", line)
        gsub(/[[:space:]].*/, "", line)
        print line
    }
' "$BUILD_ARTIFACTS_YML")"

total_mb=0
declare -a contributors=()
ci_count=0
ba_count=0

if [ -n "$ci_matches" ]; then
    while read -r job size; do
        [ -z "$size" ] && continue
        if is_excluded "$job"; then
            contributors+=("ci.yml:${job} max_size=${size} -> excluded (PR-scoped, covered by the runtime usage alarm)")
            continue
        fi
        mb="$(to_mb "$size")"
        total_mb=$((total_mb + mb))
        ci_count=$((ci_count + 1))
        contributors+=("ci.yml:${job} max_size=${size} -> ${mb} MB")
    done <<<"$ci_matches"
fi

if [ -n "$ba_matches" ]; then
    while read -r size; do
        [ -z "$size" ] && continue
        mb="$(to_mb "$size")"
        total_mb=$((total_mb + mb))
        ba_count=$((ba_count + 1))
        contributors+=("build-artifacts.yml ccache_max_size=${size} -> ${mb} MB")
    done <<<"$ba_matches"
fi

total_mb=$((total_mb + DEPS_ALLOWANCE_MB))
contributors+=("deps allowance (ci.yml deps6 + build-artifacts.yml release-deps-ninja6, all OSes) -> ${DEPS_ALLOWANCE_MB} MB")

if [ "$LIST" -eq 1 ]; then
    printf '%s\n' "${contributors[@]}"
    printf '\ntotal: %s MB (budget %s MB)\n' "$total_mb" "$BUDGET_CAP_MB"
    exit 0
fi

# Fails safe: a parser that silently matches fewer lines than the workflows are known to contain
# would let a real cap grow unnoticed behind a falsely-reassuring "under budget" -- the same
# blind spot ci-cache-check.sh's launcher audit warns about when IT matches nothing.
if [ "$ci_count" -lt "$MIN_CI_MAX_SIZE_LINES" ]; then
    echo "ci-cache-budget: only found ${ci_count} non-excluded max_size line(s) in $CI_YML," \
        "expected at least ${MIN_CI_MAX_SIZE_LINES} -- the parser may be broken, not the budget." >&2
    exit 1
fi
if [ "$ba_count" -lt "$MIN_BA_MAX_SIZE_LINES" ]; then
    echo "ci-cache-budget: only found ${ba_count} ccache_max_size line(s) in $BUILD_ARTIFACTS_YML," \
        "expected at least ${MIN_BA_MAX_SIZE_LINES} -- the parser may be broken, not the budget." >&2
    exit 1
fi

if [ "$total_mb" -gt "$BUDGET_CAP_MB" ]; then
    printf 'ci-cache-budget: one cache generation would total %s MB, over the %s MB budget:\n' \
        "$total_mb" "$BUDGET_CAP_MB" >&2
    printf '  %s\n' "${contributors[@]}" >&2
    exit 1
fi

printf 'ci-cache-budget: one cache generation totals %s MB, within the %s MB budget.\n' \
    "$total_mb" "$BUDGET_CAP_MB"
exit 0
