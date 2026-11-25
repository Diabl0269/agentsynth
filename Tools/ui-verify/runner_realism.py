"""Rules that keep a scenario run close to what a hand and a real app do (the harness-mode-divergence bug kind).

* A recorded drag is a hand's path: a small tremble across the line, an overshoot past the target and back, and a point
  outside the window when the target is near its edge, sent as `captured` moves. The path comes from a seed made of the
  scenario name and the step number, so a run repeats.
* A step that freezes the app fails: every input command is timed (send to answer) and the slowest of a step must stay
  under `max_step_ms` (400 ms by default).
* `feedVisual` fills a display with no audio chain, so a scenario that feeds one may not check Mute, Bypass or note state.
* A click whose point is under another window of the app (a pop-up, a picker) would reach that window for a user.
* The opt-outs (`straight_drags`, `max_step_ms`, `click_through_windows`, `skip_reopen_check`) each state a reason; scenarios that broke a
  rule before it existed are listed, shrink-only, in `known-realism-fails.txt`.
"""

import math
import os
import random
import re
import time
import zlib

STALL_MS = 400  # the default limit of one input command, from sending it to the app's answer
STALL_COMMANDS = {"click", "wheel", "hover", "mouseDown", "mouseMoveHeld", "mouseUp", "key", "keyDown", "keyUp", "drag"}
DRAG_MS_PER_MOVE = 40  # the `drag` command is many moves in one call: it may take this much more for each
TREMBLE_PX = 1.5  # most a hand's path strays across the line
OVERSHOOT_MIN_PX, OVERSHOOT_MAX_PX, OVERSHOOT_SHARE, OVERSHOOT_MIN_LENGTH = 3, 14, 0.06, 20
NOT_A_WINDOW = {"tooltipWindow"}  # tips let the pointer through
AUDIO_STATE = re.compile(r"mute|bypass|note|voice", re.IGNORECASE)
RULES = {"stall", "covered", "reopen"}
KNOWN_REALISM = os.path.join(os.path.dirname(os.path.abspath(__file__)), "known-realism-fails.txt")


class RealismError(ValueError):
    """A scenario breaks a realism rule at parse time (the runner turns it into its own ScenarioError)."""


class Realism:
    """The realism options of one scenario."""

    def __init__(self, straight_drags=False, max_step_ms=STALL_MS, click_through_windows=False, skip_reopen_check=False, real_window=False):
        self.straight_drags = straight_drags
        self.max_step_ms = max_step_ms
        self.click_through_windows = click_through_windows
        self.skip_reopen_check = skip_reopen_check
        self.real_window = real_window  # runs visible, with OS keys, only when the user is away (real_window.py)


def _reason(value, key, name):
    if not isinstance(value, str) or not value.strip():
        raise RealismError("%s: `%s` needs a reason string saying why the scenario is the exception, not true" % (name, key))
    return True


def parse_realism(document, name):
    """The `straight_drags`, `max_step_ms`, `click_through_windows`, `skip_reopen_check` and `real_window` keys of a scenario mapping."""
    options = Realism()
    if "straight_drags" in document:
        options.straight_drags = _reason(document["straight_drags"], "straight_drags", name)
    if "real_window" in document:
        if not isinstance(document["real_window"], bool):
            raise RealismError("%s: `real_window` is true or false" % name)
        options.real_window = document["real_window"]
    if "skip_reopen_check" in document:
        options.skip_reopen_check = _reason(document["skip_reopen_check"], "skip_reopen_check", name)
    if "click_through_windows" in document:
        options.click_through_windows = _reason(document["click_through_windows"], "click_through_windows", name)
    if "max_step_ms" in document:
        value = document["max_step_ms"]
        ms = value.get("ms") if isinstance(value, dict) else None
        if not isinstance(value, dict) or set(value) != {"ms", "reason"} or isinstance(ms, bool) \
                or not isinstance(ms, (int, float)) or ms <= 0:
            raise RealismError("%s: `max_step_ms` is {ms: <positive number>, reason: \"<why a step may take this long>\"}" % name)
        _reason(value["reason"], "max_step_ms.reason", name)
        options.max_step_ms = ms
    return options


