#!/usr/bin/env python3
"""Starts two test-mode instances at once (separate settings directories, OS-picked ports), runs select_card's steps in
both concurrently and checks that both pass and each wrote its own PNG.

    python3 Tools/ui-verify/examples/parallel.py [--app <path to Agent Synth.app>] [--out <dir>]
"""

import argparse
import os
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
from agentsynth_testmode import TestModeApp  # noqa: E402
from select_card import select_card  # noqa: E402


def run_one(index, app_path, out_dir, results):
    try:
        os.makedirs(out_dir, exist_ok=True)
        with TestModeApp(app_path) as app:
            print("instance %d: pid %d port %d settings %s" % (index, app.pid, app.port, app.settings_dir), flush=True)
            results[index] = {"png": select_card(app, out_dir), "port": app.port, "settings": app.settings_dir}
    except Exception as exc:  # reported below, with the instance number
        results[index] = {"error": repr(exc)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app")
    parser.add_argument("--out", default=None, help="parent of the per-instance output directories")
    options = parser.parse_args()
    out_root = os.path.abspath(options.out) if options.out else tempfile.mkdtemp(prefix="agentsynth-parallel-")
    results = {}
    threads = [
        threading.Thread(target=run_one, args=(i, options.app, os.path.join(out_root, "instance%d" % i), results))
        for i in (1, 2)
    ]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    failures = [i for i, r in sorted(results.items()) if "error" in r]
    for i in failures:
        print("FAIL instance %d: %s" % (i, results[i]["error"]))
    if failures or len(results) != 2:
        sys.exit(1)
    assert results[1]["port"] != results[2]["port"], "both instances share a port"
    assert results[1]["settings"] != results[2]["settings"], "both instances share a settings directory"
    assert results[1]["png"] != results[2]["png"]
    for r in results.values():
        assert os.path.getsize(r["png"]) > 0
    print("PASS parallel: 2 instances, ports %d and %d, PNGs:" % (results[1]["port"], results[2]["port"]))
    for i in (1, 2):
        print("  ", results[i]["png"])


if __name__ == "__main__":
    main()
