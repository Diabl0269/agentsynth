#!/usr/bin/env bash
#
# hold-test-slots.sh -- keeps every test run on this machine off while an app check or a recording uses
# the audio device. A running Tests binary can wedge the device.
#
# Takes every test slot (scripts/lib/test-shards.sh): running shards finish, new ones wait. Touches
# <got-file> once it holds them all, and lets go when <done-file> appears or after an hour.
#
# Usage: bash scripts/hold-test-slots.sh <got-file> <done-file>
# Never run the suite while holding them: it would wait for its own slots.

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/lib/test-shards.sh
source "$ROOT/scripts/lib/test-shards.sh"

got="$1"
done_file="$2"
next="${3:-0}"
wait_limit="${CI_LOCAL_TEST_SLOT_TIMEOUT:-3600}"
dir="$(test_slots_dir "$ROOT")"
mkdir -p "$dir"

if [ "$next" -ge "$(test_slots_total)" ]; then
    touch "$got"
    n=0
    until [ -f "$done_file" ] || [ "$n" -ge 1800 ]; do
        sleep 2
        n=$((n + 1))
    done
    exit 0
fi

case "$(test_slot_pick_tool)" in
    flock) exec flock -w "$wait_limit" "$dir/slot-$next" bash "$0" "$got" "$done_file" $((next + 1)) ;;
    lockf) exec lockf -s -t "$wait_limit" "$dir/slot-$next" bash "$0" "$got" "$done_file" $((next + 1)) ;;
    *) exec bash "$0" "$got" "$done_file" 999999 ;;
esac