def hand_path(start, end, steps, seed):
    """The moves of one drag as [(x, y)]: `steps` along the line to a point a little past `end` with a tremble across
    it, then two back to `end`, the last exactly on `end`. A short drag has no overshoot. The same seed, the same path."""
    rng = random.Random(seed)
    dx, dy = end[0] - start[0], end[1] - start[1]
    length = math.hypot(dx, dy)
    if length < 1:
        return [tuple(end)] * steps
    ux, uy = dx / length, dy / length
    over = 0.0
    if length >= OVERSHOOT_MIN_LENGTH:
        over = min(OVERSHOOT_MAX_PX, max(OVERSHOOT_MIN_PX, OVERSHOOT_SHARE * length))
    far = (end[0] + ux * over, end[1] + uy * over)
    points = []
    for index in range(1, steps + 1):
        t = index / steps
        wobble = rng.uniform(-TREMBLE_PX, TREMBLE_PX) * math.sin(math.pi * t)  # none at either end
        points.append((round(start[0] + (far[0] - start[0]) * t - uy * wobble),
                       round(start[1] + (far[1] - start[1]) * t + ux * wobble)))
    if over:
        points[-1] = (round(far[0]), round(far[1]))
        points.append((round(end[0] + ux * over / 2 + uy * rng.uniform(-1, 1)),
                       round(end[1] + uy * over / 2 - ux * rng.uniform(-1, 1))))
        points.append(tuple(end))
    else:
        points[-1] = tuple(end)
    return points


def drag_seed(scenario, index):
    return zlib.crc32(("%s:%d" % (scenario, index)).encode("utf-8"))


def outside(point, width, height):
    return not (0 <= point[0] < width and 0 <= point[1] < height)


class TimedApp:
    """Wraps the app client: times every input command, from sending it to the app's answer. The app answers once its
    message thread has handled the input, so a step that freezes it shows as a slow answer (a wait for idle after the step
    would hide it)."""

    def __init__(self, app, clock=time.monotonic):
        self._app = app
        self._clock = clock
        self.slowest = None  # (ms over the command's allowance, ms, command) of the worst call since reset

    def __getattr__(self, name):
        return getattr(self._app, name)

    def reset(self):
        self.slowest = None

    def call(self, cmd, timeout_ms=None, **args):
        started = self._clock()
        try:
            return self._app.call(cmd, timeout_ms=timeout_ms, **args)
        finally:
            if cmd in STALL_COMMANDS:
                ms = (self._clock() - started) * 1000.0
                allowance = DRAG_MS_PER_MOVE * max(2, args.get("steps", 12)) if cmd == "drag" else 0
                if self.slowest is None or ms - allowance > self.slowest[0]:
                    self.slowest = (ms - allowance, ms, cmd)


def load_factor():
    """How many times busier than its cores the machine is (1 when idle): a run beside builds and other runs answers
    slowly for reasons that are not the app's, so the stall limit grows with it."""
    try:
        return max(1.0, os.getloadavg()[0] / (os.cpu_count() or 1))
    except (OSError, AttributeError):
        return 1.0


def stall_message(timed, limit_ms):
    """"" when the step's slowest input answered within `limit_ms` (times the machine's load factor), else the failure naming it."""
    worst = getattr(timed, "slowest", None)
    if worst is None or worst[0] <= limit_ms:
        return ""
    factor = load_factor()
    if worst[0] <= limit_ms * factor:
        return ""
    return ("%s froze the app: it answered after %d ms, the limit is %d ms%s (a hang a user would feel; fix the stall, or "
            "`max_step_ms: {ms: N, reason: ...}` when this step is slow on purpose)"
            % (worst[2], worst[1], limit_ms * factor, " (%d ms, times a machine load of %.1f)" % (limit_ms, factor) if factor > 1 else ""))


REDO_RATIO = 3.0  # a redo may take this many times its undo to answer ...
REDO_EXTRA_MS = 120  # ... plus this much, so a 2 ms undo and a 9 ms redo never fail


