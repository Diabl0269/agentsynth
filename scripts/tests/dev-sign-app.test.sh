#!/usr/bin/env bash
#
# Unit tests for scripts/dev-sign-app.sh.
#
# That script exists so a local rebuild doesn't re-trigger the microphone TCC prompt (T184) --
# if identity selection or the Darwin/skip guards regress, developers are back to a permission
# dialog automation can't dismiss on every single build. Runs in the Lint job: no compiler, no
# real codesign/security call, no network, ~1s -- entirely fake `uname`/`security`/`codesign`
# shims on PATH stand in for the real macOS tools, so this also passes on Linux CI.
#
# Usage: bash scripts/tests/dev-sign-app.test.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="$SCRIPT_DIR/scripts/dev-sign-app.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0

APP_PATH="$WORK/Agent Synth.app"
mkdir -p "$APP_PATH"

FAKEBIN="$WORK/fakebin"
mkdir -p "$FAKEBIN"

cat >"$FAKEBIN/uname" <<'EOF'
#!/usr/bin/env bash
echo "${FAKE_UNAME_OS:-Darwin}"
EOF

# Stands in for `security find-identity -v -p codesigning`: cats a fixture file (list format,
# one quoted identity name per line) if one is configured, else prints nothing -- matching the
# real tool's "0 valid identities found" case. Also logs that it was called, so tests can assert
# it was (or wasn't) consulted.
cat >"$FAKEBIN/security" <<'EOF'
#!/usr/bin/env bash
[ -n "${FAKE_SECURITY_LOG:-}" ] && echo "$@" >>"$FAKE_SECURITY_LOG"
if [ -n "${FAKE_SECURITY_OUTPUT:-}" ] && [ -f "$FAKE_SECURITY_OUTPUT" ]; then
    cat "$FAKE_SECURITY_OUTPUT"
fi
EOF

# Stands in for `codesign`: logs every invocation's full argument list (one per line) so a test
# can assert both which identity was used and that --verify was (or wasn't) reached. Fails the
# --verify call when FAKE_CODESIGN_VERIFY_FAIL is set, to exercise the "signed but didn't verify"
# path.
cat >"$FAKEBIN/codesign" <<'EOF'
#!/usr/bin/env bash
[ -n "${FAKE_CODESIGN_LOG:-}" ] && echo "$@" >>"$FAKE_CODESIGN_LOG"
if [ "${1:-}" = "--verify" ] && [ -n "${FAKE_CODESIGN_VERIFY_FAIL:-}" ]; then
    echo "fake codesign: verify failed" >&2
    exit 1
fi
exit 0
EOF

chmod +x "$FAKEBIN"/uname "$FAKEBIN"/security "$FAKEBIN"/codesign

cat >"$WORK/two-identities.txt" <<'EOF'
  1) AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA "Apple Development: Jane Doe (TEAM1234AB)"
  2) BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB "Apple Development: John Roe (TEAM1234AB)"
     2 valid identities found
EOF

cat >"$WORK/no-dev-identities.txt" <<'EOF'
  1) CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC "Developer ID Application: Some Org (TEAM1234AB)"
     1 valid identities found
EOF

# run <name> <expected_exit> <expected_output_substring|-> [env assignments...]
#
# Every input the script reads (identity override, and the fakes' own control knobs) is unset
# before the test's own assignments are applied, so tests are hermetic and order-independent.
run() {
    local name="$1" want_exit="$2" want_text="$3"
    shift 3
    local out status
    codesign_log="$WORK/codesign.log"
    security_log="$WORK/security.log"
    rm -f "$codesign_log" "$security_log"
    out="$(env -u AGENTSYNTH_DEV_SIGN_IDENTITY -u FAKE_UNAME_OS -u FAKE_SECURITY_OUTPUT \
               -u FAKE_CODESIGN_VERIFY_FAIL \
               PATH="$FAKEBIN:$PATH" \
               FAKE_CODESIGN_LOG="$codesign_log" FAKE_SECURITY_LOG="$security_log" \
               "$@" bash "$SCRIPT" "$APP_PATH" 2>&1)"
    status=$?

    if [ "$status" -ne "$want_exit" ]; then
        printf 'FAIL  %s\n      expected exit %s, got %s\n%s\n' "$name" "$want_exit" "$status" "$out"
        fail=$((fail + 1))
        return 1
    fi
    if [ "$want_text" != "-" ] && ! printf '%s' "$out" | grep -qF "$want_text"; then
        printf 'FAIL  %s\n      expected output to contain: %s\n%s\n' "$name" "$want_text" "$out"
        fail=$((fail + 1))
        return 1
    fi
    printf 'ok    %s\n' "$name"
    pass=$((pass + 1))
    return 0
}

