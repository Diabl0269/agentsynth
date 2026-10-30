#!/usr/bin/env bash
#
# Unit tests for what scripts/ci-local.sh skips because nothing changed: the per-test dependency keys
# (scripts/lib/test-deps.py), the script self-tests that rerun only when something they read changed,
# the whole-gate stamp, and --fast leaving the guards and script tests to the full gate.
#
# A stale pass is worse than a slow gate, so most cases are a change that must NOT be skipped: an
# edited test, an edited script it runs, a file that script sources, a failing test (never stamped),
# a tree-scanning test, a different flag, an untracked file, an --open launch. The gate runs for real
# in a throwaway git repo with stub clang-format and cmake and guards that only log that they ran.
# No compiler, no network.
#
# Usage: bash scripts/tests/ci-local-skip.test.sh

set -uo pipefail

unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPTS="$SCRIPT_DIR/scripts"

TMPROOT="$(mktemp -d)"
trap 'rm -rf "${TMPROOT:?}"' EXIT
G="$TMPROOT/g"
RAN_LOG="$TMPROOT/ran.log"
export RAN_LOG

pass=0
fail=0

record() {
    if [ "$1" -eq 0 ]; then
        echo "PASS: $2"
        pass=$((pass + 1))
    else
        echo "FAIL: $2"
        fail=$((fail + 1))
    fi
}

# --- the dependency keys -----------------------------------------------------------------------

mkdir -p "$TMPROOT/k/scripts/tests" "$TMPROOT/k/scripts/lib" "$TMPROOT/k/.github/workflows"
K="$TMPROOT/k"
printf '#!/bin/bash\nbash "$(dirname "$0")/../one.sh"\n' >"$K/scripts/tests/one.test.sh"
printf '#!/bin/bash\n# prose mention of scripts/three.sh\nsource "$SCRIPT_DIR/lib/two.sh"\n' >"$K/scripts/one.sh"
printf 'two=1\n' >"$K/scripts/lib/two.sh"
printf 'three=1\n' >"$K/scripts/three.sh"
printf 'other=1\n' >"$K/scripts/other.sh"
printf '#!/bin/bash\nexit 0\n' >"$K/scripts/tests/bare.test.sh"
printf '#!/bin/bash\nfor s in "$SCRIPT_DIR"/scripts/*.sh; do bash "$s"; done\nbash scripts/one.sh\n' >"$K/scripts/tests/globbing.test.sh"
printf '#!/bin/bash\ngrep x .github/workflows/ci.yml\nbash scripts/one.sh\n' >"$K/scripts/tests/flows.test.sh"
printf 'name: ci\n' >"$K/.github/workflows/ci.yml"
printf 'name: other\n' >"$K/.github/workflows/other.yml"

key_of() { python3 "$SCRIPTS/lib/test-deps.py" --root "$K" "scripts/tests/$1.test.sh" | cut -f2; }

k0="$(key_of one)"
[ -n "$k0" ]
record $? "keys: a test that runs a script has a key"
[ "$k0" = "$(key_of one)" ]
record $? "keys: the key is stable when nothing changed"
printf 'other=2\n' >"$K/scripts/other.sh"
[ "$k0" = "$(key_of one)" ]
record $? "keys: editing a script the test never names leaves the key alone"
printf 'three=2\n' >"$K/scripts/three.sh"
[ "$k0" = "$(key_of one)" ]
record $? "keys: a script named only in a comment is not a dependency"
printf 'two=2\n' >"$K/scripts/lib/two.sh"
k1="$(key_of one)"
[ "$k0" != "$k1" ]
record $? "keys: editing a file the tested script sources changes the key"
printf '#!/bin/bash\n# prose mention of scripts/three.sh\nsource "$SCRIPT_DIR/lib/two.sh"\necho more\n' >"$K/scripts/one.sh"
[ "$k1" != "$(key_of one)" ]
record $? "keys: editing the tested script changes the key"
k2="$(key_of one)"
printf '#!/bin/bash\nbash "$(dirname "$0")/../one.sh"\necho more\n' >"$K/scripts/tests/one.test.sh"
[ "$k2" != "$(key_of one)" ]
record $? "keys: editing the test itself changes the key"
[ -z "$(key_of bare)" ]
record $? "keys: a test that names no other file has no key, so it always runs"
[ -z "$(key_of globbing)" ]
record $? "keys: a test that reaches scripts by glob has no key, so it always runs"
kf="$(key_of flows)"
printf 'name: other2\n' >"$K/.github/workflows/other.yml"
[ "$kf" != "$(key_of flows)" ]
record $? "keys: a test that names the workflows depends on every workflow file"

