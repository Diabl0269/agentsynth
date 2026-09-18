#!/usr/bin/env bash
#
# Unit tests for scripts/check-docs.sh.
#
# The guard exists to stop the doc tree from drifting silently -- a rename that misses a
# Source/** comment, a link that outlives its target, a section reference nobody re-numbers. If
# the guard itself has a hole (a mis-scanned exclusion, a ratchet that doesn't actually ratchet, a
# regex that matches the wrong `]`), the checks are decorative. Runs in the Lint job -- no
# compiler, no network, ~1s (each case builds a tiny throwaway fixture tree; unlike
# check-file-sizes.test.sh, check-docs.sh scans the filesystem directly via `find`, not
# `git ls-files`, so fixtures need no `git init` at all).
#
# A NOTE ON WHY FIXTURE PATHS ARE BUILT FROM `$D`/`$S` INSTEAD OF WRITTEN OUT: checks C and D in
# check-docs.sh scan every in-scope *.sh file's own raw bytes for a doc-folder-plus-filename
# mention -- and this .sh file is itself in scope. A fixture path spelled out literally (folder
# name, slash, fake filename, dot-em-dee, all adjacent) would therefore be a real occurrence THIS
# FILE'S OWN SOURCE makes when the real check-docs.sh scans the real repo, even though it's only
# ever meant as fake content for a throwaway $REPO fixture tree miles from here -- and most of
# these fixture names don't exist for real, so they'd fail check C against the real tree. Building
# each one from `$D`/`$S` (set once, below) keeps that literal substring out of this file's bytes
# entirely, so scanning THIS file never manufactures a false violation.
#
# Usage: bash scripts/tests/check-docs.test.sh

set -uo pipefail

# Same reasoning as check-file-sizes.test.sh: strip any inherited git env so nothing here can ever
# touch a real repository, even though this harness itself doesn't call git.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECK="$SCRIPT_DIR/scripts/check-docs.sh"

# See the header comment above: never write these adjacently as a literal "docs/" in this file.
D="docs"
S="/"
DOCPFX="$D$S" # only for use INSIDE heredoc bodies -- see each site below

TMPROOT="$(mktemp -d)"
trap 'rm -rf "$TMPROOT"' EXIT
REPO="$TMPROOT/repo"
BASELINE="$TMPROOT/baseline.txt"

export DOCS_BASELINE="$BASELINE"

pass=0
fail=0

# --- fixture helpers ----------------------------------------------------------------------------

reset_repo() {
    rm -rf "$REPO"
    mkdir -p "$REPO/$D"
    rm -f "$BASELINE"
}

# write_file <relative-path> -- content on stdin.
write_file() {
    local path="$REPO/$1"
    mkdir -p "$(dirname "$path")"
    cat >"$path"
}

# link_from_readme <docs-relative-target> -- appends a link to docs/README.md so check E (map
# completeness) doesn't flag a fixture doc a check-A/B/C/D test adds as a SEPARATE, unrelated
# concern. Only needed by a test whose own point is A/B/C/D, not E -- the check-E tests below build
# their own README.md deliberately, without this.
link_from_readme() {
    printf '\n[extra](%s)\n' "$1" >>"$REPO/$D$S""README.md"
}

set_baseline() {
    : >"$BASELINE"
    for line in "$@"; do
        printf '%s\n' "$line" >>"$BASELINE"
    done
}

run_check() {
    bash "$CHECK" --root "$REPO"
}

assert_pass() {
    local desc="$1" output
    if output="$(run_check 2>&1)"; then
        echo "PASS: $desc"
        pass=$((pass + 1))
    else
        echo "FAIL: $desc (expected exit 0, got failure)"
        echo "$output"
        fail=$((fail + 1))
    fi
}

assert_fail() {
    local desc="$1" expect_grep="$2" output
    if output="$(run_check 2>&1)"; then
        echo "FAIL: $desc (expected non-zero exit, got success)"
        echo "$output"
        fail=$((fail + 1))
    elif echo "$output" | grep -qF -- "$expect_grep"; then
        echo "PASS: $desc"
        pass=$((pass + 1))
    else
        echo "FAIL: $desc (exited non-zero but output didn't mention '$expect_grep')"
        echo "$output"
        fail=$((fail + 1))
    fi
}

