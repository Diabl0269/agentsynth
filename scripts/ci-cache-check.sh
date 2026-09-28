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
#   DEPS_KEY_CHANGED     "true" when THIS run changed the deps cache key prefix itself (e.g.
#                        deps3 -> deps4, FRO341's JUCE-shallow bump) — a brand-new prefix has
#                        nothing saved under it anywhere yet, so a miss is expected for the
#                        build/_deps cache specifically, even on a same-repo PR run where
#                        CACHE_WARM_EXPECTED is otherwise "true". Does NOT relax the ccache
#                        check, since ccache's key prefix is unaffected by a deps bump. If unset,
#                        computed automatically: when $GITHUB_BASE_REF is set (a same-repo PR),
#                        fetch that ref and diff .github/workflows/ci.yml against it for an added
#                        or removed "-deps<N>-" key-prefix line; a fetch failure leaves it
#                        "false" (fail toward the existing error, never toward silently swallowing
#                        a real cold-cache regression). Outside CI (no GITHUB_BASE_REF) this is
#                        always "false", so ci-local.sh's behaviour is unaffected.
#   CCACHE_STATS_FILE    optional: read `ccache --show-stats` output from this file instead of
#                        invoking ccache (used by the unit tests)
#   CACHE_MIN_HIT_RATE   integer percent floor for the ccache hit rate (default 25)
#   CACHE_SATURATION_WARN_PCT  warn when ccache's own reported cache size is at or above this
#                        percent of its configured max_size (default 90) — visible proof a cap is
#                        too small, since growth against a full cap shows up as misses, not extra
#                        bytes, and nothing else surfaces that.
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
DEPS_KEY_CHANGED="${DEPS_KEY_CHANGED:-}"
CACHE_MIN_HIT_RATE="${CACHE_MIN_HIT_RATE:-25}"
CACHE_SATURATION_WARN_PCT="${CACHE_SATURATION_WARN_PCT:-90}"
CACHE_CHECK_ENFORCE="${CACHE_CHECK_ENFORCE:-true}"
CCACHE_STATS_FILE="${CCACHE_STATS_FILE:-}"
BUILD_NINJA="${BUILD_NINJA:-build/build.ninja}"

failures=0
warnings=0

# --- deps cache key generation: was it bumped in THIS run? ---------------------------------
# Only computed when the caller didn't already say (the unit tests always say). $GITHUB_BASE_REF
# is set by Actions on a same-repo pull_request run and nowhere else, so this is a no-op for
# push/workflow_dispatch runs (already exempt via CACHE_WARM_EXPECTED) and for ci-local.sh.
if [ -z "$DEPS_KEY_CHANGED" ]; then
    DEPS_KEY_CHANGED=false
    if [ -n "${GITHUB_BASE_REF:-}" ] && command -v git >/dev/null 2>&1 &&
        git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        if git fetch -q --depth=1 origin "$GITHUB_BASE_REF" 2>/dev/null &&
            git diff -U0 FETCH_HEAD HEAD -- .github/workflows/ci.yml 2>/dev/null |
                grep -qE '^[-+][^-+].*-deps[0-9]+-'; then
            DEPS_KEY_CHANGED=true
        fi
        # A fetch/diff failure leaves DEPS_KEY_CHANGED=false — a real cold-cache regression must
        # still fail loudly, never get silently reclassified as "expected" because this best-effort
        # detector itself couldn't run.
    fi
fi

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

# ccache 4.7+ also reports how full the cache is against its configured max_size, e.g.
#   Local storage:
#     Cache size (GB): 1.9 / 2.0 (95.05%)
# max_size is set per job (see ci.yml / build-artifacts.yml's "Configure ccache" steps) from what
# a warm build measures, but nothing else ever re-checks that the measurement is still right — a
# cap that has become too small doesn't show up as extra bytes (ccache evicts to stay under it),
# it shows up as extra MISSES, indistinguishable from any other cause of a lower hit rate. This is
# the one signal that says "the cap itself", not "something else", so it is worth its own warning.
parse_cache_size_pct() {
    printf '%s\n' "$stats" | awk '
        /^[[:space:]]*Cache size/ {
            if (match($0, /\([0-9]+\.?[0-9]*%\)/)) {
                s = substr($0, RSTART + 1, RLENGTH - 2)
                gsub(/%/, "", s)
                print s
                exit
            }
        }'
}

read -r hits misses <<EOF
$(parse_stats)
EOF
total=$((hits + misses))
cache_size_pct="$(parse_cache_size_pct)"

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
if [ -n "$cache_size_pct" ]; then
    summary "| ccache size vs max_size | ${cache_size_pct}% full |"
fi
summary "| warm cache expected | ${CACHE_WARM_EXPECTED} |"
summary "| deps cache key changed this run | ${DEPS_KEY_CHANGED} |"

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

