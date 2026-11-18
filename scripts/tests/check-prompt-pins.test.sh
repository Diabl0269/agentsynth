#!/usr/bin/env bash
#
# Unit tests for scripts/check-prompt-pins.sh with a stub `curl` (no network): pins at or below the live versions pass,
# a pin ahead fails, both reply spellings parse, and an unreachable or unreadable service only warns.
#
# Usage: bash scripts/tests/check-prompt-pins.test.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECK="$SCRIPT_DIR/scripts/check-prompt-pins.sh"
TMPROOT="$(mktemp -d "${TMPDIR:-/tmp}/prompt-pins-test.XXXXXX")"
trap 'rm -rf "$TMPROOT"' EXIT

SRC="$TMPROOT/src"
mkdir -p "$TMPROOT/bin" "$SRC/Source/AI/AIIntegrationService"
printf 'constexpr const char* kApiBaseUrl = "https://example.invalid";\n' >"$SRC/Source/Branding.h"

write_pins() { # write_pins <project> <patch> <timeline>
    printf 'static constexpr int kProjectGeneratePromptVersion = %s;\n' "$1" \
        >"$SRC/Source/AI/AIIntegrationService/AIIntegrationServiceProjectEdit.cpp"
    printf 'constexpr int kPatchGeneratePromptVersion = %s;\n' "$2" >"$SRC/Source/AI/RemoteProvider.cpp"
    printf 'static constexpr int kTimelineGeneratePromptVersion = %s;\n' "$3" \
        >"$SRC/Source/AI/AIIntegrationService/AIIntegrationServiceRequestSending.cpp"
}

# The stub answers from $STUB_REPLY (project), $STUB_PATCH, $STUB_TIMELINE by the capability in the URL, or fails.
cat >"$TMPROOT/bin/curl" <<'STUB'
#!/usr/bin/env bash
[ -n "${STUB_FAIL:-}" ] && exit 6
for a in "$@"; do
    case "$a" in
    */project.generate) printf '%s' "$STUB_PROJECT" ;;
    */patch.generate) printf '%s' "$STUB_PATCH" ;;
    */timeline.generate) printf '%s' "$STUB_TIMELINE" ;;
    esac
done
STUB
chmod +x "$TMPROOT/bin/curl"

reply() { printf '{"error":{"message":"Unknown x system prompt version: 9999. Known versions: %s"}}' "$1"; }

pass=0
fail=0
expect() { # expect <wanted exit> <name> [grep pattern wanted in output]
    local want="$1" name="$2" pattern="${3:-}" got out
    out=$(PATH="$TMPROOT/bin:$PATH" PROMPT_PINS_SRC="$SRC" bash "$CHECK" 2>&1)
    got=$?
    if [ "$got" -eq "$want" ] && { [ -z "$pattern" ] || grep -q "$pattern" <<<"$out"; }; then
        pass=$((pass + 1)); echo "PASS: $name"
    else
        fail=$((fail + 1)); echo "FAIL: $name (exit $got, wanted $want)"; printf '%s\n' "$out"
    fi
}

export STUB_PROJECT="$(reply '1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12')" STUB_PATCH="$(reply '1, 2, 3')" STUB_TIMELINE="$(reply '1, 2')"
write_pins 12 3 2; expect 0 "pins equal to the live versions pass"
write_pins 11 3 1; expect 0 "pins below the live versions pass"
write_pins 13 3 2; expect 1 "a project pin ahead of live fails" "project.generate pins prompt version 13"
write_pins 12 4 2; expect 1 "a patch pin ahead of live fails" "patch.generate"
write_pins 12 3 3; expect 1 "a timeline pin ahead of live fails" "timeline.generate"
STUB_PROJECT="$(reply '1 to 11')"; export STUB_PROJECT
write_pins 12 3 2; expect 1 "the A to B spelling is read" "only knows up to 11"
write_pins 11 3 2; expect 0 "the A to B spelling passes when the pin is served"
STUB_PROJECT='{"error":{"message":"boom"}}'; export STUB_PROJECT
write_pins 99 3 2; expect 0 "an unreadable reply warns and skips" "::warning::"
STUB_FAIL=1 expect 0 "an unreachable service warns and skips" "::warning::"
unset STUB_FAIL
echo 'nothing here' >"$SRC/Source/AI/RemoteProvider.cpp"; expect 2 "a pin missing from source is an error"

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
