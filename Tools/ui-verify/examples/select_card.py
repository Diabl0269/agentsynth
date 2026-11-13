#!/usr/bin/env python3
"""Opens the macros fixture, selects the oscillator card with a real (synthetic) click and saves a PNG of the canvas.

    python3 Tools/ui-verify/examples/select_card.py [--app <path to Agent Synth.app>] [--out <dir>]

Nothing here touches the real mouse or keyboard, and no macOS privacy prompt appears.
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from agentsynth_testmode import TestModeApp  # noqa: E402

FIXTURE = os.path.join(os.path.dirname(HERE), "fixtures", "macros.agsproj")


def find_cards(node, out):
    if node.get("id", "").startswith("card:"):
        out.append(node)
    for child in node.get("children", []):
        find_cards(child, out)
    return out


def select_card(app, out_dir):
    """The whole scenario against one running instance. Returns the PNG path."""
    app.call("openProject", path=FIXTURE)
    app.call("waitForIdle")
    tree = app.call("list", root="window", depth=40)
    cards = find_cards(tree, [])
    ids = [c["id"] for c in cards]
    assert ids, "no module cards listed"
    print("cards:", ", ".join(ids))
    win = tree["bounds"]
    # Click the middle of the visible part of a card header (a card scrolled partly out of the window included).
    spot = None
    for card in sorted(cards, key=lambda c: not c["id"].startswith("card:Oscillator")):
        b = card["bounds"]
        left, right = max(b["x"], 0), min(b["x"] + b["w"], win["w"])
        if right - left >= 60 and 0 <= b["y"] + 12 < win["h"]:
            spot = (card["id"], (left + right) // 2 - b["x"])
            break
    assert spot, "no card header is inside the window"
    target, click_x = spot
    assert not app.call("getState", id=target)["selected"], "%s started selected" % target
    app.call("click", id=target, x=click_x, y=12)  # the header strip: selects the card without touching a control
    app.call("waitForIdle")
    state = app.call("getState", id=target)
    assert state["selected"], "%s was not selected by the click: %r" % (target, state)
    png = os.path.join(out_dir, "select_card.png")
    shot = app.call("screenshot", id="window", path=png)
    assert os.path.getsize(png) > 0 and shot["width"] > 0
    return png


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", help="path to Agent Synth.app (default: build-ci-local)")
    parser.add_argument("--out", default=os.path.join(os.path.dirname(HERE), "out"), help="where the PNG goes")
    parser.add_argument("--visible", action="store_true", help="show the app window (default: hidden)")
    options = parser.parse_args()
    out_dir = os.path.abspath(options.out)
    os.makedirs(out_dir, exist_ok=True)
    with TestModeApp(options.app, visible=options.visible) as app:
        png = select_card(app, out_dir)
    print("PASS select_card ->", png)


if __name__ == "__main__":
    main()