def restore_ratio_message(undo_ms, redo_ms):
    """"" when a redo answered about as fast as the undo of the same edit (an undo and its redo should feel the same), else
    the failure naming both. The allowance grows with the machine's load, like the stall limit. The app answers once the
    restore is done (the call), so this catches a redo whose restore does more than its undo (a card it rebuilds, a pass
    it runs again); the slide that waits on a phase of its own is a count the glide tests hold (`CardGlideMacroTakeOutUndo`)."""
    allowed = max(undo_ms * REDO_RATIO, undo_ms + REDO_EXTRA_MS * load_factor())
    if redo_ms <= allowed:
        return ""
    return ("the redo answered after %d ms but the undo of the same edit after %d ms (allowed %d ms): an undo and its redo "
            "should start together; find what the redo does that the undo does not, or list the scenario in "
            "known-realism-fails.txt under `stall`" % (redo_ms, undo_ms, allowed))


def check_feed_visual(steps, name):
    """Raises RealismError when a scenario feeds a visual and also checks Mute, Bypass or note state: `feedVisual`
    detaches the audio callback and skips the chain, so the state it would show is not the app's."""
    def commands(command, args):
        yield command, args
        if command == "glide":
            for key in ("do", "then"):
                for inner, inner_args in (args.get(key) or {}).items():
                    yield inner, inner_args or {}

    fed = any(c == "feedVisual" for command, args in steps for c, _ in commands(command, args))
    if not fed:
        return
    for index, (command, args) in enumerate(steps, 1):
        if command != "expect":
            continue
        named = [str(args.get("id", ""))] + [k for k in args if k not in {"id", "within_ms", "exists"}]
        for key in ("contains", "above"):
            named += list(args.get(key) or {})
        hit = next((n for n in named if AUDIO_STATE.search(n)), None)
        if hit:
            raise RealismError(
                "%s: step %d checks %r but the scenario uses `feedVisual`, which fills a display with the audio chain "
                "detached, so it cannot prove Mute, Bypass or note state; check that in a scenario with a played note "
                "(`audioPump`) and no `feedVisual`" % (name, index, hit))


def covering_window(windows, point, target):
    """The id of the window a click at `point` (main-window corner coordinates) reaches instead of `target` (its bounds,
    the same coordinates), or None. `windows` is the screen frame's list, the main window first and later ones on top, in
    frame coordinates. A window that holds the whole target is the target's own."""
    if not windows:
        return None
    ox, oy = windows[0]["x"], windows[0]["y"]
    px, py = point[0] + ox, point[1] + oy
    tx, ty = target["x"] + ox, target["y"] + oy

    def holds(w, x, y, width=0, height=0):
        return w["x"] <= x and w["y"] <= y and x + width <= w["x"] + w["width"] and y + height <= w["y"] + w["height"]

    top = None
    for window in windows[1:]:  # the main window is never above itself: a card scrolled partly off it is still its own
        if window.get("id") not in NOT_A_WINDOW and holds(window, px, py):
            top = window
    if top is None or holds(top, tx, ty, target["w"], target["h"]):
        return None
    return top.get("id") or "a window"


def covered_message(app, component, x, y, shot_path):
    """"" when a click on `component` at its local (x, y) lands on it, else a message naming the window above it. A
    component the app cannot find is left to the click itself to report."""
    bounds = app.call("getState", id=component).get("bounds")
    if not bounds:
        return ""
    point = (bounds["x"] + (x if x is not None else bounds["w"] // 2), bounds["y"] + (y if y is not None else bounds["h"] // 2))
    windows = (app.call("screenshot", id="screen", scale=0.1, path=shot_path) or {}).get("windows") or []
    above = covering_window(windows, point, bounds)
    if above is None:
        return ""
    return ("the click on %r at screen point %d,%d lands under the window %r: a user's click would reach that window, not "
            "the component (close it first, or `click_through_windows: \"<reason>\"` when the click is meant to pass through)"
            % (component, point[0], point[1], above))


def load_known_realism(path=KNOWN_REALISM):
    """known-realism-fails.txt: lines `<scenario> <rule> <ticket> <reason...>` (rule: stall, covered or reopen), `#`
    comments. {(scenario, rule): (ticket, reason)}. Shrink-only: a fixed scenario's line is deleted."""
    known = {}
    if not os.path.isfile(path):
        return known
    with open(path, encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(None, 3)
            if len(parts) < 4 or parts[1] not in RULES:
                raise RealismError("%s:%d: expected `<scenario> <%s> <ticket> <reason>`, got %r"
                                   % (path, number, "|".join(sorted(RULES)), line))
            known[(parts[0], parts[1])] = (parts[2], parts[3])
    return known