# A minimal docs/ tree with no violations at all -- every case below starts from this and adds
# exactly the one thing it's testing, so a clean baseline run always passes on its own.
seed_clean_tree() {
    write_file "$D$S""architecture.md" <<EOF
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

# --- check A: naming ------------------------------------------------------------------------

reset_repo
seed_clean_tree
assert_pass "a clean docs/ tree with no violations passes"

reset_repo
seed_clean_tree
write_file "$D$S""Some_Doc.md" <<'EOF'
# Some Doc
EOF
assert_fail "a non-kebab-case docs/*.md basename not in the baseline fails" "${D}${S}Some_Doc.md -- basename is not lowercase kebab-case"

reset_repo
seed_clean_tree
write_file "$D$S""Some_Doc.md" <<'EOF'
# Some Doc
EOF
link_from_readme "Some_Doc.md"
set_baseline "naming $D$S""Some_Doc.md"
assert_pass "a non-kebab-case basename matching a baseline entry passes"

reset_repo
seed_clean_tree
write_file "$D$S""kebab-ok.md" <<'EOF'
# Kebab OK
EOF
set_baseline "naming $D$S""kebab-ok.md"
assert_fail "a baseline naming entry for a file that no longer violates is stale" "${D}${S}kebab-ok.md is a stale baseline entry"

reset_repo
seed_clean_tree
write_file "$D$S""nested/deep/README.md" <<'EOF'
# README
EOF
link_from_readme "nested/deep/README.md"
assert_pass "README.md is exempt from kebab-case at any depth"

# --- check B: markdown links -----------------------------------------------------------------

reset_repo
seed_clean_tree
write_file "$D$S""dangling.md" <<'EOF'
# Dangling

See [missing](does-not-exist.md).
EOF
assert_fail "a markdown link to a nonexistent .md file fails" "does not exist"

reset_repo
seed_clean_tree
write_file "$D$S""dangling.md" <<'EOF'
# Dangling

See [architecture, wrong anchor](architecture.md#9-nope).
EOF
assert_fail "a markdown link with an anchor that has no matching heading fails" "anchor '#9-nope'"

reset_repo
seed_clean_tree
write_file "$D$S""ok.md" <<'EOF'
# OK

See [overview](architecture.md#1-overview) and the [repo map](../CLAUDE.md).
EOF
write_file "CLAUDE.md" <<'EOF'
# root
EOF
link_from_readme "ok.md"
assert_pass "a link resolving with a valid anchor passes, and one only resolvable above ROOT is skipped rather than failing"

# --- check C: docs/... path mentions ----------------------------------------------------------

reset_repo
seed_clean_tree
write_file "Source/Some.cpp" <<EOF
// see ${DOCPFX}does-not-exist.md for the full story
EOF
assert_fail "a docs/... mention in a Source/*.cpp comment that doesn't resolve fails" "${D}${S}does-not-exist.md' does not exist"

reset_repo
seed_clean_tree
write_file "Source/Some.cpp" <<EOF
// see ${DOCPFX}architecture.md for the full story
EOF
assert_pass "a docs/... mention that does resolve passes"

reset_repo
seed_clean_tree
write_file "Source/Some.cpp" <<EOF
// see other-repo${S}${DOCPFX}billing.md for the full story -- a QUALIFIED path into a sibling
// repo's docs/ tree, which must never be misread as naming OUR docs/ tree
EOF
assert_pass "a qualified sibling-repo docs/ path (preceded by '/') is not flagged, even though the bare filename doesn't exist here"

# --- check D: section references (zero tolerance, never baselined) ----------------------------

reset_repo
seed_clean_tree
write_file "Source/Some.h" <<EOF
// see ${DOCPFX}architecture.md §9 for the rationale
EOF
assert_fail "a stale §-section reference always fails -- no baseline can grandfather it" "architecture.md §9 -- no such section in ${D}${S}architecture.md"

reset_repo
seed_clean_tree
write_file "Source/Some.h" <<EOF
// see ${DOCPFX}architecture.md §1 for the rationale
EOF
assert_pass "a §-section reference to a heading that actually exists passes"

reset_repo
seed_clean_tree
write_file "$D$S""dotted.md" <<'EOF'
# Dotted

### 5.3 A real subsection, with no top-level "## 5" heading at all
EOF
write_file "Source/Some.h" <<EOF
// the coarser section reference below has no exact "## 5" heading in dotted.md -- only the finer
// "### 5.3" subsection does, so this must resolve via the dotted-prefix rule, not an exact match
// see ${DOCPFX}dotted.md §5 for the rationale
EOF
link_from_readme "dotted.md"
assert_pass "a coarser section reference resolves via a finer dotted heading (5.3 satisfies a query for 5)"

# --- check E: docs/README.md map completeness --------------------------------------------------

reset_repo
seed_clean_tree
write_file "$D$S""orphan.md" <<'EOF'
# Orphan

Not linked from README.md anywhere.
EOF
assert_fail "a real doc that docs/README.md never links fails" "${D}${S}orphan.md: not linked from ${D}${S}README.md"

reset_repo
seed_clean_tree
write_file "$D$S""README.md" <<'EOF'
# Docs

[architecture](architecture.md) -- see [details](architecture.md#2-details).
[missing](does-not-exist.md)
EOF
assert_fail "a docs/README.md map link to a file that doesn't exist fails" "${D}${S}README.md: links to ${D}${S}does-not-exist.md, which does not exist"

reset_repo
seed_clean_tree
write_file "$D$S""nested/deep/README.md" <<'EOF'
# README
EOF
assert_fail "only the top-level docs/README.md is exempt -- a nested README.md still needs a map link" "${D}${S}nested/deep/README.md: not linked from ${D}${S}README.md"

# --- --update semantics -----------------------------------------------------------------------

reset_repo
seed_clean_tree
write_file "$D$S""Old_Name.md" <<'EOF'
# Old Name
EOF
link_from_readme "Old_Name.md"
update_output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"
if echo "$update_output" | grep -qF -- "baseline rewritten (1 naming entries)" \
    && grep -qxF -- "naming ${D}${S}Old_Name.md" "$BASELINE"; then
    echo "PASS: --update (first run, no prior baseline) seeds the naming entry without needing --allow-growth"
    pass=$((pass + 1))
else
    echo "FAIL: --update (first run, no prior baseline) seeds the naming entry without needing --allow-growth"
    echo "$update_output"
    fail=$((fail + 1))
fi
assert_pass "a subsequent check passes against the just-written baseline"

reset_repo
seed_clean_tree
write_file "$D$S""Old_Name.md" <<'EOF'
# Old Name
EOF
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
write_file "$D$S""New_Violation.md" <<'EOF'
# New Violation
EOF
refuse_output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"
refuse_status=$?
if [ "$refuse_status" -ne 0 ] && echo "$refuse_output" | grep -qF -- "${D}${S}New_Violation.md is a new naming violation" \
    && ! grep -q "New_Violation" "$BASELINE"; then
    echo "PASS: --update refuses to add a new naming entry once a baseline already exists"
    pass=$((pass + 1))
else
    echo "FAIL: --update refuses to add a new naming entry once a baseline already exists"
    echo "exit=$refuse_status"
    echo "$refuse_output"
    fail=$((fail + 1))
fi

allow_output="$(bash "$CHECK" --root "$REPO" --update --allow-growth 2>&1)"
allow_status=$?
if [ "$allow_status" -eq 0 ] && echo "$allow_output" | grep -qF -- "::warning::${D}${S}New_Violation.md is a new naming violation" \
    && grep -qxF -- "naming ${D}${S}New_Violation.md" "$BASELINE"; then
    echo "PASS: --update --allow-growth lets a new naming entry through with a warning"
    pass=$((pass + 1))
else
    echo "FAIL: --update --allow-growth lets a new naming entry through with a warning"
    echo "exit=$allow_status"
    echo "$allow_output"
    fail=$((fail + 1))
fi

reset_repo
seed_clean_tree
write_file "Source/Some.h" <<EOF
// see ${DOCPFX}architecture.md §1 for the rationale
EOF
noop_output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"
noop_status=$?
if [ "$noop_status" -eq 0 ] && ! grep -q '^naming ' "$BASELINE" 2>/dev/null && [ -f "$BASELINE" ]; then
    echo "PASS: --update writes only naming entries -- §-references never touch the baseline at all"
    pass=$((pass + 1))
else
    echo "FAIL: --update writes only naming entries -- §-references never touch the baseline at all"
    echo "exit=$noop_status"
    echo "$noop_output"
    cat "$BASELINE" 2>/dev/null
    fail=$((fail + 1))
fi

# --- --list and vacuous-pass fail-safe -----------------------------------------------------

reset_repo
seed_clean_tree
write_file "$D$S""Bad_Name.md" <<'EOF'
# Bad Name
EOF
list_output="$(bash "$CHECK" --root "$REPO" --list 2>&1)"
list_status=$?
if [ "$list_status" -eq 0 ] && echo "$list_output" | grep -qF -- "${D}${S}Bad_Name.md" \
    && echo "$list_output" | grep -qF -- "=== naming violations (check A) ==="; then
    echo "PASS: --list exits 0 and summarizes current violations"
    pass=$((pass + 1))
else
    echo "FAIL: --list exits 0 and summarizes current violations"
    echo "exit=$list_status"
    echo "$list_output"
    fail=$((fail + 1))
fi

empty_dir="$TMPROOT/empty"
rm -rf "$empty_dir"
mkdir -p "$empty_dir/Source"
printf '// nothing here\n' >"$empty_dir/Source/Placeholder.cpp"
vacuous_output="$(bash "$CHECK" --root "$empty_dir" 2>&1)"
vacuous_status=$?
if [ "$vacuous_status" -ne 0 ] && echo "$vacuous_output" | grep -qF -- "0 .md files scanned"; then
    echo "PASS: zero .md files scanned fails loudly instead of reporting a vacuous pass"
    pass=$((pass + 1))
else
    echo "FAIL: zero .md files scanned fails loudly instead of reporting a vacuous pass"
    echo "exit=$vacuous_status"
    echo "$vacuous_output"
    fail=$((fail + 1))
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
