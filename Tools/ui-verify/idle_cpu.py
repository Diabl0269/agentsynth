#!/usr/bin/env python3
"""Measures how much CPU the app burns, idle and playing, the figure Activity Monitor shows, split per thread.

Launches a build, optionally opens a project (a copy, so the original is never written), waits for the load to settle,
then reads the process's CPU seconds (ps) over a fixed window, several times. The result is the average CPU percent over
each window (100 = one core), the same unit as Activity Monitor's % CPU column, plus the same window split per thread
(`ps -M`: the audio device thread, the main thread, everything else; see cpu_threads.py).

    python3 Tools/ui-verify/idle_cpu.py --project "~/Music/AgentSynth/Load test.agsproj" --motion --visible
    python3 Tools/ui-verify/idle_cpu.py                       # the app with no project
    python3 Tools/ui-verify/idle_cpu.py ... --sample out.txt  # also a 10 s `sample` call tree of the idle process
    python3 Tools/ui-verify/idle_cpu.py --real --play --buffer 128 --project ...   # user-style, idle then playing
    python3 Tools/ui-verify/idle_cpu.py --table --project "~/Music/AgentSynth/Load test.agsproj"   # the claim table

--motion keeps Animations on (a test-mode launch otherwise switches them off); --visible shows the window, which is
what a user reads (a hidden window still paints, but the window server composites nothing). A test-mode launch
already renders through the silent device's own audio thread, so there is no pump flag.

--real launches the app the way a user does, with no test-mode server: the real audio device, plugin scan, update check
and a visible, focused window (only the keychain and the settings folder stay out of the user's way: --no-keychain and a
scratch --settings-dir). A test-mode launch differs in exactly those things, so --real is the number to compare with
Activity Monitor. --buffer asks the default output device for that buffer size (0: the device's own default); the
buffer the device really opened at is read back from the settings file the app writes and logged. --play then presses
Play in the app (the app's own --play-after flag, so nothing drives the keyboard) and measures the same windows again
with the transport running; the Mac's output is muted while it plays and restored after (--audible keeps it).

--table runs the empty app, a new project and --project, Animations On and Off, at every --buffers size, idle and
playing (user-style), next to an idle test-mode launch, and flags any idle gap over 25% between the two. With
--bench-idle-us / --bench-playing-us (the mean block of Tests/App/ModuleCpuProfileTests.cpp's bench, 512 samples at
48 kHz) each --project row also says how many of its Activity Monitor points the engine bench explains."""

import argparse
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cpu_threads  # noqa: E402
from agentsynth_testmode import TestModeApp  # noqa: E402


