#!/usr/bin/env bash
#
# Unit tests for the result caches behind scripts/check-function-sizes.sh, check-file-sizes.sh,
# check-fixed-waits.sh, check-header-comments.sh and check-docs.sh (scripts/lib/guard-cache.sh).
#
# Each guard runs cold, then warm, then with ONE real change that must still fail it: a function made
# longer in a file the cache already holds, a changed baseline, a lowered limit, a deleted link
# target, a header's wrapper redefined, a sibling file deleted, an edited scanner. A stale pass is
# worse than a slow guard, so every case here is a way a cache could wrongly say "unchanged".
# Also: a cache hit really is read, a corrupt entry just means a rescan, and gates writing at once
# do not tear entries. Fixtures are throwaway git repos; no compiler, no network.
#
# Usage: bash scripts/tests/guard-cache.test.sh

set -uo pipefail

# Same reasoning as check-file-sizes.test.sh: strip any inherited git env so nothing here can ever
# touch a real repository.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR
unset GUARD_CACHE

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPTS="$SCRIPT_DIR/scripts"

TMPROOT="$(mktemp -d)"
trap 'rm -rf "${TMPROOT:?}"' EXIT
REPO="$TMPROOT/repo"
BASELINE="$TMPROOT/baseline.txt"
CACHE="$REPO/.git/agentsynth-guard-cache"

export FUNCTION_SIZE_CAP=5 FUNCTION_SIZE_BASELINE="$BASELINE"
export FILE_SIZE_CAP=10 FILE_SIZE_BASELINE="$BASELINE"
export HEADER_COMMENT_FLOOR=10 HEADER_COMMENT_BASELINE="$BASELINE"
export FIXED_WAITS_BASELINE="$BASELINE"
export DOCS_BASELINE="$BASELINE"

pass=0
fail=0
PUMP='juce::MessageManager::getInstance()->runDispatchLoopUntil'
D="docs"
S="/"

record() {
    if [ "$1" -eq 0 ]; then
        echo "PASS: $2"
        pass=$((pass + 1))
    else
        echo "FAIL: $2"
        fail=$((fail + 1))
    fi
}

reset_repo() {
    rm -rf "${REPO:?}"
    mkdir -p "$REPO"
    (cd "$REPO" && git init -q && git -c user.email=t@t -c user.name=t commit -qm init --allow-empty)
    : >"$BASELINE"
}

write_file() {
    mkdir -p "$(dirname "$REPO/$1")"
    cat >"$REPO/$1"
}

drop_file() {
    rm -f "${REPO:?}/$1"
}

drop_cache() {
    rm -rf "${CACHE:?}"
}

commit_all() {
    (cd "$REPO" && git add -A && git -c user.email=t@t -c user.name=t commit -qm fixture --allow-empty)
}

# make_function <path> <name> <total-lines> -- a free function that occupies exactly that many lines.
make_function() {
    local path="$REPO/$1" i
    mkdir -p "$(dirname "$path")"
    {
        printf 'void %s() {\n' "$2"
        i=2
        while [ "$i" -lt "$3" ]; do
            printf '    int x%d = %d;\n' "$i" "$i"
            i=$((i + 1))
        done
        printf '}\n'
    } >"$path"
}

# make_lines <path> <count> -- a file of <count> plain lines.
make_lines() {
    local path="$REPO/$1" i=1
    mkdir -p "$(dirname "$path")"
    : >"$path"
    while [ "$i" -le "$2" ]; do
        printf 'line %d\n' "$i" >>"$path"
        i=$((i + 1))
    done
}

make_header() {
    local path="$REPO/$1" i=1
    mkdir -p "$(dirname "$path")"
    : >"$path"
    while [ "$i" -le "$2" ]; do printf '// comment %d\n' "$i" >>"$path"; i=$((i + 1)); done
    i=1
    while [ "$i" -le "$3" ]; do printf 'int code_%d = %d;\n' "$i" "$i" >>"$path"; i=$((i + 1)); done
}

# run_guard <script> -- runs the guard from the scripts folder $GUARD_SCRIPTS over the fixture.
GUARD_SCRIPTS="$SCRIPTS"
run_guard() {
    bash "$GUARD_SCRIPTS/$1" --root "$REPO" 2>&1
}