assert_log() { # assert_log <desc> <logfile> <contains|absent> <needle>
    local desc="$1" logfile="$2" mode="$3" needle="$4"
    local hit=false
    [ -f "$logfile" ] && grep -qF -- "$needle" "$logfile" && hit=true
    if { [ "$mode" = contains ] && $hit; } || { [ "$mode" = absent ] && ! $hit; }; then
        printf 'ok    %s\n' "$desc"
        pass=$((pass + 1))
    else
        printf 'FAIL  %s\n      %s expected to %s %q, log:\n%s\n' \
            "$desc" "$logfile" "$mode" "$needle" "$(cat "$logfile" 2>/dev/null || echo '<missing>')"
        fail=$((fail + 1))
    fi
}

# --- (a) auto-picks the first "Apple Development:" identity -------------------------------
if run "auto-pick: uses the first Apple Development identity" 0 \
    "signing with identity: Apple Development: Jane Doe (TEAM1234AB)" \
    FAKE_SECURITY_OUTPUT="$WORK/two-identities.txt"; then
    assert_log "auto-pick: codesign --sign called with the first identity" \
        "$WORK/codesign.log" contains "Apple Development: Jane Doe (TEAM1234AB)"
    assert_log "auto-pick: does not use the second identity" \
        "$WORK/codesign.log" absent "John Roe"
    assert_log "auto-pick: codesign --verify --deep --strict was run" \
        "$WORK/codesign.log" contains "--verify --deep --strict"
fi

# --- (b) AGENTSYNTH_DEV_SIGN_IDENTITY overrides auto-detection -----------------------------
if run "override: AGENTSYNTH_DEV_SIGN_IDENTITY wins over the identity list" 0 \
    "signing with identity: My Custom Identity" \
    FAKE_SECURITY_OUTPUT="$WORK/two-identities.txt" \
    AGENTSYNTH_DEV_SIGN_IDENTITY="My Custom Identity"; then
    assert_log "override: codesign --sign called with the override identity" \
        "$WORK/codesign.log" contains "My Custom Identity"
    assert_log "override: does not use the auto-detected identity" \
        "$WORK/codesign.log" absent "Jane Doe"
fi

# --- (c) "none" deliberately skips, without calling codesign -------------------------------
if run "none: skips signing without calling codesign" 0 "skipping (AGENTSYNTH_DEV_SIGN_IDENTITY=none)" \
    FAKE_SECURITY_OUTPUT="$WORK/two-identities.txt" \
    AGENTSYNTH_DEV_SIGN_IDENTITY=none; then
    [ -s "$WORK/codesign.log" ] && {
        printf 'FAIL  none: codesign log should be empty/absent, got:\n%s\n' "$(cat "$WORK/codesign.log")"
        fail=$((fail + 1))
    } || {
        printf 'ok    none: codesign log is empty\n'
        pass=$((pass + 1))
    }
fi

# --- (d) no identity found: exits 0, no codesign call, prints the fix-it note --------------
if run "no identity: exits 0 with a note, does not call codesign" 0 \
    "no Apple Development signing identity found" \
    FAKE_SECURITY_OUTPUT="$WORK/no-dev-identities.txt"; then
    [ -s "$WORK/codesign.log" ] && {
        printf 'FAIL  no identity: codesign log should be empty, got:\n%s\n' "$(cat "$WORK/codesign.log")"
        fail=$((fail + 1))
    } || {
        printf 'ok    no identity: codesign log is empty\n'
        pass=$((pass + 1))
    }
fi
run "no identity: note mentions the env var fix" 0 "AGENTSYNTH_DEV_SIGN_IDENTITY" \
    FAKE_SECURITY_OUTPUT="$WORK/no-dev-identities.txt"

# --- (e) non-Darwin: exits 0, calls neither security nor codesign --------------------------
if run "non-Darwin: skips without consulting security or codesign" 0 "skipping (not macOS)" \
    FAKE_UNAME_OS=Linux FAKE_SECURITY_OUTPUT="$WORK/two-identities.txt"; then
    [ -s "$WORK/security.log" ] && {
        printf 'FAIL  non-Darwin: security should not have been called, got:\n%s\n' "$(cat "$WORK/security.log")"
        fail=$((fail + 1))
    } || {
        printf 'ok    non-Darwin: security was not called\n'
        pass=$((pass + 1))
    }
    [ -s "$WORK/codesign.log" ] && {
        printf 'FAIL  non-Darwin: codesign should not have been called, got:\n%s\n' "$(cat "$WORK/codesign.log")"
        fail=$((fail + 1))
    } || {
        printf 'ok    non-Darwin: codesign was not called\n'
        pass=$((pass + 1))
    }
fi

# --- (f) codesign --verify failure is a real problem: non-zero exit ------------------------
run "verify failure: exits non-zero" 1 - \
    FAKE_SECURITY_OUTPUT="$WORK/two-identities.txt" \
    FAKE_CODESIGN_VERIFY_FAIL=1

printf '\n%s passed, %s failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