# Every test the real repo has either gets a key or is run every time; tree-scanning ones never skip.
real="$(python3 "$SCRIPTS/lib/test-deps.py" --root "$SCRIPT_DIR" "$SCRIPTS"/tests/*.test.sh | cut -f1 | sort | tr '\n' ' ')"
expected="$(for t in "$SCRIPTS"/tests/*.test.sh; do basename "$t" .test.sh; done | sort | tr '\n' ' ')"
[ "$real" = "$expected" ]
record $? "keys: the real repo's tests all map"
for name in check-nonascii-literals check-ui-caps no-pipe-into-grep-q; do
    [ -f "$SCRIPTS/tests/$name.test.sh" ]
    record $? "keys: the tree-scanning test $name still exists, so the list in ci-local.sh is not stale"
done
grep -q '^tree_scanning="check-nonascii-literals check-ui-caps no-pipe-into-grep-q"$' "$SCRIPTS/ci-local.sh"
record $? "keys: ci-local.sh still lists the tree-scanning tests"

# --- the gate, run for real over stubs ---------------------------------------------------------

mkdir -p "$TMPROOT/bin" "$G/scripts/tests" "$G/Source" "$G/Tests" "$G/Tools"
(cd "$SCRIPTS" && tar cf - --exclude=./tests --exclude=__pycache__ .) | (cd "$G/scripts" && tar xf -)
for f in "$G"/scripts/check-*.sh "$G/scripts/utf8-literal-check.sh" "$G/scripts/ci-cache-budget.sh"; do
    printf '#!/bin/bash\necho guard >>"$RAN_LOG"\nexit 0\n' >"$f"