# expect_pass <description> <script> -- commits the fixture, runs the guard, wants exit 0.
expect_pass() {
    local out
    commit_all
    if out="$(run_guard "$2")"; then record 0 "$1"; else record 1 "$1 (expected exit 0)"; echo "$out"; fi
}

# expect_fail <description> <wanted-text> <script> -- wants a non-zero exit mentioning <wanted-text>.
expect_fail() {
    local out
    commit_all
    if out="$(run_guard "$3")"; then
        record 1 "$1 (expected a failure, got a pass)"
        echo "$out"
    elif grep -qF -- "$2" <<<"$out"; then
        record 0 "$1"
    else
        record 1 "$1 (failed, but not with '$2')"
        echo "$out"
    fi
}

# expect_skip <description> <script> -- wants a pass that says it skipped the scan.
expect_skip() {
    local out
    commit_all
    out="$(run_guard "$2")" && grep -q "skipped the scan" <<<"$out"
    record $? "$1"
}

# entries <guard> -- how many cached entries that guard has in the fixture.
entries() {
    find "$CACHE" -path "*/$1-*" -type f ! -name '.tmp.*' 2>/dev/null | wc -l | tr -d ' '
}

# entry_for <name-suffix> -- the cached entry file of the one file whose encoded path ends so.
entry_for() {
    find "$CACHE" -name "*_$1" | head -1
}

# --- function sizes ----------------------------------------------------------------------------

reset_repo
make_function Source/big.cpp big 8
make_function Source/small.cpp small 4
printf '8 Source/big.cpp::big\n' >"$BASELINE"
expect_pass "function sizes: a baselined over-cap function passes cold" check-function-sizes.sh
[ "$(entries function-sizes)" -ge 2 ]
record $? "function sizes: the cold run stored one entry per scanned file"
expect_pass "function sizes: and passes warm" check-function-sizes.sh

# A hit is really read: edit the stored answer for big.cpp and the guard must see the edit.
printf '#ok\n9\tSource/big.cpp::big\t1\n#end\n' >"$(entry_for 'Source%2Fbig.cpp')"
expect_fail "function sizes: an unchanged file's answer comes from the cache" "grew from 8 to 9" check-function-sizes.sh
drop_cache

expect_pass "function sizes: a rebuilt cache passes again" check-function-sizes.sh
make_function Source/big.cpp big 9
expect_fail "function sizes: a function made longer in a cached file fails" "grew from 8 to 9" check-function-sizes.sh
make_function Source/big.cpp big 8
printf '7 Source/big.cpp::big\n' >"$BASELINE"
expect_fail "function sizes: a lowered baseline fails with every file cached" "grew from 7 to 8" check-function-sizes.sh
: >"$BASELINE"
expect_fail "function sizes: a baseline entry removed fails with every file cached" "not in the baseline" check-function-sizes.sh
printf '8 Source/big.cpp::big\n' >"$BASELINE"
expect_pass "function sizes: the original baseline passes again" check-function-sizes.sh
FUNCTION_SIZE_CAP=3 expect_fail "function sizes: a lowered cap fails with every file cached" "small" check-function-sizes.sh

# A damaged cache is a rescan, never a wrong answer or a crash.
for f in "$CACHE"/function-sizes-*/*; do printf '#ok\n8\tpartial' >"$f"; done
expect_pass "function sizes: half-written entries are rescanned" check-function-sizes.sh
for f in "$CACHE"/function-sizes-*/*; do printf 'garbage' >"$f"; done
expect_pass "function sizes: garbage entries are rescanned" check-function-sizes.sh
drop_cache
mkdir -p "$CACHE" && chmod 500 "$CACHE"
expect_pass "function sizes: an unwritable cache folder still passes" check-function-sizes.sh
chmod 700 "$CACHE"
drop_cache
GUARD_CACHE=0 expect_pass "function sizes: GUARD_CACHE=0 passes" check-function-sizes.sh
[ ! -d "$CACHE" ]
record $? "function sizes: GUARD_CACHE=0 writes no cache folder"

# Several gates at once: every run passes, no temporary file is left, and the entries read back.
drop_cache
pids=()
for i in 1 2 3 4 5 6; do
    (
        run_guard check-function-sizes.sh >"$TMPROOT/par$i.log" 2>&1
        echo $? >"$TMPROOT/par$i.rc"
    ) &
    pids+=("$!")
