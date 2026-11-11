#!/usr/bin/env python3
"""Holds each kind of canvas drag at the right edge of the window and checks that the canvas scrolls, that moving back
inward stops it and that the release ends it: a module card, a folded macro card, an expanded macro's name chip, a
cable and a selection box. Writes a PNG per gesture (held at the edge) and prints one line per gesture.

    python3 Tools/ui-verify/examples/edge_scroll_drags.py [--app <path to Agent Synth.app>] [--out <dir>] [--visible]

Test mode turns animations off, but the edge scroll runs on its own 30 Hz timer, so it needs no `--automation-motion`.
"""

import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from agentsynth_testmode import TestModeApp  # noqa: E402

FIXTURE = os.path.join(os.path.dirname(HERE), "fixtures", "macros.agsproj")
REFERENCE = "card:Audio Output"  # a card no gesture below drags sideways: its x only changes with the pan
EDGE_X = 1597
EDGE_Y = 600


def x_of(app, card_id):
    return app.call("getState", id=card_id)["bounds"]["x"]


# name -> (press id, press x, press y, mods, vertical wobble so a box has an area)
GESTURES = [
    ("module-card", "card:Oscillator 2", 140, 12, None),
    ("macro-card", "macroCard", 140, 70, None),
    ("macro-name-chip", "card:LFO 3", -30, -8, None),
    ("cable", "card:Oscillator 2", 279, 76, None),
    ("selection-box", "window", 520, 780, "shift")  # clear of the minimap, bottom left,
]


def run_one(name, press_id, px, py, mods, app_path, out_dir, visible):
    extra = {"mods": mods} if mods else {}
    with TestModeApp(app_path, visible=visible) as app:
        app.call("openProject", path=FIXTURE)
        app.call("waitForIdle")
        for _ in range(3):
            app.call("key", keys="cmd+-")  # zoomed out: the whole patch fits, so only the pan moves the reference
        app.call("waitForIdle")
        before = x_of(app, REFERENCE)
        app.call("mouseDown", id=press_id, x=px, y=py, **extra)
        for x in (900, 1300, EDGE_X):
            app.call("mouseMoveHeld", id="window", x=x, y=EDGE_Y, **extra)
        time.sleep(0.8)
        held = x_of(app, REFERENCE)
        app.call("screenshot", id="window", path=os.path.join(out_dir, "%s-held-at-edge.png" % name))
        assert held < before - 100, "%s: the canvas did not scroll (%d -> %d)" % (name, before, held)
        # Back inward: the scroll stops.
        app.call("mouseMoveHeld", id="window", x=900, y=EDGE_Y, **extra)
        time.sleep(0.2)
        stopped = x_of(app, REFERENCE)
        time.sleep(0.5)
        assert x_of(app, REFERENCE) == stopped, "%s: kept scrolling after the pointer moved back" % name
        app.call("screenshot", id="window", path=os.path.join(out_dir, "%s-moved-back.png" % name))
        # Held at the edge again, then released: the scroll ends with the drag.
        app.call("mouseMoveHeld", id="window", x=EDGE_X, y=EDGE_Y, **extra)
        time.sleep(0.4)
        app.call("mouseUp", id="window", x=EDGE_X, y=EDGE_Y, **extra)
        time.sleep(0.2)
        ended = x_of(app, REFERENCE)
        time.sleep(0.5)
        assert x_of(app, REFERENCE) == ended, "%s: kept scrolling after the release" % name
        print("PASS %-16s reference card x %d -> %d (held) -> %d (moved back) -> %d (released)" % (
            name, before, held, stopped, ended), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", help="path to Agent Synth.app (default: build-ci-local)")
    parser.add_argument("--out", default=os.path.join(os.path.dirname(HERE), "out"), help="where the PNGs go")
    parser.add_argument("--visible", action="store_true", help="show the app window (default: hidden)")
    options = parser.parse_args()
    out_dir = os.path.abspath(options.out)
    os.makedirs(out_dir, exist_ok=True)
    for name, press_id, px, py, mods in GESTURES:
        run_one(name, press_id, px, py, mods, options.app, out_dir, options.visible)


if __name__ == "__main__":
    main()
