#!/usr/bin/env python3
"""Measures how much CPU the app burns while nobody touches it, the figure Activity Monitor shows.

Launches a test-mode build, optionally opens a project (a copy, so the original is never written), waits for the
load to settle, then reads the process's CPU seconds (ps) over a fixed window, several times. The result is the
average CPU percent over each window (100 = one core), the same unit as Activity Monitor's % CPU column.

    python3 Tools/ui-verify/idle_cpu.py --project "~/Music/AgentSynth/Load test.agsproj" --motion --visible
    python3 Tools/ui-verify/idle_cpu.py                       # a new, empty project
    python3 Tools/ui-verify/idle_cpu.py ... --sample out.txt  # also a 10 s `sample` call tree of the idle process

--motion keeps Animations on (a test-mode launch otherwise switches them off); --visible shows the window, which is
what a user reads (a hidden window still paints, but the window server composites nothing); --pump renders the
project in real time with no device, standing in for the audio thread a real launch runs."""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agentsynth_testmode import TestModeApp  # noqa: E402


def cpu_seconds(pid):
    """The process's total user+system CPU time, from ps (`[[dd-]hh:]mm:ss.cc`)."""
    out = subprocess.run(["ps", "-o", "time=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
    days = 0
    if "-" in out:
        d, out = out.split("-", 1)
        days = int(d)
    parts = [float(p) for p in out.split(":")]
    secs = 0.0
    for p in parts:
        secs = secs * 60 + p
    return days * 86400 + secs


def measure(pid, window_s):
    start_cpu, start = cpu_seconds(pid), time.monotonic()
    time.sleep(window_s)
    return 100.0 * (cpu_seconds(pid) - start_cpu) / (time.monotonic() - start)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project", help="a .agsproj bundle; copied to a scratch folder first")
    ap.add_argument("--motion", action="store_true", help="Animations on")
    ap.add_argument("--visible", action="store_true", help="show the window")
    ap.add_argument("--pump", action="store_true", help="render audio in real time (no device)")
    ap.add_argument("--settle", type=float, default=8.0, help="seconds to wait after the load before measuring")
    ap.add_argument("--window", type=float, default=10.0, help="seconds per measurement")
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--sample", help="write a 10 s `sample` call tree of the idle process to this file")
    ap.add_argument("--app", help="the Agent Synth.app to run (default: this worktree's build-ci-local)")
    args = ap.parse_args()

    scratch = tempfile.mkdtemp(prefix="agentsynth-idle-cpu-")
    extra = ["--automation-motion"] if args.motion else []
    try:
        with TestModeApp(app_path=args.app, extra_args=extra, visible=args.visible) as app:
            name = "new project"
            if args.project:
                src = os.path.expanduser(args.project)
                dst = os.path.join(scratch, os.path.basename(src.rstrip("/")))
                shutil.copytree(src, dst)
                name = app.call("openProject", timeout_ms=120000, path=dst)["name"]
            if args.pump:
                app.call("audioPump", run=True)
            try:
                app.call("waitForIdle", timeout_ms=30000, timeoutMs=20000, settleMs=500)
            except Exception:  # an animation that never settles (flow dots) is exactly what is measured
                pass
            time.sleep(args.settle)
            readings = [round(measure(app.pid, args.window), 1) for _ in range(args.repeats)]
            if args.sample:
                subprocess.run(["sample", str(app.pid), "10", "-file", os.path.abspath(args.sample)],
                               capture_output=True)
            result = {
                "project": name,
                "animations": "on" if args.motion else "off",
                "visible": args.visible,
                "pump": args.pump,
                "windowSeconds": args.window,
                "cpuPercent": readings,
                "mean": round(sum(readings) / len(readings), 1),
            }
            print(json.dumps(result))
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    main()