done
wait "${pids[@]}"
all_ok=0
for i in 1 2 3 4 5 6; do [ "$(cat "$TMPROOT/par$i.rc")" = 0 ] || all_ok=1; done
record "$all_ok" "function sizes: six runs writing one cache at once all pass"
[ -z "$(find "$CACHE" -name '.tmp.*' 2>/dev/null)" ]
record $? "function sizes: concurrent runs leave no temporary files"
expect_pass "function sizes: the entries they wrote read back" check-function-sizes.sh

# The guard's own scripts are part of the key: a scanner that finds nothing must not be served old answers.
rm -rf "${TMPROOT:?}/s"
mkdir -p "$TMPROOT/s"
cp -R "$SCRIPTS" "$TMPROOT/s/scripts"
GUARD_SCRIPTS="$TMPROOT/s/scripts"
drop_cache
expect_pass "function sizes: a copy of the guard passes and fills the cache" check-function-sizes.sh
printf 'BEGIN { }\n' >"$TMPROOT/s/scripts/function-size-scan.awk"
expect_fail "function sizes: an edited scanner is not served the old scanner's answers" "stale baseline entry" check-function-sizes.sh
GUARD_SCRIPTS="$SCRIPTS"

# --- file sizes --------------------------------------------------------------------------------

reset_repo
make_lines big.md 12
make_lines small.md 3
printf '12 big.md\n' >"$BASELINE"
expect_pass "file sizes: a baselined over-cap file passes cold" check-file-sizes.sh
expect_pass "file sizes: and passes warm" check-file-sizes.sh
printf '#ok\n13 big.md\n#end\n' >"$(entry_for big.md)"
expect_fail "file sizes: an unchanged file's count comes from the cache" "grew from 12 to 13" check-file-sizes.sh
drop_cache
make_lines big.md 13
expect_fail "file sizes: a file made longer after it was cached fails" "grew from 12 to 13" check-file-sizes.sh
make_lines big.md 12
printf '11 big.md\n' >"$BASELINE"
expect_fail "file sizes: a lowered baseline fails with every file cached" "grew from 11 to 12" check-file-sizes.sh
: >"$BASELINE"
expect_fail "file sizes: a removed baseline entry fails with every file cached" "not in the baseline" check-file-sizes.sh
printf '12 big.md\n' >"$BASELINE"
expect_pass "file sizes: the original baseline passes again" check-file-sizes.sh
FILE_SIZE_CAP=20 expect_fail "file sizes: a raised cap makes the entry stale, with every file cached" "stale baseline entry" check-file-sizes.sh
FILE_SIZE_CAP=2 expect_fail "file sizes: a lowered cap fails with every file cached" "small.md" check-file-sizes.sh
(cd "$REPO" && git rm -q -f big.md)
expect_fail "file sizes: a deleted baselined file fails" "stale baseline entry" check-file-sizes.sh

# --- header comments ---------------------------------------------------------------------------

reset_repo
make_header Source/UI/Foo/Foo.h 15 5
printf '// unit\nvoid f() {}\n' >"$REPO/Source/UI/Foo/Foo.cpp"
printf '10 Source/UI/Foo/Foo.h\n' >"$BASELINE"
expect_pass "header comments: a baselined heavy header passes cold" check-header-comments.sh
expect_pass "header comments: and passes warm" check-header-comments.sh
printf '#ok\n16 5 Source/UI/Foo/Foo.h\n#end\n' >"$(entry_for 'Source%2FUI%2FFoo%2FFoo.h')"
expect_fail "header comments: an unchanged header's counts come from the cache" "grew from +10 to +11" check-header-comments.sh
drop_cache
make_header Source/UI/Foo/Foo.h 16 5
expect_fail "header comments: a header with more comments after it was cached fails" "grew from +10 to +11" check-header-comments.sh
make_header Source/UI/Foo/Foo.h 15 5
printf '9 Source/UI/Foo/Foo.h\n' >"$BASELINE"
expect_fail "header comments: a lowered baseline fails with every header cached" "grew from +9 to +10" check-header-comments.sh
printf '10 Source/UI/Foo/Foo.h\n' >"$BASELINE"
expect_pass "header comments: the original baseline passes again" check-header-comments.sh
HEADER_COMMENT_FLOOR=20 expect_fail "header comments: a raised floor fails with every header cached" "stale baseline entry" check-header-comments.sh
drop_file Source/UI/Foo/Foo.cpp
expect_fail "header comments: deleting the sibling .cpp (another file) unflags the header" "stale baseline entry" check-header-comments.sh