def cpu_seconds(pid):
    """The process's total user+system CPU time, from ps (`[[dd-]hh:]mm:ss.cc`)."""
    out = subprocess.run(["ps", "-o", "time=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
    return cpu_threads.clock_seconds(out)


def measure(pid, window_s):
    """(% CPU of the whole process, {audio, main, other} % per thread role) over one window."""
    start_threads, start_cpu, start = cpu_threads.thread_times(pid), cpu_seconds(pid), time.monotonic()
    time.sleep(window_s)
    elapsed = time.monotonic() - start
    total = 100.0 * (cpu_seconds(pid) - start_cpu) / elapsed
    return round(total, 1), cpu_threads.split_between(start_threads, cpu_threads.thread_times(pid), elapsed)


GAP_LIMIT = 0.25
SETTINGS_FILE = "Agent Synth.settings"
ROLES = ("audio", "main", "other")


def app_binary(app_path):
    base = app_path or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build-ci-local",
                                    "AgentSynth_artefacts", "Release", "Agent Synth.app")
    return os.path.join(os.path.abspath(base), "Contents", "MacOS", "Agent Synth")


def device_setup_xml(device, buffer):
    """A CoreAudio DEVICESETUP for `device`'s output at `buffer` samples (0 leaves the device's default buffer)."""
    if not device:
        return ""
    attrs = 'deviceType="CoreAudio" audioOutputDeviceName="%s"' % device.replace('"', "&quot;")
    if buffer:
        attrs += ' audioDeviceBufferSize="%d"' % buffer
    return '<DEVICESETUP %s/>' % attrs


def animations_settings_xml(motion, device_setup=""):
    """A settings file that pins the Animations setting (full when `motion`, else off, never `follow`) and, when
    given, the audio device setup."""
    device = ""
    if device_setup:
        device = '  <VALUE name="audioDeviceState">\n    %s\n  </VALUE>\n' % device_setup
    return ('<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n  <VALUE name="animationMode" val="%s"/>\n%s'
            '</PROPERTIES>\n' % ("full" if motion else "off", device))


def default_output_device(run=subprocess.run):
    """The name of the Mac's default output device (system_profiler), or None."""
    try:
        text = run(["/usr/sbin/system_profiler", "SPAudioDataType"], capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    name = None
    for line in (text or "").splitlines():
        heading = re.match(r"^ {8}(\S.*):$", line)
        if heading:
            name = heading.group(1)
        elif "Default Output Device: Yes" in line:
            return name
    return None


def opened_device(settings_text):
    """(buffer, rate) of the device setup the app wrote back to its settings file, or (None, None)."""
    found = re.search(r'name="audioDeviceState".*?<DEVICESETUP([^>]*)/>', settings_text or "", re.S)
    if not found:
        return None, None
    buffer = re.search(r'audioDeviceBufferSize="(\d+)"', found.group(1))
    rate = re.search(r'audioDeviceRate="([\d.]+)"', found.group(1))
    return (int(buffer.group(1)) if buffer else None), (float(rate.group(1)) if rate else None)


def gap_fraction(real, test):
    """How far the user-style number is above the test-mode number, as a fraction of the user-style number."""
    return 0.0 if real <= 0 else (real - test) / real


def bench_share_percent(bench_us, block=512, rate=48000.0):
    """The bench's mean block time as % of one core: block time over the block's real-time length."""
    return 100.0 * bench_us / (block / rate * 1e6)


def bench_line(bench, real):
    """`5.2 of 27.0 (19%)`: how many of the Activity Monitor points the engine bench explains."""
    if bench is None or real <= 0:
        return "-"
    return "%.1f of %.1f (%d%%)" % (bench, real, round(100.0 * bench / real))


def table_markdown(rows):
    """rows: dicts with project, animations, buffer, state, real, split, test, bench (median CPU percents of the repeats,
    so one busy window, such as a 97% outlier seen once, never decides a row). test and bench may be None. An idle gap
    over GAP_LIMIT is marked."""
    lines = ["| project | animations | buffer | state | user-style (Activity Monitor) | audio thread | main thread | "
             "other threads | test-mode launch | gap | engine bench explains |",
             "|---|---|---|---|---|---|---|---|---|---|---|"]
    for row in rows:
        split = row.get("split") or {}
        test, gap_text = row.get("test"), "-"
        if test is not None:
            gap = gap_fraction(row["real"], test)
            gap_text = "%d%% (%s)" % (round(gap * 100), "FINDING: test mode under-reports" if abs(gap) > GAP_LIMIT else "ok")
        lines.append("| %s | %s | %s | %s | %.1f%% | %s | %s | %s | %s | %s | %s |" % (
            row["project"], row["animations"], row.get("buffer", "-"), row.get("state", "idle"), row["real"],
            *("%.1f%%" % split[r] if r in split else "-" for r in ROLES), "-" if test is None else "%.1f%%" % test,
            gap_text, bench_line(row.get("bench"), row["real"])))
    return "\n".join(lines)


def prepare_project(project, scratch):
    if not project:
        return None, "empty app"
    src = os.path.expanduser(project)
    dst = os.path.join(scratch, os.path.basename(src.rstrip("/")))
    shutil.copytree(src, dst)
    return dst, os.path.basename(src.rstrip("/")).replace(".agsproj", "")


def output_muted(run=subprocess.run):
    out = run(["osascript", "-e", "output muted of (get volume settings)"], capture_output=True, text=True).stdout
    return out.strip() == "true"


def set_output_muted(muted, run=subprocess.run):
    run(["osascript", "-e", "set volume output muted %s" % ("true" if muted else "false")], capture_output=True)


def readings_summary(readings):
    """Median total and median per-role split of a list of (total, split) windows."""
    totals = [total for total, _ in readings]
    return {"cpuPercent": totals, "median": round(statistics.median(totals), 1),
            "split": {r: round(statistics.median([s[r] for _, s in readings]), 1) for r in ROLES}}


def take_sample(pid, path):
    """A 10 s `sample` call tree to `path`, summarised per thread role with the audio and main threads' top functions."""
    subprocess.run(["sample", str(pid), "10", "-file", os.path.abspath(path)], capture_output=True)
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            threads = cpu_threads.parse_sample(handle.read())
    except OSError:
        return None
    return {"file": path, "split": cpu_threads.sample_split(threads),
            "audioTop": cpu_threads.top_self(threads, "audio"), "mainTop": cpu_threads.top_self(threads, "main")}


def window(pid, args, sample_path):
    result = readings_summary([measure(pid, args.window) for _ in range(args.repeats)])
    if sample_path:
        result["sample"] = take_sample(pid, sample_path)
    return result


def sample_path_for(args, name, motion, buffer, state):
    if not args.sample:
        return None
    base = args.sample[:-4] if args.sample.endswith(".txt") else args.sample
    if not args.table and state == "idle":
        return args.sample
    return "%s-%s-%s-%s-%s.txt" % (base, name.replace(" ", "_"), "on" if motion else "off", buffer or "default", state)


PLAY_SETTLE_SECONDS = 3.0


def play_after_seconds(load_wait, repeats, window_s, sampled):
    """When the app should press Play (--play-after): after the load and every idle window (and the idle sample), with
    a margin for ps and launch jitter."""
    return load_wait + repeats * (window_s + 0.5) + (12.0 if sampled else 0.0) + 5.0


def run_real(args, scratch, motion, project=None, buffer=None):
    """A user-style launch (real audio device, no test-mode server); the project opens from the command line. Idle,
    then (with --play) the same windows again while the transport runs."""
    buffer = args.buffer if buffer is None else buffer
    settings = os.path.join(scratch, "settings")
    os.makedirs(settings, exist_ok=True)
    settings_file = os.path.join(settings, SETTINGS_FILE)
    device = args.device or default_output_device()
    with open(settings_file, "w", encoding="utf-8") as handle:
        handle.write(animations_settings_xml(motion, device_setup_xml(device, buffer)))
    cmd = [app_binary(args.app), "--no-keychain", "--settings-dir", settings]
    dst, name = prepare_project(project, scratch)
    load_wait = args.settle + (12.0 if dst else 4.0)
    sample_idle = sample_path_for(args, name, motion, buffer, "idle")
    play_at = play_after_seconds(load_wait, args.repeats, args.window, bool(sample_idle))
    if args.play:
        cmd += ["--play-after", "%.1f" % play_at]
    if dst:
        cmd.append(dst)
    was_muted = None
    if args.play and not args.audible:
        was_muted = output_muted()
        set_output_muted(True)
    launched = time.monotonic()
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    result = {"project": name, "launch": "real", "animations": "on" if motion else "off", "visible": True,
              "windowSeconds": args.window, "device": device, "askedBuffer": buffer or "default"}
    try:
        time.sleep(load_wait)
        result["idle"] = window(proc.pid, args, sample_idle)
        if args.play:
            time.sleep(max(0.0, launched + play_at + PLAY_SETTLE_SECONDS - time.monotonic()))
            result["playing"] = window(proc.pid, args, sample_path_for(args, name, motion, buffer, "playing"))
    finally:
        if was_muted is not None:
            set_output_muted(was_muted)
        proc.terminate()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
    with open(settings_file, encoding="utf-8", errors="replace") as handle:
        result["openedBuffer"], result["openedRate"] = opened_device(handle.read())
    result["mean"], result["median"] = (round(sum(result["idle"]["cpuPercent"]) / args.repeats, 1),
                                        result["idle"]["median"])
    return result


def run_test_mode(args, scratch, motion, project=None):
    extra = ["--automation-motion"] if motion else []
    with TestModeApp(app_path=args.app, extra_args=extra, visible=args.visible or args.table) as app:
        dst, name = prepare_project(project, scratch)
        if dst:
            name = app.call("openProject", timeout_ms=120000, path=dst)["name"]
        try:
            app.call("waitForIdle", timeout_ms=30000, timeoutMs=20000, settleMs=500)
        except Exception:  # an animation that never settles (flow dots) is exactly what is measured
            pass
        time.sleep(args.settle)
        idle = window(app.pid, args, None if args.table else args.sample)
        return {"project": name, "launch": "test-mode", "animations": "on" if motion else "off",
                "visible": args.visible or args.table, "windowSeconds": args.window, "idle": idle,
                "median": idle["median"]}


def make_new_project(args, scratch):
    """A new, empty project saved by a test-mode launch: what File > New gives a user."""
    path = os.path.join(scratch, "new project.agsproj")
    with TestModeApp(app_path=args.app) as app:
        app.call("waitForIdle", timeout_ms=30000, timeoutMs=20000, settleMs=500)
        app.call("saveProject", timeout_ms=60000, path=path)
    return path


def buffer_label(real):
    opened = real.get("openedBuffer")
    asked = real.get("askedBuffer")
    return "%s (asked %s)" % (opened if opened else "?", asked)


def table_rows(real, test, bench):
    """One idle and (when measured) one playing row of a launch; bench is {"idle": %, "playing": %} or None."""
    rows = []
    for state in ("idle", "playing"):
        if state not in real:
            continue
        rows.append({"project": real["project"], "animations": real["animations"], "buffer": buffer_label(real),
                     "state": state, "real": real[state]["median"], "split": real[state]["split"],
                     "test": test["median"] if (test and state == "idle") else None,
                     "bench": bench.get(state) if bench else None})
    return rows


def run_table(args):
    """The empty app, a new project and --project; Animations On and Off; every --buffers size; idle and playing."""
    rows, raw = [], []
    keep = tempfile.mkdtemp(prefix="agentsynth-idle-cpu-new-")
    named = args.project
    projects = [None, make_new_project(args, keep)] + ([named] if named else [])
    bench = None
    if args.bench_idle_us or args.bench_playing_us:
        bench = {k: round(bench_share_percent(v), 1) for k, v in
                 (("idle", args.bench_idle_us), ("playing", args.bench_playing_us)) if v}
    buffers = [int(b) for b in args.buffers.split(",")]
    try:
        for project in projects:
            for motion in (True, False):
                test = None
                for index, buffer in enumerate(buffers):
                    scratch = tempfile.mkdtemp(prefix="agentsynth-idle-cpu-")
                    try:
                        real = run_real(args, os.path.join(scratch, "real"), motion, project, buffer)
                        if index == 0:
                            test = run_test_mode(args, scratch, motion, project)
                    finally:
                        shutil.rmtree(scratch, ignore_errors=True)
                    raw.append({"real": real, "test": test if index == 0 else None})
                    added = table_rows(real, test if index == 0 else None,
                                       bench if project and project == named else None)
                    rows.extend(added)
                    print("\n".join(table_markdown(added).splitlines()[2:]), file=sys.stderr, flush=True)
    finally:
        shutil.rmtree(keep, ignore_errors=True)
    print(table_markdown(rows))
    print(json.dumps({"rows": rows, "runs": raw}))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project", help="a .agsproj bundle; copied to a scratch folder first")
    ap.add_argument("--motion", action="store_true", help="Animations on")
    ap.add_argument("--visible", action="store_true", help="show the window")
    ap.add_argument("--real", action="store_true", help="launch like a user (real audio device, no test-mode server)")
    ap.add_argument("--play", action="store_true", help="--real: after the idle windows, press Space and measure again")
    ap.add_argument("--audible", action="store_true", help="--play: leave the Mac's output unmuted")
    ap.add_argument("--buffer", type=int, default=0, help="--real: device buffer size to ask for (0: device default)")
    ap.add_argument("--buffers", default="0,512,128", help="--table: comma-separated buffer sizes (0: device default)")
    ap.add_argument("--device", help="--real: the output device to open (default: the Mac's default output)")
    ap.add_argument("--table", action="store_true", help="empty app, new project and --project, Animations On and Off, "
                                                         "every --buffers size, idle and playing, with the test-mode gap")
    ap.add_argument("--bench-idle-us", type=float, help="--table: the bench's mean idle block (us, 512 @ 48 kHz)")
    ap.add_argument("--bench-playing-us", type=float, help="--table: the bench's mean playing block (us)")
    ap.add_argument("--settle", type=float, default=8.0, help="seconds to wait after the load before measuring")
    ap.add_argument("--window", type=float, default=10.0, help="seconds per measurement")
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--sample", help="write a 10 s `sample` call tree of the idle process to this file (with --table "
                                     "or --play, one file per launch and state, named from it)")
    ap.add_argument("--app", help="the Agent Synth.app to run (default: this worktree's build-ci-local)")
    args = ap.parse_args()
    if args.table:
        args.play = True
        run_table(args)
        return
    scratch = tempfile.mkdtemp(prefix="agentsynth-idle-cpu-")
    try:
        if args.real:
            print(json.dumps(run_real(args, scratch, args.motion, args.project)))
        else:
            print(json.dumps(run_test_mode(args, scratch, args.motion, args.project)))
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    main()
