#!/usr/bin/env bash
#
# check-prompt-pins.sh -- the app may only pin a system-prompt version the live service already serves. A build that
# pins a newer one reaches users before the deploy and its AI requests fail until the service catches up.
#
# The service has no version-list endpoint, so each capability is probed with a deliberately unknown version
# (9999): it answers "Unknown <capability> system prompt version: 9999. Known versions: 1, 2, ... N" and N is live.
# The pins are read from source (one constant per capability, table below).
#
# Usage: bash scripts/check-prompt-pins.sh
# Environment: PROMPT_PINS_API_BASE overrides the service URL (default: kApiBaseUrl in Source/Branding.h);
#   PROMPT_PINS_SRC overrides the repo root that holds Source/ (script tests).
# Exit: 0 every pin is served (or the service could not be reached/read: a ::warning:: is printed and the check is
#   skipped), 1 a pin is ahead of the live service, 2 a pin could not be read from source.

set -uo pipefail

ROOT="${PROMPT_PINS_SRC:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"

# capability | source file | constant
PINS=(
    "project.generate|Source/AI/AIIntegrationService/AIIntegrationServiceProjectEdit.cpp|kProjectGeneratePromptVersion"
    "patch.generate|Source/AI/RemoteProvider.cpp|kPatchGeneratePromptVersion"
    "timeline.generate|Source/AI/AIIntegrationService/AIIntegrationServiceRequestSending.cpp|kTimelineGeneratePromptVersion"
)

BASE="${PROMPT_PINS_API_BASE:-$(sed -n 's/.*kApiBaseUrl *= *"\([^"]*\)".*/\1/p' "$ROOT/Source/Branding.h" | head -1)}"
[ -n "$BASE" ] || { echo "check-prompt-pins: FAIL cannot read kApiBaseUrl from Source/Branding.h" >&2; exit 2; }

# Probe body: every field any capability's input schema requires; unknown fields are ignored by the service.
PROBE='{"productName":"prompt-pin-probe","userPrompt":"probe","arrangementContext":"","paramTargets":[],"availableTracks":[],"promptVersion":9999}'

ahead=0
for entry in "${PINS[@]}"; do
    IFS='|' read -r capability file constant <<<"$entry"
    pin=$(sed -n "s/.*constexpr int ${constant} *= *\([0-9][0-9]*\);.*/\1/p" "$ROOT/$file" | head -1)
    if [ -z "$pin" ]; then
        echo "check-prompt-pins: FAIL cannot read $constant from $file" >&2
        exit 2
    fi
    reply=$(curl -s -m 30 -X POST "$BASE/v1/capability/$capability" -H 'Content-Type: application/json' \
        -H 'X-Device-Id: ci-prompt-pin-probe' -d "$PROBE") || reply=""
    # Every integer after "Known versions:" is a known version, so the comma list and an "A to B" range both work.
    known=$(printf '%s' "$reply" | sed -n 's/.*Known versions:\([^"]*\).*/\1/p' | grep -oE '[0-9]+' | sort -n | tail -1)
    if [ -z "$known" ]; then
        echo "::warning::check-prompt-pins: could not read the live $capability versions from $BASE (skipped)"
        continue
    fi
    if [ "$pin" -gt "$known" ]; then
        echo "check-prompt-pins: FAIL $capability pins prompt version $pin but the live service only knows up to $known" >&2
        ahead=1
    else
        echo "check-prompt-pins: ok $capability pins $pin, live knows up to $known"
    fi
done

if [ "$ahead" -ne 0 ]; then
    echo "Deploy the service's newer prompt first, or lower the pin; a build pinning an undeployed version cannot answer AI requests." >&2
    exit 1
fi
exit 0