# deps and ccache are checked independently (rather than as one combined verdict) so that
# DEPS_KEY_CHANGED can relax the deps half alone: bumping the deps cache key's prefix (e.g.
# deps3 -> deps4, FRO341's JUCE-shallow change) guarantees a miss for every job in the PR that
# does it, since nothing has ever been saved under the new prefix — that is not a regression in
# the ccache plumbing, which is unaffected and must still be held to the normal standard.
deps_warm_expected="$CACHE_WARM_EXPECTED"
[ "$DEPS_KEY_CHANGED" = "true" ] && deps_warm_expected=false

if [ "$deps_warm_expected" = "true" ] && [ "$inputs_suspect" -eq 0 ]; then
    if [ -z "$DEPS_MATCHED_KEY" ]; then
        annotate error "build/_deps cache did not restore. Every dependency is being re-fetched \
and rebuilt from scratch. PR runs restore only now — only a push to main (or a manual \
workflow_dispatch run of this workflow on main) seeds or re-seeds this cache, so check that one \
has run and saved an entry for this runner OS, and that the repo is under GitHub's 10 GB cache \
limit (gh api repos/:owner/:repo/actions/caches)."
        failures=$((failures + 1))
    fi
elif [ "$deps_warm_expected" != "true" ] && [ -z "$DEPS_MATCHED_KEY" ]; then
    if [ "$DEPS_KEY_CHANGED" = "true" ]; then
        annotate notice "build/_deps cache miss, but this run changed the deps cache key prefix \
in .github/workflows/ci.yml (e.g. deps3 -> deps4) — nothing has ever been saved under the new \
prefix yet, so a miss here is expected for this PR, not a regression. Every PR after this one \
merges will restore normally. Not a defect."
    else
        # Covers both runs that legitimately start cold: the push-to-main run that seeds the
        # cache, and a pull request from a fork, which GitHub gives an isolated cache scope with
        # no access to the base repository's entries.
        annotate notice "build/_deps cache miss on a run not expected to have a warm cache — \
either the push-to-main run that seeds the cache pull requests restore from, or a fork pull \
request (forks cannot read base-repository caches). Not a defect."
    fi
fi

if [ "$CACHE_WARM_EXPECTED" = "true" ] && [ "$inputs_suspect" -eq 0 ]; then
    if [ -z "$CACHE_MATCHED_KEY" ]; then
        annotate error "ccache cache did not restore — this build compiled every translation unit \
from cold. Verify CCACHE_DIR matches the actions/cache path for this OS. PR runs restore only \
now — only a push to main (or a manual workflow_dispatch run of this workflow on main) seeds or \
re-seeds this cache."
        failures=$((failures + 1))
    fi
elif [ "$CACHE_WARM_EXPECTED" != "true" ] && [ -z "$CACHE_MATCHED_KEY" ]; then
    # Covers both runs that legitimately start cold: the push-to-main run that seeds the cache,
    # and a pull request from a fork, which GitHub gives an isolated cache scope with no access to
    # the base repository's entries.
    annotate notice "ccache cache miss on a run not expected to have a warm cache — either the \
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

# Saturation: never fails the build (a too-small cap is a slower build, not a broken one), but
# must be visible somewhere, because it looks identical to any other cause of a low hit rate from
# inside a single job's own logs.
if [ -n "$cache_size_pct" ] &&
    awk -v pct="$cache_size_pct" -v floor="$CACHE_SATURATION_WARN_PCT" \
        'BEGIN { exit !(pct >= floor) }'; then
    annotate warning "ccache is ${cache_size_pct}% full against its configured max_size. Growth \
against a saturated cap shows up as extra MISSES, not extra bytes, so a cap that has become too \
small never shows up any other way. Consider raising this job's max_size (and re-checking \
scripts/ci-cache-budget.sh's total afterward, since every configured byte counts toward the \
repo's cache budget)."
    warnings=$((warnings + 1))
fi

