#!/usr/bin/env bash
#
# Unit tests for scripts/ci-cache-budget.sh (FRO341).
#
# The budget check is what catches a future cap bump pushing one cache generation back past the
# repo's Actions-cache budget, the way two generations did on 2026-09-28. If IT breaks silently --
# a job-boundary regex that stops matching, an excluded job whose exclusion swallows too much --
# a real overrun ships unnoticed again, so it gets tests of its own. Runs in the Lint job -- no
# compiler, no network, ~1s.
#
# Usage: bash scripts/tests/ci-cache-budget.test.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUDGET="$SCRIPT_DIR/scripts/ci-cache-budget.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0

run() { # run <name> <expected_exit> <expected_substring|-> [env assignments...]
    local name="$1" want_exit="$2" want_text="$3"
    shift 3
    local out status
    out="$(env -u CI_YML -u BUILD_ARTIFACTS_YML -u EXCLUDED_CI_JOBS -u DEPS_ALLOWANCE_MB \
               -u BUDGET_CAP_MB -u MIN_CI_MAX_SIZE_LINES -u MIN_BA_MAX_SIZE_LINES \
               "$@" bash "$BUDGET" $EXTRA_ARG 2>&1)"
    status=$?

    if [ "$status" -ne "$want_exit" ]; then
        printf 'FAIL  %s\n      expected exit %s, got %s\n%s\n' "$name" "$want_exit" "$status" "$out"
        fail=$((fail + 1))
        return
    fi
    if [ "$want_text" != "-" ] && ! printf '%s' "$out" | grep -qF "$want_text"; then
        printf 'FAIL  %s\n      expected output to contain: %s\n%s\n' "$name" "$want_text" "$out"
        fail=$((fail + 1))
        return
    fi
    printf 'ok    %s\n' "$name"
    pass=$((pass + 1))
}

EXTRA_ARG=""

# --- the real repo's own workflows must pass --------------------------------------------------
run "real workflows are within budget" 0 "within the 8192 MB budget"

EXTRA_ARG="--list"
run "--list prints every contributor and the total" 0 "deps allowance" \
    CI_YML="$SCRIPT_DIR/.github/workflows/ci.yml" \
    BUILD_ARTIFACTS_YML="$SCRIPT_DIR/.github/workflows/build-artifacts.yml"
EXTRA_ARG=""

# --- fixtures -----------------------------------------------------------------------------
# A minimal ci.yml shape: enough job structure for the awk job-boundary tracker to exercise, one
# excluded (ASAN-shaped) job and one not.
write_fixture_ci() { # write_fixture_ci <path> <build-and-test max_size> <asan max_size>
    cat >"$1" <<EOF
name: fixture
on:
  pull_request:
    branches: [ "main" ]
jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps:
    - name: Configure ccache
      run: |
        ccache --set-config=max_size=$2
  build-and-test-asan:
    runs-on: ubuntu-latest
    steps:
    - name: Configure ccache
      run: |
        ccache --set-config=max_size=$3
  build-and-test-macos:
    runs-on: macos-latest
    steps:
    - name: Configure ccache
      run: |
        ccache --set-config=max_size=512M
  build-and-test-windows:
    runs-on: windows-latest
    steps:
    - name: Configure ccache
      run: |
        ccache --set-config=max_size=512M
EOF
}

write_fixture_ba() { # write_fixture_ba <path> <matrix max_size, templated or literal>
    cat >"$1" <<EOF
name: fixture-release
jobs:
  build:
    strategy:
      matrix:
        include:
          - os: ubuntu-latest
            ccache_max_size: $2
          - os: macos-latest
            ccache_max_size: $2
          - os: windows-latest
            ccache_max_size: $2
    steps:
    - name: Configure ccache
      run: |
        ccache --set-config=max_size=\${{ matrix.ccache_max_size }}
EOF
}

write_fixture_ci "$WORK/ci-normal.yml" "2G" "1500M"
write_fixture_ba "$WORK/ba-normal.yml" "512M"

run "fixture within budget passes" 0 "within the" \
    CI_YML="$WORK/ci-normal.yml" BUILD_ARTIFACTS_YML="$WORK/ba-normal.yml"

# --- the fixture this test file exists to catch: a cap bump that blows the budget -------------
write_fixture_ci "$WORK/ci-huge.yml" "9G" "1500M"

run "fixture exceeding the budget fails" 1 "over the" \
    CI_YML="$WORK/ci-huge.yml" BUILD_ARTIFACTS_YML="$WORK/ba-normal.yml"

# --- the ASAN-shaped job is excluded even when its cap alone would blow the budget -------------
write_fixture_ci "$WORK/ci-huge-asan.yml" "512M" "9G"

run "a huge cap on the excluded (ASAN) job alone does not fail" 0 "within the" \
    CI_YML="$WORK/ci-huge-asan.yml" BUILD_ARTIFACTS_YML="$WORK/ba-normal.yml"

# --- a templated (non-literal) matrix value contributes nothing, not a false hit ---------------
write_fixture_ba "$WORK/ba-templated.yml" '${{ matrix.some_other_value }}'
# Reduce the excluded set to nothing so the fixture's ci.yml is normal-sized, then check the
# build-artifacts total alone by driving DEPS_ALLOWANCE_MB + BUDGET_CAP_MB tight around the
# expected ci.yml contribution (2G + 512M + 512M = 3072 MB) plus the deps allowance, so a
# templated line being silently counted as some non-zero MB would push the total over.
run "a non-literal ccache_max_size contributes zero, not a parse error" 0 "within the" \
    CI_YML="$WORK/ci-normal.yml" BUILD_ARTIFACTS_YML="$WORK/ba-templated.yml" \
    BUDGET_CAP_MB=6000 MIN_BA_MAX_SIZE_LINES=0

# --- fails-safe: a parser that suddenly matches nothing must not silently pass -----------------
cat >"$WORK/ci-empty.yml" <<'EOF'
name: fixture-empty
jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps:
    - name: Configure ccache
      run: echo "no max_size line here"
EOF

run "a ci.yml with no max_size lines fails loudly instead of passing" 1 "parser may be broken" \
    CI_YML="$WORK/ci-empty.yml" BUILD_ARTIFACTS_YML="$WORK/ba-normal.yml"

# --- the deps allowance is actually summed in ------------------------------------------------
run "the deps allowance alone can push a tight budget over" 1 "over the" \
    CI_YML="$WORK/ci-normal.yml" BUILD_ARTIFACTS_YML="$WORK/ba-normal.yml" \
    DEPS_ALLOWANCE_MB=100000

printf '\n%s passed, %s failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
