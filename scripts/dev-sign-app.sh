#!/usr/bin/env bash
#
# dev-sign-app.sh — re-sign a local "Agent Synth.app" build with a stable Apple Development
# identity, so macOS TCC stops re-asking for microphone access on every rebuild.
#
# WHY: a local ad-hoc/linker-signed build's designated requirement pins to `cdhash H"..."`, which
# changes on every build (any changed byte mints a new hash). TCC grants (microphone, etc.) are
# keyed on the designated requirement, so each fresh local build looks like a brand-new app and
# macOS re-prompts -- a dialog automation cannot dismiss, stalling live app testing on a human
# click. Signing with a real "Apple Development: <name> (<team>)" identity instead gives a
# designated requirement of the form:
#   identifier "com.agentsynth.app" and anchor apple generic and certificate leaf[subject.CN] =
#   "Apple Development: <name> (<team>)" and ...
# which is IDENTICAL across rebuilds (same identity -> same requirement), so TCC keeps recognising
# the app. The FIRST launch after switching to a stable identity still asks once; every later
# rebuild signed with the same identity does not.
#
# This is local dev tooling only:
#   - Never touches CI: CI macOS runners have no Apple Development identity installed, so this
#     naturally no-ops there (falls through to the "no identity found" branch below).
#   - Never touches release packaging: .github/workflows/build-artifacts.yml keeps its own
#     ad-hoc `codesign -s -` for distributed builds, untouched by this script.
#   - No hardened runtime, no entitlements -- this is a plain re-sign for local trust, not a
#     distributable/notarizable signature.
#
# Usage:
#   bash scripts/dev-sign-app.sh <path-to-app-bundle>
#
# Identity selection:
#   1. $AGENTSYNTH_DEV_SIGN_IDENTITY, if set and non-empty:
#        - "-" or "none" means "deliberately skip signing", exit 0
#        - any other value is passed straight to `codesign --sign`
#   2. Otherwise: the first valid identity from `security find-identity -v -p codesigning`
#      whose name starts with "Apple Development:".
#   3. If neither is available: print a note that the app stays ad-hoc signed (so macOS will
#      re-ask for the microphone on every rebuild) and how to fix it, then exit 0 -- a missing
#      dev-sign identity must never fail a build.
#
# A signature that fails to apply or verify IS a real problem (exit non-zero): unlike a missing
# identity, that's not an expected/dev-machine-dependent state.
#
# Called from scripts/ci-local.sh after a successful local build, on Darwin only.

set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "Usage: bash scripts/dev-sign-app.sh <path-to-app-bundle>" >&2
    exit 1
fi

APP_PATH="$1"

if [ "$(uname)" != "Darwin" ]; then
    echo "dev-sign-app: skipping (not macOS)."
    exit 0
fi

identity="${AGENTSYNTH_DEV_SIGN_IDENTITY:-}"

if [ -n "$identity" ]; then
    if [ "$identity" = "-" ] || [ "$identity" = "none" ]; then
        echo "dev-sign-app: skipping (AGENTSYNTH_DEV_SIGN_IDENTITY=$identity)."
        exit 0
    fi
else
    # `security find-identity -v -p codesigning` lists only valid (trusted, unexpired) identities,
    # one per line, like:
    #   1) 0123456789ABCDEF0123456789ABCDEF01234567 "Apple Development: Jane Doe (ABCDE12345)"
    # Take the first one whose quoted name starts with "Apple Development:".
    # `|| true`: under `set -o pipefail`, grep finding no match (the expected "no dev identity
    # installed" case) makes the whole pipeline exit non-zero, which -- since this is a plain
    # assignment, not `local` -- would trip `set -e` and abort the script before it ever reaches
    # the "no identity found" branch below.
    identity="$( (security find-identity -v -p codesigning 2>/dev/null \
        | grep -o '"Apple Development:[^"]*"' \
        | head -n 1 \
        | tr -d '"') || true)"
fi

if [ -z "$identity" ]; then
    cat <<'EOF'
dev-sign-app: no Apple Development signing identity found -- the app stays ad-hoc signed, so
macOS will re-ask for microphone access after every rebuild (an ad-hoc signature's designated
requirement pins to a cdhash that changes on every build, and TCC keys its grants on that
requirement).

To fix, either:
  - create a signing identity: Xcode > Settings > Accounts > Manage Certificates > + >
    Apple Development, or
  - set AGENTSYNTH_DEV_SIGN_IDENTITY to an existing identity's name (or to "none" to silence
    this note deliberately).
EOF
    exit 0
fi

echo "dev-sign-app: signing with identity: $identity"
codesign --force --deep --sign "$identity" "$APP_PATH"
codesign --verify --deep --strict "$APP_PATH"
echo "dev-sign-app: signed and verified: $APP_PATH"
