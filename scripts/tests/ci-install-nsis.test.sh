#!/usr/bin/env bash
#
# Unit tests for scripts/ci-install-nsis.sh.
#
# The point of that script is the path that only runs when Chocolatey is down -- which is exactly
# the path nobody exercises deliberately, and the one that skipped a whole release on 2026-09-16
# (FRO104). So the retry and the pinned-download fallback are tested here against a fake choco, a
# file:// installer URL and a fake silent installer. Runs in the Lint job: no choco, no Windows, no
# network, a couple of seconds (one case lets a real `timeout` kill a hanging fake).
#
# Usage: bash scripts/tests/ci-install-nsis.test.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="$SCRIPT_DIR/scripts/ci-install-nsis.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0

check() { # check <name> <what> <actual> <expected>
    if [ "$3" = "$4" ]; then
        printf 'ok    %s\n' "$1"
        pass=$((pass + 1))
    else
        printf 'FAIL  %s\n      %s: expected %q, got %q\n' "$1" "$2" "$4" "$3"
        fail=$((fail + 1))
    fi
}

# A fake choco whose behaviour is a list of outcomes, one per invocation, in $WORK/choco.plan:
#   ok       -> creates makensis.exe under the search root and exits 0
#   empty    -> exits 0 having installed nothing (the real 0/0 outage signature)
#   fail     -> exits 1
#   hang     -> sleeps 30 s (the per-attempt timeout has to kill it)
# Every call appends its outcome to $WORK/choco.log.
mkdir -p "$WORK/bin"
cat >"$WORK/bin/choco" <<'FAKE'
#!/usr/bin/env bash
plan="$WORK/choco.plan"
outcome="$(head -n 1 "$plan")"
tail -n +2 "$plan" >"$plan.tmp" && mv "$plan.tmp" "$plan"
echo "$outcome" >>"$WORK/choco.log"
case "$outcome" in
    ok)    mkdir -p "$WORK/root/NSIS" && : >"$WORK/root/NSIS/makensis.exe"; exit 0 ;;
    empty) exit 0 ;;
    fail)  exit 1 ;;
    hang)  sleep 30; exit 0 ;;
    *)     echo "fake choco: plan exhausted" >&2; exit 2 ;;
esac
FAKE
chmod +x "$WORK/bin/choco"

# A fake silent installer runner: records the path it was handed, then "installs" makensis.
cat >"$WORK/bin/fake-install" <<'FAKE'
#!/usr/bin/env bash
echo "$1" >>"$WORK/installer.log"
mkdir -p "$WORK/root/NSIS" && : >"$WORK/root/NSIS/makensis.exe"
FAKE
chmod +x "$WORK/bin/fake-install"

# The script caps each choco attempt with `timeout` (GNU coreutils). The Linux Lint runner has it,
# but macOS has neither `timeout` nor `gtimeout`, so running scripts/ci-local.sh on a Mac found no
# timeout binary, never ran the fake choco, and failed every choco-count check. Use the real one
# when present; otherwise a perl alarm shim with the same "<seconds> <command...>" shape.
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_FOR_TESTS="timeout"
else
    cat >"$WORK/bin/timeout" <<'FAKE'
#!/usr/bin/env bash
secs="$1"
shift
exec perl -e 'alarm shift; exec @ARGV' "$secs" "$@"
FAKE
    chmod +x "$WORK/bin/timeout"
    TIMEOUT_FOR_TESTS="$WORK/bin/timeout"
fi

# The "downloaded" installer is a fixture file served over file://, which curl handles natively.
printf 'not really an installer\n' >"$WORK/nsis-fixture.exe"
FIXTURE_SHA="$(sha256sum "$WORK/nsis-fixture.exe" | awk '{print $1}')"

reset() { # reset <choco plan lines...>
    rm -rf "$WORK/root" "$WORK/choco.log" "$WORK/installer.log" "$WORK/github_path"
    mkdir -p "$WORK/root"
    : >"$WORK/choco.log"
    : >"$WORK/installer.log"
    : >"$WORK/github_path"
    printf '%s\n' "$@" >"$WORK/choco.plan"
}

# macOS ships no `timeout`, so the script's default per-attempt cap would fail every choco call
# there; run uncapped when it is missing (the cap itself is only exercised where `timeout` exists).
TIMEOUT_FOR_TESTS="timeout"
command -v timeout >/dev/null 2>&1 || TIMEOUT_FOR_TESTS=""

