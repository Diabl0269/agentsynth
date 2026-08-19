#!/bin/bash
# Boots the whole client<->server loop locally, so Pro-gated features (cloud conversation
# history, x-conversation-id threading) can be exercised end-to-end with nothing deployed:
# local Postgres, a local synth-platform API server (dev IdP, real Ollama inference), and this
# repo's locally-built Debug app pointed at both. See docs/testing.md "Testing Cloud-Gated
# Features Locally" for the full manual walkthrough this automates, and background/context on
# why two separate hosts are involved.
#
# Usage:
#   scripts/run-local-cloud-dev.sh          # start Postgres + API server, then launch the app
#   scripts/run-local-cloud-dev.sh --down    # stop the API server and Postgres container
#
# Env overrides (all optional):
#   SYNTH_PLATFORM_DIR    path to a synth-platform checkout (default: ../synth-platform)
#   BUILD_DIR             this repo's CMake build dir (default: build)
#   INFERENCE_MODEL_ID    Ollama model tag for patch.generate (default: first of llama3.1 or
#                          whatever `ollama list` actually has pulled)
#   POLAR_WEBHOOK_SECRET  shared secret for the mock Polar webhook (default: local-dev-secret)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SYNTH_PLATFORM_DIR="$(cd "${SYNTH_PLATFORM_DIR:-$REPO_DIR/../synth-platform}" && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
API_URL="http://localhost:8787"
DB_URL="postgres://synth:synth@localhost:5433/synth"
POLAR_WEBHOOK_SECRET="${POLAR_WEBHOOK_SECRET:-local-dev-secret}"

if [ "${1:-}" = "--down" ]; then
    echo "==> stopping local synth-platform API server (if running on :8787)"
    lsof -ti:8787 | xargs -r kill 2>/dev/null || true
    echo "==> stopping local Postgres"
    (cd "$SYNTH_PLATFORM_DIR" && docker compose down -v)
    exit 0
fi

if [ ! -d "$SYNTH_PLATFORM_DIR" ]; then
    echo "synth-platform checkout not found at $SYNTH_PLATFORM_DIR -- set SYNTH_PLATFORM_DIR" >&2
    exit 1
fi

echo "==> starting local Postgres ($SYNTH_PLATFORM_DIR)"
(cd "$SYNTH_PLATFORM_DIR" && docker compose up -d db)

echo "==> waiting for Postgres to accept connections"
DB_CONTAINER="$(cd "$SYNTH_PLATFORM_DIR" && docker compose ps -q db)"
until docker exec "$DB_CONTAINER" pg_isready -U synth >/dev/null 2>&1; do
    sleep 1
done

echo "==> running migrations"
(cd "$SYNTH_PLATFORM_DIR" && DATABASE_URL="$DB_URL" pnpm --filter @platform/ops migrate)

if [ -z "${INFERENCE_MODEL_ID:-}" ]; then
    OLLAMA_TAGS="$(curl -s http://localhost:11434/api/tags 2>/dev/null || true)"
    # Prefer the documented default, then a small-and-fast pulled model, before falling back to
    # whatever's first -- an auto-picked 120B model makes every test round-trip painfully slow.
    for CANDIDATE in llama3.1 gpt-oss:20b; do
        if echo "$OLLAMA_TAGS" | grep -q "\"$CANDIDATE\""; then
            INFERENCE_MODEL_ID="$CANDIDATE"
            break
        fi
    done
    if [ -z "${INFERENCE_MODEL_ID:-}" ]; then
        INFERENCE_MODEL_ID="$(echo "$OLLAMA_TAGS" | python3 -c \
            'import json,sys; m=json.load(sys.stdin)["models"]; print(m[0]["name"] if m else "")' 2>/dev/null || true)"
        if [ -z "$INFERENCE_MODEL_ID" ]; then
            echo "No Ollama model found -- pull one first, e.g.: ollama pull llama3.1" >&2
            exit 1
        fi
        echo "    (no preferred model pulled -- using $INFERENCE_MODEL_ID instead, which may be slow)"
    fi
fi

echo "==> starting synth-platform API on $API_URL (model: $INFERENCE_MODEL_ID)"
(
    cd "$SYNTH_PLATFORM_DIR/apps/api" && \
    DATABASE_URL="$DB_URL" AUTH_STORE=postgres \
    POLAR_WEBHOOK_SECRET="$POLAR_WEBHOOK_SECRET" \
    INFERENCE_MODEL_ID="$INFERENCE_MODEL_ID" \
    pnpm dev
) &
API_PID=$!
# `pnpm dev` (tsx watch) spawns a child node process that outlives a plain `kill` of the subshell
# above -- kill whatever's actually bound to the port too, or the next run hits EADDRINUSE.
trap 'echo "==> stopping API server"; kill "$API_PID" 2>/dev/null || true; lsof -ti:8787 | xargs -r kill 2>/dev/null || true' EXIT

echo "==> waiting for the API to come up"
until curl -s -o /dev/null "$API_URL"; do
    sleep 1
done

case "$BUILD_DIR" in
    /*) APP_BUILD_DIR="$BUILD_DIR" ;;
    *) APP_BUILD_DIR="$REPO_DIR/$BUILD_DIR" ;;
esac
APP_BIN="$APP_BUILD_DIR/AgentSynth_artefacts/Debug/Agent Synth.app/Contents/MacOS/Agent Synth"
if [ ! -x "$APP_BIN" ]; then
    echo "Debug app not built at: $APP_BIN" >&2
    echo "Build it first: cmake -S . -B $BUILD_DIR -DENABLE_PLUGIN=OFF && cmake --build $BUILD_DIR --target AgentSynth" >&2
    exit 1
fi

cat <<EOF

==> launching AgentSynth.app against $API_URL
    In the app:
      1. Settings -> AI provider -> "Remote (hosted)", Host -> $API_URL
      2. Sign in -> a browser tab opens to a local dev sign-in page -- type any email, Continue, Approve
      3. To grant that account Pro (skip if you only need free-tier local history):
         POLAR_WEBHOOK_SECRET=$POLAR_WEBHOOK_SECRET (cd $SYNTH_PLATFORM_DIR && pnpm --filter @platform/ops polar-webhook --external-id <account-id-from-step-2>)
    Inspect what got saved:
      psql $DB_URL -c "select id, title, created_at from conversations;"

    Ctrl+C here stops the API server (Postgres keeps running -- './run-local-cloud-dev.sh --down' tears both down).

EOF

AGENTSYNTH_LOCAL_API_URL="$API_URL" "$APP_BIN"