# --- fixed waits -------------------------------------------------------------------------------

reset_repo
write_file Tests/Helpers.h <<EOF
#pragma once
inline void pump() { $PUMP(20); }
EOF
write_file Tests/ATests.cpp <<EOF
#include "Helpers.h"
TEST(A, B) {
    start();
    $PUMP(50);
    EXPECT_TRUE(done());
    pump();
    EXPECT_TRUE(done());
}
EOF
{
    printf 'Tests/ATests.cpp | %s(50); | a negative check\n' "$PUMP"
    printf 'Tests/ATests.cpp | pump(); | a negative check\n'
} >"$BASELINE"
expect_pass "fixed waits: two baselined sites pass cold" check-fixed-waits.sh
expect_pass "fixed waits: and pass warm" check-fixed-waits.sh
printf 'Tests/ATests.cpp | pump(); | a negative check\n' >"$BASELINE"
expect_fail "fixed waits: a removed baseline entry fails with every file cached" "fixed-time wait" check-fixed-waits.sh
{
    printf 'Tests/ATests.cpp | %s(50); | a negative check\n' "$PUMP"
    printf 'Tests/ATests.cpp | pump(); | a negative check\n'
} >"$BASELINE"
expect_pass "fixed waits: the original baseline passes again" check-fixed-waits.sh
write_file Tests/BTests.cpp <<EOF
TEST(B, C) {
    $PUMP(30);
    EXPECT_TRUE(done());
}
EOF
expect_fail "fixed waits: a new site in a new file fails with the rest cached" "Tests/BTests.cpp:2:" check-fixed-waits.sh
drop_file Tests/BTests.cpp
# ATests.cpp is untouched, but what pump() means lives in a header: the answer for it must change.
write_file Tests/Helpers.h <<EOF
#pragma once
inline void pump() { settle(); }
EOF
expect_fail "fixed waits: redefining a wrapper in a header changes an untouched file's answer" "stale baseline entry" check-fixed-waits.sh

# --- docs --------------------------------------------------------------------------------------

seed_docs() {
    write_file "$D$S""architecture.md" <<'EOF'
# Architecture

## 1. Overview

Some text.

## 2. Details

More text.
EOF
    write_file "$D$S""README.md" <<'EOF'
# Docs

[architecture](architecture.md) -- see [details](architecture.md#2-details).
EOF
}

reset_repo
seed_docs
expect_pass "docs: a clean tree passes cold" check-docs.sh
expect_skip "docs: the same inputs again skip the scan" check-docs.sh
drop_file "$D$S""architecture.md"
expect_fail "docs: deleting a link's target fails although README is unchanged" "architecture.md" check-docs.sh
seed_docs
expect_skip "docs: restoring the same content is the same inputs again" check-docs.sh
write_file "$D$S""architecture.md" <<'EOF'
# Architecture

## 1. Overview
EOF
expect_fail "docs: removing the heading an anchor link needs fails" "details" check-docs.sh
seed_docs
write_file "$D$S""lonely.md" <<'EOF'
# Lonely
EOF
expect_fail "docs: a new doc README does not link fails" "lonely.md" check-docs.sh
drop_file "$D$S""lonely.md"
printf 'naming %s%sOld_Name.md\n' "$D" "$S" >"$BASELINE"
expect_fail "docs: a changed baseline fails although no doc changed" "stale baseline entry" check-docs.sh
: >"$BASELINE"
write_file Source/note.cpp <<EOF
// see $D${S}missing.md for the background
EOF
expect_fail "docs: a doc mention added to a source file fails" "missing.md" check-docs.sh
drop_file Source/note.cpp
expect_pass "docs: removing it passes again" check-docs.sh
# A failed run is never remembered.
write_file "$D$S""Bad_Name.md" <<'EOF'
# Bad
EOF
expect_fail "docs: a bad name fails" "Bad_Name.md" check-docs.sh
expect_fail "docs: and fails again, a failure is never cached" "Bad_Name.md" check-docs.sh

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
