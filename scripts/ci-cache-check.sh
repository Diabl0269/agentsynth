#!/usr/bin/env bash
#
# ci-cache-check.sh — assert that CI's build caches are actually working.
#
# WHY: between the repo's first ccache commit (Jul 2026) and Aug 2026 the CI caches never once
# restored, on any platform, and nobody noticed — the workflow logged "Cache not found for input
# keys: ..." and carried on to a full cold build. Three independent faults hid behind that silence:
#   1. ci.yml only ran on `pull_request`, so it never wrote a cache into the base branch's scope
#      and no PR could ever restore one (GitHub scopes caches per ref; PRs read their own ref and
#      the base branch, nothing else).
#   2. The build/_deps cache key hashed all of CMakeLists.txt, so adding a module minted a fresh
#      ~350 MB entry per platform per workflow and blew the repo past GitHub's 10 GB budget,
#      LRU-evicting everything else.
#   3. The Linux job cached ~/.ccache while ccache 4.x on Ubuntu 24.04 writes to ~/.cache/ccache,
#      so the Linux ccache was never even saved.
# A silent cache failure looks exactly like a healthy build, just slower — hence this check.
#
# Reads its inputs from the environment so it is runnable (and testable) outside CI:
#   CACHE_MATCHED_KEY    restore result for the ccache entry   (empty => nothing restored)
#   DEPS_MATCHED_KEY     restore result for the build/_deps entry (empty => nothing restored)
#   CACHE_WARM_EXPECTED  "true" when a warm cache should already exist (PR runs). On the
#                        push-to-main run that *creates* the warm cache, set "false".
#   CCACHE_STATS_FILE    optional: read `ccache --show-stats` output from this file instead of
#                        invoking ccache (used by the unit tests)
#   CACHE_MIN_HIT_RATE   integer percent floor for the ccache hit rate (default 25)
#   CACHE_CHECK_ENFORCE  "true" (default) => exit 1 on a hard failure; "false" => annotate only
#
# Hard failure (exit 1) means "a cache that should have restored did not" — the actionable,
# unambiguous signal. A hit rate below the floor is only ever a warning: it drops legitimately
# whenever a PR touches a widely-included header, and failing on that would train people to
# ignore this check.

set -euo pipefail

CACHE_MATCHED_KEY="${CACHE_MATCHED_KEY:-}"
DEPS_MATCHED_KEY="${DEPS_MATCHED_KEY:-}"
CACHE_WARM_EXPECTED="${CACHE_WARM_EXPECTED:-true}"
CACHE_MIN_HIT_RATE="${CACHE_MIN_HIT_RATE:-25}"
CACHE_CHECK_ENFORCE="${CACHE_CHECK_ENFORCE:-true}"
CCACHE_STATS_FILE="${CCACHE_STATS_FILE:-}"

failures=0
warnings=0

# GitHub Actions workflow commands degrade to plain text when run outside CI.
annotate() { # annotate <error|warning|notice> <message>
    printf '::%s::%s\n' "$1" "$2"
}

summary() { # append a line to the job summary, when running under Actions
    [ -n "${GITHUB_STEP_SUMMARY:-}" ] && printf '%s\n' "$1" >>"$GITHUB_STEP_SUMMARY"
    return 0
}

# --- collect ccache statistics ------------------------------------------------------------
stats=""
if [ -n "$CCACHE_STATS_FILE" ]; then
    stats="$(cat "$CCACHE_STATS_FILE")"
elif command -v ccache >/dev/null 2>&1; then
    stats="$(ccache --show-stats 2>/dev/null || true)"
fi

