#!/usr/bin/env python3
"""Measures how much CPU the app burns while nobody touches it, the figure Activity Monitor shows.

Launches a test-mode build, optionally opens a project (a copy, so the original is never written), waits for the
load to settle, then reads the process's CPU seconds (ps) over a fixed window, several times. The result is the
average CPU percent over each window (100 = one core), the same unit as Activity Monitor's % CPU column.

    python3 Tools/ui-verify/idle_cpu.py --project "~/Music/AgentSynth/Load test.agsproj" --motion --visible
    python3 Tools/ui-verify/idle_cpu.py                       # a new, empty project
    python3 Tools/ui-verify/idle_cpu.py ... --sample out.txt  # also a 10 s `sample` call tree of the idle process
    python3 Tools/ui-verify/idle_cpu.py --table --project "~/Music/AgentSynth/Load test.agsproj"   # the claim table

--motion keeps Animations on (a test-mode launch otherwise switches them off); --visible shows the window, which is
what a user reads (a hidden window still paints, but the window server composites nothing). A test-mode launch
already renders through the silent device's own audio thread, so there is no pump flag.

--real launches the app the way a user does, with no test-mode server: the real audio device, plugin scan, update check
and a visible, focused window (only the keychain and the settings folder stay out of the user's way: --no-keychain and a
scratch --settings-dir). A test-mode launch differs in exactly those things, so --real is the number to compare with
Activity Monitor. --table runs both for the empty app and the named project, Animations On and Off, and flags any gap
over 25% between the two (a gap is a finding: the test-mode number alone is not an Activity Monitor claim)."""

import argparse
import json
import os
import shutil
import statistics
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


GAP_LIMIT = 0.25
SETTINGS_FILE = "Agent Synth.settings"


def app_binary(app_path):
    base = app_path or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build-ci-local",
                                    "AgentSynth_artefacts", "Release", "Agent Synth.app")
    return os.path.join(os.path.abspath(base), "Contents", "MacOS", "Agent Synth")


def animations_settings_xml(motion):
    """A settings file that pins the Animations setting: full when `motion`, else off (never `follow`)."""
    return ('<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n  <VALUE name="animationMode" val="%s"/>\n'
            '</PROPERTIES>\n' % ("full" if motion else "off"))


def gap_fraction(real, test):
    """How far the user-style number is above the test-mode number, as a fraction of the user-style number."""
    return 0.0 if real <= 0 else (real - test) / real


def table_markdown(rows):
    """rows: dicts with project, animations, real, test (median CPU percents of the repeats, so one busy window,
    such as a 97% outlier seen once, never decides a row). A gap over GAP_LIMIT is marked."""
    lines = ["| project | animations | user-style launch | test-mode launch | gap |", "|---|---|---|---|---|"]
    for row in rows:
        gap = gap_fraction(row["real"], row["test"])
        mark = "FINDING: test mode under-reports" if abs(gap) > GAP_LIMIT else "ok"
        lines.append("| %s | %s | %.1f%% | %.1f%% | %d%% (%s) |" % (row["project"], row["animations"], row["real"],
                                                                    row["test"], round(gap * 100), mark))
    return "\n".join(lines)


def prepare_project(args, scratch):
    if not args.project:
        return None, "new project"
    src = os.path.expanduser(args.project)
    dst = os.path.join(scratch, os.path.basename(src.rstrip("/")))
    shutil.copytree(src, dst)
    return dst, os.path.basename(src.rstrip("/")).replace(".agsproj", "")


def run_real(args, scratch, motion):
    """A user-style launch (real audio device, no test-mode server); the project opens from the command line."""
    settings = os.path.join(scratch, "settings")
    os.makedirs(settings, exist_ok=True)
    with open(os.path.join(settings, SETTINGS_FILE), "w", encoding="utf-8") as handle:
        handle.write(animations_settings_xml(motion))
    cmd = [app_binary(args.app), "--no-keychain", "--settings-dir", settings]
    dst, name = prepare_project(args, scratch)
    if dst:
        cmd.append(dst)
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(args.settle + (12.0 if dst else 4.0))
        readings = [round(measure(proc.pid, args.window), 1) for _ in range(args.repeats)]
        if args.sample:
            subprocess.run(["sample", str(proc.pid), "10", "-file", os.path.abspath(args.sample)], capture_output=True)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
    return result_dict(name, motion, readings, args.window, launch="real", visible=True)


def run_test_mode(args, scratch, motion):
    extra = ["--automation-motion"] if motion else []
    with TestModeApp(app_path=args.app, extra_args=extra, visible=args.visible or args.table) as app:
        dst, name = prepare_project(args, scratch)
        if dst:
            name = app.call("openProject", timeout_ms=120000, path=dst)["name"]
        try:
            app.call("waitForIdle", timeout_ms=30000, timeoutMs=20000, settleMs=500)
        except Exception:  # an animation that never settles (flow dots) is exactly what is measured
            pass
        time.sleep(args.settle)
        readings = [round(measure(app.pid, args.window), 1) for _ in range(args.repeats)]
        if args.sample:
            subprocess.run(["sample", str(app.pid), "10", "-file", os.path.abspath(args.sample)], capture_output=True)
        return result_dict(name, motion, readings, args.window, launch="test-mode", visible=args.visible or args.table)


def result_dict(name, motion, readings, window, launch, visible):
    return {"project": name, "launch": launch, "animations": "on" if motion else "off", "visible": visible,
            "windowSeconds": window, "cpuPercent": readings, "mean": round(sum(readings) / len(readings), 1),
            "median": round(statistics.median(readings), 1)}


def run_table(args):
    """Empty app and the named project, Animations On and Off, both launch styles; prints the table."""
    rows = []
    projects = [None] + ([args.project] if args.project else [])
    for project in projects:
        for motion in (True, False):
            args.project = project
            scratch = tempfile.mkdtemp(prefix="agentsynth-idle-cpu-")
            try:
                real = run_real(args, os.path.join(scratch, "real"), motion)
                test = run_test_mode(args, scratch, motion)
            finally:
                shutil.rmtree(scratch, ignore_errors=True)
            rows.append({"project": real["project"], "animations": real["animations"], "real": real["median"],
                         "test": test["median"]})
    print(table_markdown(rows))
    print(json.dumps(rows))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project", help="a .agsproj bundle; copied to a scratch folder first")
    ap.add_argument("--motion", action="store_true", help="Animations on")
    ap.add_argument("--visible", action="store_true", help="show the window")
    ap.add_argument("--real", action="store_true", help="launch like a user (real audio device, no test-mode server)")
    ap.add_argument("--table", action="store_true", help="empty app and --project, Animations On and Off, both launch "
                                                         "styles, with the gap between them")
    ap.add_argument("--settle", type=float, default=8.0, help="seconds to wait after the load before measuring")
    ap.add_argument("--window", type=float, default=10.0, help="seconds per measurement")
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--sample", help="write a 10 s `sample` call tree of the idle process to this file")
    ap.add_argument("--app", help="the Agent Synth.app to run (default: this worktree's build-ci-local)")
    args = ap.parse_args()
    if args.table:
        run_table(args)
        return
    scratch = tempfile.mkdtemp(prefix="agentsynth-idle-cpu-")
    try:
        run = run_real if args.real else run_test_mode
        print(json.dumps(run(args, scratch, args.motion)))
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    main()
