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
#   4. (Aug 2026) Only CMAKE_C/CXX_COMPILER_LAUNCHER were set, and CMake treats Objective-C++ as
#      its own language, so all 103 JUCE .mm translation units bypassed ccache and were rebuilt
#      cold every run — ~12 min of the macOS job. This one was invisible even to the hit-rate
#      check below: a compile that never reaches ccache is not counted as a miss, so the rate read
#      54%, low but plausible. Hence the build.ninja launcher audit further down.
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
#   BUILD_NINJA          path to the generated build.ninja for the launcher audit
#                        (default build/build.ninja; skipped when the file is absent)
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
BUILD_NINJA="${BUILD_NINJA:-build/build.ninja}"

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
# Cross-validation, so this script fails SAFE. An empty matched-key alongside a high ccache hit
# rate is self-contradictory: a genuinely cold build cannot hit ~100%, because on a cold cache the
# only hits are the handful of files compiled into two targets within the same run (~15% here).
# That combination means this script's own inputs are mis-wired, not that the cache is cold — and
# it has happened: the workflow first read `cache-matched-key` off the combined `actions/cache`
# action, which declares only `cache-hit`, so the value was always empty and every run was
# reported as MISS even at a 100% hit rate. Enforcing on that would have failed every build. So
# when the evidence disagrees, warn about the check instead of failing the build.
#
# Keyed on the CCACHE contradiction specifically: a high hit rate proves the *ccache* restored, so
# an empty CACHE_MATCHED_KEY alongside it can only mean the key never reached this script. Both
# keys arrive through the same plumbing, so that one signal condemns DEPS_MATCHED_KEY too, and
# both errors are suppressed. A high hit rate on its own proves nothing about build/_deps — when
# the ccache key IS populated (plumbing demonstrably fine) and only the deps key is empty, that is
# a real dependency-cache miss and still fails.
inputs_suspect=0
if [ "$total" -gt 0 ] && [ "$hit_rate" -ge "${CACHE_SELFCHECK_HIT_RATE:-50}" ] &&
    [ -z "$CACHE_MATCHED_KEY" ]; then
    inputs_suspect=1
    annotate warning "Cache reported as not restored, yet ccache hit ${hit_rate}% — a cold build \
cannot do that. Treating this as a misconfigured check rather than a cold cache: verify the \
workflow reads cache-matched-key from an actions/cache/restore step (the combined actions/cache \
action does not expose it). Not failing the build on this."
    warnings=$((warnings + 1))
fi

if [ "$CACHE_WARM_EXPECTED" = "true" ] && [ "$inputs_suspect" -eq 0 ]; then
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
elif [ "$CACHE_WARM_EXPECTED" != "true" ] &&
    { [ -z "$DEPS_MATCHED_KEY" ] || [ -z "$CACHE_MATCHED_KEY" ]; }; then
    # Covers both runs that legitimately start cold: the push-to-main run that seeds the cache,
    # and a pull request from a fork, which GitHub gives an isolated cache scope with no access to
    # the base repository's entries.
    annotate notice "Cache miss on a run not expected to have a warm cache — either the \
push-to-main run that seeds the cache pull requests restore from, or a fork pull request (forks \
cannot read base-repository caches). Not a defect."
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

# --- compiler-launcher audit ----------------------------------------------------------------
# The hit rate cannot see this class of fault. CMAKE_<LANG>_COMPILER_LAUNCHER is per language, and
# a language left unwired doesn't lower the hit rate — its compiles never reach ccache at all, so
# they are counted as neither hit nor miss. That is how 103 Objective-C++ units (JUCE ships every
# module as one .mm unity file, compiled once per target) were rebuilt cold on every macOS run for
# weeks behind a merely-mediocre-looking 54%.
#
# So audit the generated ninja file directly: every compile rule for a language we cache must
# invoke ccache. This is generator output, not our source, so it catches the fault however it
# arrives — a new language, a new target kind, a dropped -D flag, a CMake upgrade. RC and the
# C++20 module-scan rules are deliberately out of scope (ccache does not handle them); links are
# not compiles.
launcher_report=""
if [ -f "$BUILD_NINJA" ]; then
    launcher_report="$(awk '
        /^rule (C|CXX|OBJC|OBJCXX)_COMPILER__/ {
            split($2, parts, "_COMPILER__"); lang = parts[1]; rule = $2; next
        }
        /^rule /                  { lang = ""; next }
        lang != "" && /^ *command *=/ {
            seen[lang]++
            if ($0 !~ /[Cc]cache/) { bad[lang]++; if (!(lang in example)) example[lang] = rule }
            lang = ""
        }
        END {
            for (l in seen) printf "%s %d %d %s\n", l, (l in bad ? bad[l] : 0), seen[l], example[l]
        }' "$BUILD_NINJA" | sort)"
fi

if [ -n "$launcher_report" ]; then
    while read -r lang bad_count rule_count example_rule; do
        [ -z "$lang" ] && continue
        if [ "$bad_count" -gt 0 ]; then
            annotate error "${bad_count} of ${rule_count} ${lang} compile rules do not go through \
ccache (e.g. ${example_rule}). Those translation units are rebuilt from cold on every run and are \
invisible in the hit rate, because a compile that never reaches ccache is counted as neither hit \
nor miss. Set CMAKE_${lang}_COMPILER_LAUNCHER (CMake's launcher is per language — C and CXX do not \
cover OBJC/OBJCXX)."
            failures=$((failures + 1))
        fi
        printf '%-6s compile rules  : %s/%s via ccache\n' \
            "$lang" "$((rule_count - bad_count))" "$rule_count"
    done <<EOF
$launcher_report
EOF
elif [ -f "$BUILD_NINJA" ]; then
    # The file is there but no compile rule matched, so the audit proved nothing. Say so loudly
    # rather than printing a reassuring "skipped": a parser that silently matches nothing is the
    # same silent-success failure mode this whole script exists to catch. Most likely cause is a
    # change in how CMake names its compile rules (this expects `rule <LANG>_COMPILER__<target>`).
    annotate warning "Launcher audit recognised no C/CXX/OBJC/OBJCXX compile rules in \
${BUILD_NINJA}. The audit is not checking anything — update its rule-name pattern in \
scripts/ci-cache-check.sh."
    warnings=$((warnings + 1))
    printf 'launcher audit    : NO RULES MATCHED in %s\n' "$BUILD_NINJA"
else
    printf 'launcher audit    : skipped (%s not found)\n' "$BUILD_NINJA"
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