run_script() { # run_script [extra VAR=value...] -- runs the script with the fakes wired in
    env WORK="$WORK" TIMEOUT_BIN="$TIMEOUT_FOR_TESTS" CHOCO="$WORK/bin/choco" CHOCO_BACKOFF=0 CHOCO_TIMEOUT=2 \
        NSIS_URL="file://$WORK/nsis-fixture.exe" NSIS_SHA256="$FIXTURE_SHA" \
        NSIS_INSTALLER_RUN="$WORK/bin/fake-install" NSIS_SEARCH_ROOTS="$WORK/root/NSIS;$WORK/root/other" \
        GITHUB_PATH="$WORK/github_path" "$@" bash "$SCRIPT" >"$WORK/out" 2>&1
}

choco_calls() { tr '\n' ' ' <"$WORK/choco.log" | sed 's/ $//'; }
installer_calls() { wc -l <"$WORK/installer.log" | tr -d ' '; }

# --- 1. healthy feed: one choco call, no fallback, makensis published ---------------------------
reset ok
run_script
check "healthy: exit 0" "exit" "$?" 0
check "healthy: exactly one choco call" "choco log" "$(choco_calls)" "ok"
check "healthy: installer never downloaded/run" "installer calls" "$(installer_calls)" 0
check "healthy: makensis dir appended to GITHUB_PATH" "GITHUB_PATH" "$(cat "$WORK/github_path")" "$WORK/root/NSIS"

# --- 2. transient outage: fails twice, third attempt works, no fallback -------------------------
reset fail fail ok
run_script
check "retry: exit 0" "exit" "$?" 0
check "retry: three choco calls" "choco log" "$(choco_calls)" "fail fail ok"
check "retry: no fallback needed" "installer calls" "$(installer_calls)" 0

# --- 3. the real 2026-09-16 signature: choco exits 0 having installed nothing ---------------------
reset empty empty empty
run_script
check "empty install: exit 0 via fallback" "exit" "$?" 0
check "empty install: every attempt used before falling back" "choco log" "$(choco_calls)" "empty empty empty"
check "empty install: fallback installer run once" "installer calls" "$(installer_calls)" 1
check "empty install: makensis published" "GITHUB_PATH" "$(cat "$WORK/github_path")" "$WORK/root/NSIS"
grep -q "Checksum verified" "$WORK/out"
check "empty install: checksum verified before running" "output" "$?" 0

# --- 4. a hanging choco is killed by the per-attempt timeout, then the fallback runs -------------
reset hang fail fail
run_script
check "hang: exit 0 via fallback" "exit" "$?" 0
check "hang: hang counted as one attempt, then the rest" "choco log" "$(choco_calls)" "hang fail fail"
check "hang: fallback installer run once" "installer calls" "$(installer_calls)" 1

# --- 5. checksum mismatch: never run the installer, fail loudly ---------------------------------
reset fail fail fail
run_script NSIS_SHA256=0000000000000000000000000000000000000000000000000000000000000000
check "bad checksum: exit 1" "exit" "$?" 1
check "bad checksum: installer NOT run" "installer calls" "$(installer_calls)" 0
grep -q "checksum mismatch" "$WORK/out"
check "bad checksum: mismatch reported" "output" "$?" 0
check "bad checksum: nothing published" "GITHUB_PATH" "$(cat "$WORK/github_path")" ""

# --- 6. feed down AND download fails ------------------------------------------------------------
reset fail fail fail
run_script NSIS_URL="file://$WORK/does-not-exist.exe"
check "download fails: exit 1" "exit" "$?" 1
check "download fails: installer NOT run" "installer calls" "$(installer_calls)" 0
grep -q "Could not download" "$WORK/out"
check "download fails: reported" "output" "$?" 0

# --- 7. installer ran but left no makensis: still a failure, not a silent pass -------------------
reset fail
cat >"$WORK/bin/noop-install" <<'FAKE'
#!/usr/bin/env bash
echo "$1" >>"$WORK/installer.log"
FAKE
chmod +x "$WORK/bin/noop-install"
run_script CHOCO_ATTEMPTS=1 NSIS_INSTALLER_RUN="$WORK/bin/noop-install"
check "no makensis after install: exit 1" "exit" "$?" 1
grep -q "makensis.exe not found" "$WORK/out"
check "no makensis after install: reported" "output" "$?" 0

# --- 8. already installed: no choco call at all ---------------------------------------------------
reset fail
mkdir -p "$WORK/root/NSIS" && : >"$WORK/root/NSIS/makensis.exe"
run_script
check "already installed: exit 0" "exit" "$?" 0
check "already installed: choco never called" "choco log" "$(choco_calls)" ""
check "already installed: makensis published" "GITHUB_PATH" "$(cat "$WORK/github_path")" "$WORK/root/NSIS"

echo
echo "ci-install-nsis.test.sh: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