# ccache reports hits/misses in two formats depending on version:
#   4.7+   "  Hits:             41 / 263 (15.59%)" and "  Misses:          222 / 263"
#   <=4.6  "cache hit (direct) 18" / "cache hit (preprocessed) 23" / "cache miss 222"
# Parse whichever is present; both runner images and Homebrew ccache are in play here.
parse_stats() {
    printf '%s\n' "$stats" | awk '
        /^[[:space:]]*Hits:/       && hits   == ""  { gsub(/[^0-9 ]/, " "); hits   = $1 }
        /^[[:space:]]*Misses:/     && misses == ""  { gsub(/[^0-9 ]/, " "); misses = $1 }
        /^cache hit \(direct\)/                     { direct = $NF }
        /^cache hit \(preprocessed\)/               { pre    = $NF }
        /^cache miss/                               { legacy_miss = $NF }
        END {
            if (hits == "" && (direct != "" || pre != "")) hits = direct + pre
            if (misses == "" && legacy_miss != "")         misses = legacy_miss
            if (hits == "")   hits = 0
            if (misses == "") misses = 0
            print hits, misses
        }'
}

read -r hits misses <<EOF
$(parse_stats)
EOF
total=$((hits + misses))

hit_rate=0
if [ "$total" -gt 0 ]; then
    hit_rate=$((hits * 100 / total))
fi

# --- report -------------------------------------------------------------------------------
deps_state="restored (${DEPS_MATCHED_KEY})"
[ -z "$DEPS_MATCHED_KEY" ] && deps_state="MISS — nothing restored"
ccache_state="restored (${CACHE_MATCHED_KEY})"
[ -z "$CACHE_MATCHED_KEY" ] && ccache_state="MISS — nothing restored"

summary "### Build cache health"
summary ""
summary "| Check | Result |"
summary "| --- | --- |"
summary "| \`build/_deps\` cache | ${deps_state} |"
summary "| \`ccache\` cache | ${ccache_state} |"
summary "| ccache hit rate | ${hit_rate}% (${hits} hits / ${total} compiles) |"
summary "| warm cache expected | ${CACHE_WARM_EXPECTED} |"

printf 'build/_deps cache : %s\n' "$deps_state"
printf 'ccache cache      : %s\n' "$ccache_state"
printf 'ccache hit rate   : %s%% (%s hits / %s compiles)\n' "$hit_rate" "$hits" "$total"

# --- verdict ------------------------------------------------------------------------------
if [ "$CACHE_WARM_EXPECTED" = "true" ]; then
    if [ -z "$DEPS_MATCHED_KEY" ]; then
        annotate error "build/_deps cache did not restore. Every dependency is being re-fetched \
and rebuilt from scratch. Check that the push-to-main run seeded a cache for this runner OS and \
that the repo is under GitHub's 10 GB cache limit (gh api repos/:owner/:repo/actions/caches)."
        failures=$((failures + 1))
    fi
    if [ -z "$CACHE_MATCHED_KEY" ]; then
        annotate error "ccache cache did not restore — this build compiled every translation unit \
from cold. Verify CCACHE_DIR matches the actions/cache path for this OS, and that a \
push-to-main run has seeded the cache."
        failures=$((failures + 1))
    fi
elif [ -z "$DEPS_MATCHED_KEY" ] || [ -z "$CACHE_MATCHED_KEY" ]; then
    annotate notice "Cache miss on a cache-seeding run — expected; this run populates the cache \
that pull requests will restore from."
fi

if [ "$total" -gt 0 ] && [ "$hit_rate" -lt "$CACHE_MIN_HIT_RATE" ]; then
    annotate warning "ccache hit rate ${hit_rate}% is below the ${CACHE_MIN_HIT_RATE}% floor. \
Expected after a dependency bump or a change to a widely-included header; investigate if it \
persists across unrelated pull requests."
    warnings=$((warnings + 1))
fi

if [ "$total" -eq 0 ]; then
    annotate warning "No ccache statistics available — ccache is not on PATH, or the compiler \
launcher is not wired up. The build is not being cached at all."
    warnings=$((warnings + 1))
fi

if [ "$failures" -gt 0 ]; then
    summary ""
    summary "**${failures} cache check(s) failed** — builds are running cold."
    if [ "$CACHE_CHECK_ENFORCE" = "true" ]; then
        exit 1
    fi
    annotate notice "CACHE_CHECK_ENFORCE is not 'true' — reporting only, not failing the job."
fi

exit 0
