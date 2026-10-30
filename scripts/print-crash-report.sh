#!/usr/bin/env bash
#
# print-crash-report.sh -- after a test shard crashes on macOS, prints the crashed thread and the main
# thread of each crash report the system wrote for the Tests binary, so a CI log shows where it crashed.
# Prints nothing elsewhere or when no report exists. Never fails the caller.
#
# Usage: bash scripts/print-crash-report.sh

set -uo pipefail
reports="$HOME/Library/Logs/DiagnosticReports"
[ -d "$reports" ] || exit 0
# ReportCrash writes the file a moment after the process dies.
sleep 10
python3 - "$reports" <<'PY' || true
import glob, json, os, sys

def show(thread, images, title):
    print(f"--- {title}")
    for frame in thread.get("frames", [])[:30]:
        image = images[frame["imageIndex"]].get("name", "?") if "imageIndex" in frame else "?"
        print(f"  {image}  {frame.get('symbol', '?')}")

for path in sorted(glob.glob(os.path.join(sys.argv[1], "Tests*.ips")), key=os.path.getmtime):
    try:
        text = open(path).read()
        report = json.loads(text[text.index("\n") + 1:])
        print(f"=== {os.path.basename(path)}: {report.get('exception', {})}")
        threads, images = report["threads"], report["usedImages"]
        show(threads[report["faultingThread"]], images, f"crashed thread {report['faultingThread']}")
        if report["faultingThread"] != 0:
            show(threads[0], images, "main thread")
    except Exception as error:
        print(f"could not read {path}: {error}")
PY
exit 0