done
printf '#!/bin/bash\nmkdir -p build-ci-local\nexit 0\n' >"$TMPROOT/bin/cmake"
printf '#!/bin/bash\n[ "$1" = --version ] && echo "clang-format version 18.1.0"\nexit 0\n' >"$TMPROOT/bin/clang-format"
chmod +x "$TMPROOT"/bin/*
printf 'int a;\n' >"$G/Source/a.cpp"
printf 'int b;\n' >"$G/Tests/b.cpp"
printf 'int c;\n' >"$G/Tools/c.cpp"
printf 'build*\n' >"$G/.gitignore"
# alpha and beta test scripts of their own; gamma names nothing (always runs); check-ui-caps is a tree-scanning name.
for n in alpha beta; do
    printf '#!/bin/bash\necho %s >>"$RAN_LOG"\nbash "$(dirname "$0")/../%s.sh"\n' "$n" "$n" >"$G/scripts/tests/$n.test.sh"
    printf '#!/bin/bash\nexit 0\n' >"$G/scripts/$n.sh"
done
printf '#!/bin/bash\necho gamma >>"$RAN_LOG"\nexit 0\n' >"$G/scripts/tests/gamma.test.sh"
printf '#!/bin/bash\necho tree >>"$RAN_LOG"\nbash "$(dirname "$0")/../tree.sh"\n' >"$G/scripts/tests/check-ui-caps.test.sh"
printf '#!/bin/bash\nexit 0\n' >"$G/scripts/tree.sh"
(cd "$G" && git init -q && git add -A && git -c user.email=t@t -c user.name=t commit -qm init)

# gate <flags> -- runs the gate over the fixture; output in $OUT, what ran in $RAN.
gate() {
    : >"$RAN_LOG"
    OUT="$(cd "$G" && PATH="$TMPROOT/bin:$PATH" bash scripts/ci-local.sh "$@" 2>&1)"
    GATE_RC=$?
    RAN="$(sort "$RAN_LOG" | uniq -c | awk '{ printf "%s*%s ", $2, $1 }')"
}
ran() { case " $RAN" in *" $1*"*) return 0 ;; *) return 1 ;; esac; }
full_run() { grep -q '=== clang-format' <<<"$OUT"; }
commit_fixture() { (cd "$G" && git add -A && git -c user.email=t@t -c user.name=t commit -qm change); }

gate --skip-tests
[ "$GATE_RC" -eq 0 ] && full_run && ran alpha && ran beta && ran gamma && ran tree && ran guard
record $? "gate: the first run runs every guard and every script test (ran: $RAN)"

gate --skip-tests
[ "$GATE_RC" -eq 0 ] && ! full_run && grep -q "already passed this gate" <<<"$OUT" && [ -z "$RAN" ]
record $? "gate: the same tree again returns at once and says so (ran: $RAN)"

gate --skip-tests --open
[ "$GATE_RC" -eq 0 ] && full_run && ran guard
record $? "gate: an --open launch never takes the stamp (ran: $RAN)"

CI_LOCAL_FORCE=1 gate --skip-tests
[ "$GATE_RC" -eq 0 ] && full_run
record $? "gate: CI_LOCAL_FORCE=1 runs it anyway"

# The tests are stamped: with another change the gate runs again but only gamma and the tree-scanning test rerun.
printf 'int a2;\n' >>"$G/Source/a.cpp"
gate --skip-tests
[ "$GATE_RC" -eq 0 ] && full_run && ran gamma && ran tree && ! ran alpha && ! ran beta && ran guard
record $? "gate: an unrelated change reruns the guards, the always-run tests, and skips alpha and beta (ran: $RAN)"

printf '#!/bin/bash\n# changed\nexit 0\n' >"$G/scripts/alpha.sh"
gate --skip-tests
[ "$GATE_RC" -eq 0 ] && ran alpha && ! ran beta
record $? "gate: editing the script alpha tests reruns alpha only (ran: $RAN)"

printf '#!/bin/bash\necho alpha >>"$RAN_LOG"\n# changed\nbash "$(dirname "$0")/../alpha.sh"\n' >"$G/scripts/tests/alpha.test.sh"
gate --skip-tests
[ "$GATE_RC" -eq 0 ] && ran alpha && ! ran beta
record $? "gate: editing alpha's own test reruns alpha only (ran: $RAN)"

printf '#!/bin/bash\nexit 1\n' >"$G/scripts/beta.sh"
gate --skip-tests
[ "$GATE_RC" -ne 0 ] && ran beta && ! ran alpha
record $? "gate: a script that now fails its test fails the gate (ran: $RAN)"
gate --skip-tests
[ "$GATE_RC" -ne 0 ] && ran beta
record $? "gate: and the failure is never stamped, so beta runs and fails again"
printf '#!/bin/bash\nexit 0\n' >"$G/scripts/beta.sh"
gate --skip-tests
[ "$GATE_RC" -eq 0 ] && grep -q "already passed this gate" <<<"$OUT"
record $? "gate: undoing the break gives back the tree that passed, which is recognised (ran: $RAN)"

printf 'new\n' >"$G/untracked.txt"
gate --skip-tests
[ "$GATE_RC" -eq 0 ] && full_run
record $? "gate: an untracked file means the tree is not the one that passed"
rm -f "${G:?}/untracked.txt"
commit_fixture
gate --skip-tests
[ "$GATE_RC" -eq 0 ] && full_run
record $? "gate: committing changes the tree hash too, so it runs again"

gate
[ "$GATE_RC" -ne 0 ] || full_run
record $? "gate: other flags are another key, never the --skip-tests stamp"

# --fast leaves the guards and the script tests to the full gate and says so.
gate --fast --skip-tests
[ "$GATE_RC" -eq 0 ] && grep -q "fast skips the guards" <<<"$OUT" && grep -q "fast skips the script self-tests" <<<"$OUT" && [ -z "$RAN" ]
record $? "fast: no guard and no script test runs, and it prints that it skipped them (ran: $RAN)"
gate --fast --skip-tests
grep -q "already passed this gate" <<<"$OUT"
record $? "fast: a repeat on the same tree returns at once"
gate --skip-tests
grep -q "already passed this gate" <<<"$OUT"
record $? "fast: its stamp is separate: the full gate's stamp is still there"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