# --- compiler-launcher audit ----------------------------------------------------------------
# The hit rate cannot see this class of fault. CMAKE_<LANG>_COMPILER_LAUNCHER is per language, and
# a language left unwired doesn't lower the hit rate — its compiles never reach ccache at all, so
# they are counted as neither hit nor miss. That is how 103 Objective-C++ units (JUCE ships every
# module as one .mm unity file, compiled once per target) were rebuilt cold on every macOS run for
# weeks behind a merely-mediocre-looking 54%.
#
# So audit the generator's own output: every compile of a language we cache must invoke ccache.
# That is generator output, not our source, so it catches the fault however it arrives — a new
# language, a new target kind, a dropped -D flag, a CMake upgrade. RC and the C++20 module-scan
# rules are deliberately out of scope (ccache does not handle them); links are not compiles.
#
# HOW THE LAUNCHER ACTUALLY LANDS IN NINJA OUTPUT, because two wrong guesses have already cost a
# CI round trip each. CMake splits it across the two files:
#
#   CMakeFiles/rules.ninja   rule OBJCXX_COMPILER__Core_unscanned_
#                              command = ${LAUNCHER}${CODE_CHECK}/usr/bin/c++ ... -c $in
#   build.ninja              build CMakeFiles/Core.dir/juce_core.mm.o: OBJCXX_COMPILER__Core_...
#                              LAUNCHER = /opt/homebrew/bin/ccache
#
# The rule command NEVER contains the word ccache — it contains the placeholder — and the real
# path is a per-build-statement variable. So the unit of audit is the build STATEMENT, not the
# rule: one per translation unit, which is also the number worth reporting (103 .mm files, not
# the ~10 ObjC++ rules they share). Older CMake inlined the launcher into the rule command
# instead, so a rule whose command does name ccache still counts as wired.
launcher_rules="$(dirname "$BUILD_NINJA")/CMakeFiles/rules.ninja"
launcher_sources=""
[ -f "$launcher_rules" ] && launcher_sources="$launcher_rules"
[ -f "$BUILD_NINJA" ] && launcher_sources="$launcher_sources $BUILD_NINJA"

launcher_report=""
if [ -n "$launcher_sources" ]; then
    # rules.ninja first: a statement's verdict may depend on its rule, defined in the other file.
    # shellcheck disable=SC2086  # deliberate word splitting: one or two paths, both ours
    launcher_report="$(cat $launcher_sources | awk '
        # Close the statement being read and record its verdict.
        function flush() {
            if (lang != "") {
                seen[lang]++
                if (!launcher_ok && !(stmt_rule in rule_has_ccache)) {
                    bad[lang]++
                    if (!(lang in example)) example[lang] = stmt_out
                }
            }
            lang = ""; launcher_ok = 0; stmt_rule = ""; stmt_out = ""
        }

        # --- rule definitions (any language: we only need the ccache verdict per rule name) ---
        /^rule /                          { rule_name = $2; in_rule = 1; next }
        in_rule && /^[ \t]*command[ \t]*=/ {
            if ($0 ~ /[Cc]cache/) rule_has_ccache[rule_name] = 1
            in_rule = 0; next
        }

        # --- build statements ---
        /^build / {
            flush()
            if (match($0, /: (C|CXX|OBJC|OBJCXX)_COMPILER__[^ ]*/)) {
                stmt_rule = substr($0, RSTART + 2, RLENGTH - 2)
                split(stmt_rule, parts, "_COMPILER__")
                lang = parts[1]
                stmt_out = $2; sub(/:$/, "", stmt_out)
            }
            next
        }
        lang != "" && /^[ \t]+LAUNCHER[ \t]*=/ { if ($0 ~ /[Cc]cache/) launcher_ok = 1; next }
        /^[^ \t]/ { flush() }   # any unindented line ends the statement

        END {
            flush()
            for (l in seen) printf "%s %d %d %s\n", l, (l in bad ? bad[l] : 0), seen[l], example[l]
        }' | sort)"
fi

if [ -n "$launcher_report" ]; then
    while read -r lang bad_count rule_count example_rule; do
        [ -z "$lang" ] && continue
        if [ "$bad_count" -gt 0 ]; then
            annotate error "${bad_count} of ${rule_count} ${lang} compiles do not go through ccache \
(e.g. ${example_rule}). Those translation units are rebuilt from cold on every run and are \
invisible in the hit rate, because a compile that never reaches ccache is counted as neither hit \
nor miss. Set CMAKE_${lang}_COMPILER_LAUNCHER (CMake's launcher is per language — C and CXX do not \
cover OBJC/OBJCXX)."
            failures=$((failures + 1))
        fi
        printf '%-6s compiles       : %s/%s via ccache\n' \
            "$lang" "$((rule_count - bad_count))" "$rule_count"
    done <<EOF
$launcher_report
EOF
elif [ -n "$launcher_sources" ]; then
    # The files are there but no compile rule matched, so the audit proved nothing. Say so loudly
    # rather than printing a reassuring "skipped": a parser that silently matches nothing is the
    # same silent-success failure mode this whole script exists to catch. This is not theoretical —
    # it is how the first version of this audit failed (it read only build.ninja, while CMake emits
    # its rules into CMakeFiles/rules.ninja), and this warning is what surfaced it.
    annotate warning "Launcher audit recognised no C/CXX/OBJC/OBJCXX compiles in \
${launcher_sources}. The audit is not checking anything — update its build-statement pattern in \
scripts/ci-cache-check.sh."
    warnings=$((warnings + 1))
    printf 'launcher audit    : NOTHING MATCHED in %s\n' "$launcher_sources"
else
    printf 'launcher audit    : skipped (no ninja files found at %s)\n' "$BUILD_NINJA"
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
