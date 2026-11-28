#!/usr/bin/env python3
"""Runs ui-verify scenarios (YAML) against Agent Synth's in-app test mode. Standard library + PyYAML.

    python3 Tools/ui-verify/run.py <scenario.yaml | directory> [--app PATH] [--out DIR]

Each scenario launches its own test-mode instance (port 0, temporary settings directory), runs its steps, saves the
screenshots, writes a JSON-lines run log and prints a summary; the exit status is non-zero when any step failed. The
format is in docs/development/test-mode.md (Scenarios).

A scenario file is either a list of steps or a mapping with `steps:` and optional `settings:` (values, with ${FIXTURES} and ${OUT} filled in, written to the
instance's settings file before it first starts; `settings: {audio: {rate, block, latency}}` starts the silent test device at that setup), `motion: true` (start with --automation-motion), `record: true` (<out>/<scenario>.mp4, no sound, filmed from the `screen` frame: the main window, every other window of the app and the pointer; `record_target: {id: <component id>, scale: N}` films that component instead),
`save_project: <name>` (<out>/<name>.agsproj, saved after the last step) and `stale_pixel_check: true` (after every step
that can change the window, what the window shows must equal a full repaint; a mapping {ignore: [ids], settle_ms: N}
masks components and sets how long paints may settle), `pop_check` (on by default with `motion: true`: nothing added, removed
or swapped on screen may land in one frame; `pop_check: {off: "<reason>"}` turns it off, a bare `false` is refused; a mapping
may also give `ignore: [ids]`, `ms: N` and `undo: false`) and, on a step, `instant: "<reason>"` (that step may pop on purpose).
Known pops are listed in `known-pops.txt` (see docs/development/test-mode.md#no-pop-check). One mapping per step, `- <command>: {args}`:

    - openProject: {path: ${FIXTURES}/macros.agsproj}   # any protocol command (click, drag, key, getState, ...)
    - click: {id: "card:LFO 1", x: 40, y: 12}
    - type: {text: delay}                               # one `key` per character
    - key: {keys: cmd+z}
    - wait: {}                                          # waitForIdle
    - restart: {}                                       # quit and start again on the same settings directory
    - screenshot: {id: window, name: after-undo}        # <out>/after-undo.png
    - expect: {id: "card:LFO 1", selected: true}        # equality on getState fields (several allowed)
    - expect: {id: timelineTransportRecord, contains: {tooltip: "Ctrl+R"}}
    - expect: {id: statusBar, above: {voices: 0}}       # a numeric field strictly greater than the number (or {remembered: name})
    - expect: {id: "card:LFO 1", exists: false}
    - expect: {count: {prefix: "card:", equals: 3}}     # list ids (or field: title) starting with the prefix
    - remember: {id: "hostedKnob:1", field: value, as: knob}   # keeps the value (field default `value`) ...
    - expect: {id: "hostedKnob:1", value: {remembered: knob}}   # ... for a later expect to compare against
    - click: {id: "card:LFO 1", approach: false}        # with `record`, a click is preceded by a 4-frame pointer approach; this skips it
    - mark: {check: 2, name: picker-open}               # proof for check 2, see below
    - glide: {name: card-slides, frames: 12, do: {click: {id: "macroCard:Macro", x: 262, y: 18}}}   # a motion, frame by frame

`mark: {check: N, name: <slug>}` (N an integer >= 1, name [a-z0-9-]+, unique in the scenario) is the moment a check's
claim is on screen. It takes one frame held 1.5 s, saves that image as `<out>/proof/<name>.png` (also without `record`)
and, once the video is written, cuts `<out>/<scenario>-<name>.mp4` from 2.5 s before the mark to 3 s after (4 to 6 s
long when the video allows; `before_s: N`, a number above 0 up to 30, starts it N s before the mark frame instead and ends it with the frame's hold, with no 6 s cap: one clip of a walk of several steps). With `record`, each `click` first films the pointer gliding in (four `hover`s from 60 px up-left, 0.1 s a frame; `approach: false` skips it), so a mark right after a click that changes the screen has a clip of before, approach and after. Put it while the popup or hover it shows is still open; the summary lists the stills and clips.

`glide: {name: <slug>, do: {<command>: {args}}, frames: N}` (needs `record: true`; `frames` 4 to 40, default 12; `crop: {x, y, w, h}` limits the strip image to that part of the frame, in two columns) sends
the command and then films the screen N times in a row, 0.1 s a frame, instead of the one frame a step normally gets:
a 220 ms slide or fade is thus a dozen frames in the video, not one held frame caught mid-way (which reads as a blue
block). It also saves `<out>/proof/<name>-strip.png`, the frames tiled in rows of four. The step takes no frame of its own
after the command; follow it with a `wait` and a `mark`. A glide whose frames hold fewer than three different images
inside the crop (or the frame) fails, unless it has `instant: "<reason>"`. `slow_motion: {factor: N}` (1 to 20) makes
every animation that starts afterwards run N times longer (reset at the end). A recorded frame moves an animation on by
only 16 ms, so a slowed glide needs about 10 x N frames: a glide after `slow_motion` whose last frame still differs from
the landed state fails as "ended mid-motion".

`${FIXTURES}`, `${OUT}` and `${SCRATCH}` (the run's temporary settings directory) are replaced in every string.
`- copyFixture: {name: x.agsproj}` copies `${FIXTURES}/x.agsproj` into `${SCRATCH}/x.agsproj` for a scenario that
changes its project and must not change the committed copy; `from: <bundle>` (`~` expanded) copies a project that is not
in the repository instead (a private one on this machine), without its autosave sidecars; `to: <name>` puts the copy under another name (`name: SoundPacksBundledOnly, to: SoundPacks` seeds the sound-packs folder from a fixture of its own). An `expect` is retried for `within_ms` (default 500) so a
repaint that is still settling is not a failure; one that never arrives still is.
"""

import argparse
import json
import os
import re
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from agentsynth_testmode import TestModeApp, TestModeError  # noqa: E402
from ax_dump import write_ax_dump  # noqa: E402
from project_files import localise_scratch_files  # noqa: E402,F401
import runner_expect, runner_generated, runner_glide, runner_realism  # noqa: E402
from audio_settings import apply_audio_setting  # noqa: E402
import real_window  # noqa: E402
import runner_reopen  # noqa: E402
from scenario_checks import (MARK_NAME, RUNNER_STEPS, SLOW_MOTION_MAX, ScenarioError, _check_glide, _check_instant,  # noqa: E402,F401
                             _check_mark, _check_slow_motion)
from recording import DEFAULT_TARGET, DRAG_HOLD_SECONDS, MARK_HOLD_SECONDS, Recorder  # noqa: E402

FIXTURES = os.path.join(HERE, "fixtures")
DEFAULT_OUT = os.path.join(HERE, "out")
SETTINGS_FILE = "Agent Synth.settings"
EXPECT_KEYS = {"id", "contains", "above", "exists", "count", "within_ms"}
UNCHANGED_BY = {"expect", "screenshot", "getState", "list", "ping", "copyFixture", "mark", "glide", "slow_motion", "remember"}  # no video frame after these
NOT_CHECKED_AFTER = UNCHANGED_BY | {"restart", "quit", "wait", "mouseDown", "mouseMoveHeld", "keyDown"}  # no stale-pixel check
APPROACH_OFFSET = 60  # px up and left of the click point the recorded pointer starts from
APPROACH_STEPS = 4  # hovers (and frames) before a recorded click
POP_CHECKED = {"click", "drag", "key", "wheel", "mouseUp", "keyUp"}  # protocol actions that can change the window; a glide's `do` too
POP_KEYS = {"ignore", "ms", "undo", "off"}
KNOWN_POPS = os.path.join(HERE, "known-pops.txt")
KNOWN_STEP = re.compile(r"^(\d+)([ur]?)$")
UNDO_STEPS = (("u", "undoButton"), ("r", "redoButton"))  # the toolbar buttons: Cmd+Z in a text field would undo its text
UNDO_KEYS = {"cmd+z", "ctrl+z"}


class Scenario:
    def __init__(self, name, steps, settings, path=None, motion=False, record=False, save_project=None,
                 stale_pixel_check=None, record_target=None, pop_check=None, realism=None):
        self.name = name
        self.steps = steps  # [(command, args dict)]
        self.settings = settings  # {name: value}
        self.path = path
        self.motion = motion  # start with --automation-motion, for a check that needs a slide or fade to run
        self.record = record  # write <out>/<name>.mp4
        self.save_project = save_project  # write <out>/<save_project>.agsproj after the last step
        self.stale_pixel_check = stale_pixel_check  # None, or {"ignore": [ids], "settle_ms": N}
        self.record_target = record_target  # None (the main window), or {"id": component id, "scale": N} to film one
        self.realism = realism or runner_realism.Realism()  # straight_drags, max_step_ms, click_through_windows
        self.pop_check = pop_check  # None (off), or {"ignore": [ids], "ms": N, "undo": bool}: no add, remove or swap lands in one frame


def _substitute(value, variables):
    if isinstance(value, str):
        return re.sub(r"\$\{(\w+)\}", lambda m: variables.get(m.group(1), m.group(0)), value)
    if isinstance(value, dict):
        return {k: _substitute(v, variables) for k, v in value.items()}
    if isinstance(value, list):
        return [_substitute(v, variables) for v in value]
    return value


def _project_name(value, name):
    """The bundle's name without its extension. It stays inside the output folder, so it cannot hold a path."""
    text = str(value)
    if text.endswith(".agsproj"):
        text = text[: -len(".agsproj")]
    if not text or text.startswith(".") or "/" in text or "\\" in text:
        raise ScenarioError("%s: `save_project` must be a plain name, got %r" % (name, value))
    return text


def _stale_pixel_option(value, name):
    """`stale_pixel_check: true`, or a mapping with `ignore` (component ids) and `settle_ms`. None when off."""
    if value is None or value is False:
        return None
    if value is True:
        return {}
    if not isinstance(value, dict) or set(value) - {"ignore", "settle_ms"}:
        raise ScenarioError("%s: `stale_pixel_check` is true or a mapping with `ignore` and `settle_ms`" % name)
    if not isinstance(value.get("ignore", []), list):
        raise ScenarioError("%s: `stale_pixel_check.ignore` must be a list of component ids" % name)
    return dict(value)


def _pop_check_option(document, motion, name):
    """`pop_check`: on (an empty mapping) by default with `motion: true`, off otherwise. `true`, or a mapping with
    `ignore`, `ms` and `undo`, turns it on; `{off: "<reason>"}` turns it off. A bare `false` is refused: it needs a reason."""
    if "pop_check" not in document:
        return {} if motion else None
    value = document["pop_check"]
    if isinstance(value, dict):
        value = {"off" if key is False else key: item for key, item in value.items()}  # YAML 1.1 reads a bare `off` as False
    if value is True:
        return {}
    if not isinstance(value, dict) or set(value) - POP_KEYS:
        raise ScenarioError("%s: `pop_check: false` needs a reason: `pop_check: {off: \"<why>\"}` (or true, or a mapping "
                            "with `ignore`, `ms`, `undo`)" % name)
    if "off" in value:
        if set(value) != {"off"} or not isinstance(value["off"], str) or not value["off"].strip():
            raise ScenarioError("%s: `pop_check.off` is a reason string and stands alone" % name)
        return None
    if not isinstance(value.get("ignore", []), list):
        raise ScenarioError("%s: `pop_check.ignore` must be a list of component ids" % name)
    ms = value.get("ms", 300)
    if isinstance(ms, bool) or not isinstance(ms, (int, float)) or ms <= 0:
        raise ScenarioError("%s: `pop_check.ms` must be a positive number" % name)
    if not isinstance(value.get("undo", True), bool):
        raise ScenarioError("%s: `pop_check.undo` must be true or false" % name)
    return dict(value)


def _realism_options(document, name):
    try:
        return runner_realism.parse_realism(document, name)
    except runner_realism.RealismError as error:
        raise ScenarioError(str(error))


def load_known_pops(path=KNOWN_POPS):
    """known-pops.txt: lines `<scenario> <step>[u|r] <ticket> <reason...>`, `#` comments. {(scenario, step label): (ticket, reason)}."""
    known = {}
    if not os.path.isfile(path):
        return known
    with open(path, encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(None, 3)
            if len(parts) < 4 or not KNOWN_STEP.match(parts[1]):
                raise ScenarioError("%s:%d: expected `<scenario> <step>[u|r] <ticket> <reason>`, got %r" % (path, number, line))
            known[(parts[0], parts[1].lstrip("0") or "0")] = (parts[2], parts[3])
    return known


def _record_target_option(value, name):
    """`record_target: {id: <component id>, scale: N}`: film that component (a popover is a window of its own) instead of
    the main window."""
    if value is None:
        return None
    if not isinstance(value, dict) or not isinstance(value.get("id"), str):
        raise ScenarioError("%s: `record_target` must be a mapping with an `id`" % name)
    return {"id": value["id"], "scale": float(value.get("scale", 1))}


def parse_scenario(text, name="scenario", path=None):
    """Parses scenario YAML. A step is a mapping with exactly one key (the command); its value is the args mapping."""
    import yaml

    try:
        document = yaml.safe_load(text)
    except yaml.YAMLError as error:
        raise ScenarioError("%s: not valid YAML: %s" % (name, error))
    settings = {}
    motion = record = test_account = False
    save_project = stale = record_target = pop_check = realism = None
    if isinstance(document, dict):
        stale = _stale_pixel_option(document.get("stale_pixel_check"), name)
        motion = bool(document.get("motion"))
        test_account = document.get("test_account")
        record = bool(document.get("record"))
        record_target = _record_target_option(document.get("record_target"), name)
        pop_check = _pop_check_option(document, motion, name)
        realism = _realism_options(document, name)
        if document.get("save_project") is not None:
            save_project = _project_name(document["save_project"], name)
        settings = document.get("settings") or {}
        document = document.get("steps")
        if not isinstance(settings, dict):
            raise ScenarioError("%s: `settings` must be a mapping" % name)
        settings = apply_audio_setting(settings, name)
    if not isinstance(document, list) or not document:
        raise ScenarioError("%s: expected a non-empty list of steps" % name)
    steps = []
    mark_names = set()
    glide_names = set()
    for index, raw in enumerate(document, 1):
        if not isinstance(raw, dict) or len(raw) != 1:
            raise ScenarioError("%s: step %d must be a mapping with one command, got %r" % (name, index, raw))
        command, args = next(iter(raw.items()))
        if args is None:
            args = {}
        if not isinstance(args, dict):
            raise ScenarioError("%s: step %d (%s) takes a mapping of arguments, got %r" % (name, index, command, args))
        _check_instant(args, index, name)
        if command == "mark":
            _check_mark(args, index, name, mark_names)
        if command == "glide":
            _check_glide(args, index, name, record, glide_names)
        if command == "slow_motion":
            _check_slow_motion(args, index, name)
        if command == runner_generated.GENERATE:
            try:
                steps.extend(runner_generated.expand_generate_project(args, index, name))
            except ValueError as error:
                raise ScenarioError(str(error))
            continue
        steps.append((str(command), args))
    try:
        runner_realism.check_feed_visual(steps, name)
        runner_reopen.check_project_paths(steps, name)
    except (runner_realism.RealismError, ValueError) as error:
        raise ScenarioError(str(error))
    scenario = Scenario(name, steps, settings, path, motion, record, save_project, stale, record_target, pop_check, realism)
    scenario.test_account = bool(test_account)  # launch signed in as the fixed test account
    return scenario


def load_scenarios(target):
    """A scenario file, or every *.yaml / *.yml directly inside a directory, sorted by name."""
    paths = [target]
    if os.path.isdir(target):
        paths = sorted(
            os.path.join(target, f) for f in os.listdir(target) if f.endswith((".yaml", ".yml")) and not f.startswith("_")
        )
        if not paths:
            raise ScenarioError("no .yaml scenarios in %s" % target)
    scenarios = []
    for path in paths:
        with open(path, encoding="utf-8") as handle:
            scenarios.append(parse_scenario(handle.read(), os.path.splitext(os.path.basename(path))[0], path))
    return scenarios


def settings_xml(values):
    """The XML a juce::PropertiesFile reads: one VALUE per setting."""

    def escape(text):
        return (str(text).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;"))

    lines = ['<?xml version="1.0" encoding="UTF-8"?>', "", "<PROPERTIES>"]
    for key, value in values.items():
        if isinstance(value, bool):
            value = int(value)
        lines.append('  <VALUE name="%s" val="%s"/>' % (escape(key), escape(value)))
    lines.append("</PROPERTIES>")
    return "\n".join(lines) + "\n"


def list_ids(tree, field="id"):
    """Every non-empty `field` in a `list` tree, depth first."""
    found = []

    def walk(node):
        if node.get(field):
            found.append(node[field])
        for child in node.get("children", []):
            walk(child)

    for root in tree if isinstance(tree, list) else [tree]:  # the protocol answers with one tree per window
        walk(root)
    return found


def check_expect(args, app, remembered=None):
    """Evaluates one expect once. Returns None when it holds, else a message naming what differed.

    A wanted value of `{remembered: name}` is what a `remember` step stored under that name."""
    unknown = set(args)
    equals = {k: v for k, v in args.items() if k not in EXPECT_KEYS}
    problems = []
    if "count" in args:
        spec = args["count"]
        field = spec.get("field", "id")
        prefix = spec.get("prefix", "")
        tree = app.call("list", root=spec.get("root", "window"), depth=spec.get("depth", 40))
        matching = [v for v in list_ids(tree, field) if str(v).startswith(prefix)]
        if "equals" in spec and len(matching) != spec["equals"]:
            problems.append("count of %s starting with %r: expected %s, got %d %s" % (
                field, prefix, spec["equals"], len(matching), matching[:8]))
        if "atLeast" in spec and len(matching) < spec["atLeast"]:
            problems.append("count of %s starting with %r: expected at least %s, got %d" % (
                field, prefix, spec["atLeast"], len(matching)))
        unknown.discard("count")
    if "id" in args:
        unknown.discard("id")
        for key in ("contains", "above", "exists", "within_ms"):
            unknown.discard(key)
        try:
            state = app.call("getState", id=args["id"])
        except TestModeError as error:
            if args.get("exists") is False and "no component" in str(error):
                return "\n".join(problems) or None
            return "no state for %r: %s" % (args["id"], error)
        if args.get("exists") is False:
            problems.append("%r exists but should not" % args["id"])
        for field, want in equals.items():
            problem = runner_expect.compare_field(args["id"], field, want, state.get(field, "<missing>"), remembered)
            if problem:
                problems.append(problem)
        for field, needle in (args.get("contains") or {}).items():
            got = str(state.get(field, ""))
            if str(needle).lower() not in got.lower():
                problems.append("%s of %r: expected it to contain %r, got %r" % (field, args["id"], needle, got))
        for field, floor in (args.get("above") or {}).items():
            if isinstance(floor, dict):  # {remembered: name}: above what a `remember` step kept
                floor = (remembered or {}).get(floor.get("remembered"), float("inf"))
            got = state.get(field)
            if isinstance(got, bool) or not isinstance(got, (int, float)) or not got > floor:
                problems.append("%s of %r: expected a number above %r, got %r" % (field, args["id"], floor, got))
    elif equals or "above" in args:
        problems.append("fields %s need an `id`" % sorted(set(equals) | set(args.get("above") or {})))
    unknown -= {"contains", "above", "exists", "within_ms"}
    if not args.keys() & {"id", "count"}:
        problems.append("an expect needs `id` or `count`")
    return "; ".join(problems) or None


def run_expect(args, app, sleep=time.sleep, clock=time.monotonic, remembered=None):
    """check_expect, retried until `within_ms` has passed. Returns (ok, message)."""
    deadline = clock() + args.get("within_ms", 500) / 1000.0
    while True:
        message = check_expect(args, app, remembered)
        if message is None:
            return True, ""
        if clock() >= deadline:
            return False, message
        sleep(0.05)


class Runner:
    def __init__(self, app_path, out_dir, scenario, app_factory=TestModeApp, visible=False, known_pops=None, known_realism=None,
                 os_key_sender=real_window.send_chord):
        self.scenario = scenario
        known = load_known_pops() if known_pops is None else known_pops
        self.known_pops = {label: value for (name, label), value in known.items() if name == scenario.name}
        listed = runner_realism.load_known_realism() if known_realism is None else known_realism
        self.exempt = {rule for (name, rule) in listed if name == scenario.name}  # realism rules this scenario is listed against
        self.os_key_sender = os_key_sender
        self.step_index = 0  # the running step's number: seeds a recorded drag's hand path
        self.fed_visual = False  # a feedVisual detached the audio callback: re-attached when the scenario ends
        self.pop_problems = []  # a pop does not stop the run: every one in the scenario is reported together
        self.pop_seen = set()  # labels of the checked steps (and undo/redo) that ran
        self.undo_available = None  # None until a watch answers; False when the app reports no undo serial
        self.out_dir = out_dir
        self.app_path = app_path
        self.app_factory = app_factory
        self.visible = visible
        self.log = []
        self.app = None
        self.settings_dir = None
        self.recorder = None
        self.held = False
        self.slowed = False  # a slow_motion step set a factor above 1 on the running instance
        self.slow_factor = 1.0  # the factor that step set
        self._step_instant = None  # the running step's `instant` reason
        self.marks = []  # [{check, name, still, clip, start}] in step order; `clip` fills in once the video is written
        self.variables = {"FIXTURES": FIXTURES, "OUT": out_dir}
        self.remembered = {}  # `remember` steps: name -> the field value read then, for an `expect` of {remembered: name}

    def _launch_arguments(self):
        realistic = self.scenario.realism.real_window  # a real window is visible and moves
        extra = {"extra_args": ["--automation-motion"]} if self.scenario.motion or realistic else {}
        if self.visible or realistic:
            extra["visible"] = True
        if getattr(self.scenario, "test_account", False):
            extra["test_account"] = True
        return extra

    def _launch(self):
        self.app = runner_realism.TimedApp(self.app_factory(self.app_path, settings_dir=self.settings_dir,
                                                            **self._launch_arguments()))
        self.app.start()
        if self.scenario.stale_pixel_check is not None:
            self.app.call("trackPresentedFrame")
        self.held = False

    def _check_stale_pixels(self, index, command):
        """After a step that can change the window: what it shows must equal a full repaint once paints settle.
        Returns an error message, or "" when it holds. On a failure the three PNGs are in <out>/stale-step-NN/."""
        option = self.scenario.stale_pixel_check
        if option is None or command in NOT_CHECKED_AFTER or self.held:
            return ""
        args = {"dir": os.path.join(self.out_dir, "stale-step-%02d" % index)}
        if option.get("ignore"):
            args["ignore"] = option["ignore"]
        if "settle_ms" in option:
            args["settleMs"] = option["settle_ms"]
        try:
            reply = self.app.call("checkStalePixels", **args)
        except TestModeError as error:
            self._record(index, "checkStalePixels", args, None, False, str(error))
            return "stale pixels after %s: %s" % (command, error)
        self._record(index, "checkStalePixels", args, reply, True)
        return ""

    def _pop_watched(self, command, args):
        """Whether a step is a protocol action that can change the window, so a watch goes around it."""
        if self.scenario.pop_check is None or (self.held and command != "mouseUp"):
            return False
        if command == "glide":
            command = next(iter(args["do"]))
        return command in POP_CHECKED

    def _pop_verdict(self, label, command, reply, instant, folder):
        """The result of one popWatchCheck: "" when the change animated, was deliberately instant or is a known pop;
        else the failure message."""
        self.pop_seen.add(label)
        known = self.known_pops.get(label)
        popped = bool(reply.get("changed") and reply.get("pop"))
        if not popped:
            if known:
                # Pop detection depends on which frames the screen caught, so a listed pop can animate on a busy run:
                # say so, but do not fail the scenario.
                print("note: step %s did not pop this run: remove it from known-pops.txt if it never does (%s %s)"
                      % (label, known[0], known[1]), file=sys.stderr)
            return ""
        where = "x %d y %d w %d h %d (%d pixels, %d frames seen)" % (
            reply["regionX"], reply["regionY"], reply["regionW"], reply["regionH"], reply["regionPixels"], reply["framesSeen"])
        if instant:
            return ""
        if known:
            print("ui-verify: %s: known pop after %s at step %s (%s): %s" % (self.scenario.name, command, label, known[0], known[1]))
            return ""
        return ("pop after %s: what changed in %s showed up in one frame instead of animating; see %s "
                "(before.png, first.png, final.png, differences.png). Animate it, mark the step `instant: <reason>` if it "
                "is a text or value change, or list it in known-pops.txt" % (command, where, folder))

    def _pop_check(self, index, label, command, instant):
        """popWatchCheck after a watched action. Returns (reply, failure message)."""
        option = self.scenario.pop_check
        folder = os.path.join(self.out_dir, "pop-step-%02d%s" % (index, label[len(str(index)):]))
        # The toolbar's Undo and Redo buttons light up or dim at once after any edit, by design.
        args = {"dir": folder, "ignore": list(option.get("ignore", [])) + ["undoButton", "redoButton"]}
        if "ms" in option:
            args["ms"] = option["ms"]
        try:
            reply = self.app.call("popWatchCheck", **args)
        except TestModeError as error:
            self._record(index, "popWatchCheck", args, None, False, str(error))
            return None, "pop check after %s: %s" % (command, error)
        message = self._pop_verdict(label, command, reply, instant, folder)
        self._record(index, "popWatchCheck", args, dict(reply, step=label), not message, message or None)
        return reply, message

    @staticmethod
    def _is_undo(command, args):
        """Whether the step is itself an undo: Cmd+Z, the Undo button, or a glide whose `do` is one of those."""
        if command == "glide":
            inner = args.get("do") or {}
            return len(inner) == 1 and Runner._is_undo(*next(iter(inner.items())))
        if command == "key":
            return str(args.get("keys", "")).lower() in UNDO_KEYS
        return command == "click" and args.get("id") == "undoButton"

    def _undo_redo(self, index, command, args, instant):
        """After an edit: undo, then redo, each under a watch and each must animate too. After an undo the order is
        redo, then undo: undoing first would undo the edit before it, or nothing when the stack is empty, and the redo
        that follows would then put back the edit the step had just undone. Returns a failure message."""
        answered = {}
        for suffix, button in reversed(UNDO_STEPS) if self._is_undo(command, args) else UNDO_STEPS:
            label = "%d%s" % (index, suffix)
            self.app.call("popWatchStart")
            self.app.reset()
            self.app.call("click", id=button)
            answered[suffix] = (self.app.slowest or (0, 0, ""))[1]
            self._record(index, "click", {"id": button}, {"step": label}, True)
            _, message = self._pop_check(index, label, "%s (%s)" % ("undo" if suffix == "u" else "redo", command), instant)
            if message:
                self.pop_problems.append("step %s: %s" % (label, message))
        slow = runner_realism.restore_ratio_message(answered.get("u", 0), answered.get("r", 0))
        if slow and "stall" in self.exempt:
            print("note: step %d (%s) %s, but %s is listed in known-realism-fails.txt" % (index, command, slow, self.scenario.name), file=sys.stderr)
            return ""
        if slow:
            self._record(index, command, {}, None, False, slow)
            return slow
        return ""

    def _step(self, index, command, args, instant=None):
        """One step, inside a pop watch when it is an action that can change the window. Returns (ok, message)."""
        watched = self._pop_watched(command, args)
        self._step_instant = instant
        self.step_index = index
        self.app.reset()
        covered = self._covered(command, args)
        if covered:
            self._record(index, command, args, None, False, covered)
            return False, covered
        self.fed_visual = self.fed_visual or "feedVisual" in (args.get("do") or {}) or command == "feedVisual"
        if watched:
            self.app.call("popWatchStart")
        ok, message = self._do_step(index, command, args)
        stall = self._stall(index, command)
        if ok and stall:
            return False, stall
        if not ok or not watched:
            return ok, message
        reply, message = self._pop_check(index, str(index), command, instant)
        if message:
            self.pop_problems.append("step %d: %s" % (index, message))
            if reply is None:
                return False, message
            message = ""
        edited = reply.get("undoSerialAfter") != reply.get("undoSerialBefore")
        if self.undo_available is None and "undoSerialBefore" not in reply:
            self.undo_available = False
            print("ui-verify: %s: undo and redo are not pop-checked: the app reports no undo serial" % self.scenario.name)
        if "undoSerialBefore" in reply and edited and self.scenario.pop_check.get("undo", True):
            self.undo_available = True
            message = self._undo_redo(index, command, args, instant)
        return (False, message) if message else (True, "")

    def _stall(self, index, command):
        """The failure when the step's slowest input froze the app for longer than the scenario allows, else ""."""
        message = runner_realism.stall_message(self.app, self.scenario.realism.max_step_ms)
        if message and "stall" in self.exempt:
            print("note: step %d (%s) stalled but %s is listed in known-realism-fails.txt: %s" % (index, command, self.scenario.name, message),
                  file=sys.stderr)
            return ""
        if message:
            self._record(index, command, {}, None, False, message)
        return "%s: %s" % (command, message) if message else ""

    def _covered(self, command, args):
        """The failure when a click's point is under another window of the app, else ""."""
        if command not in ("click", "mouseDown") or "id" not in args or self.scenario.realism.click_through_windows:
            return ""
        try:
            message = runner_realism.covered_message(self.app, args["id"], args.get("x"), args.get("y"),
                                                     os.path.join(self.settings_dir, "covered.png"))
        except TestModeError:
            return ""  # no such component, or no frame: the click itself reports it
        if message and "covered" in self.exempt:
            print("note: %s is listed in known-realism-fails.txt: %s" % (self.scenario.name, message), file=sys.stderr)
            return ""
        return message

    def _record(self, index, command, args, reply, ok, error=None):
        entry = {"scenario": self.scenario.name, "step": index, "cmd": command, "args": args, "reply": reply, "pass": ok}
        if error:
            entry["error"] = error
        self.log.append(entry)
        return entry

    def _point(self, component, x, y):
        """A point local to `component` (default its centre) in window coordinates, or None outside the main window."""
        bounds = self.app.call("getState", id=component)["bounds"]
        window = self.app.call("getState", id="window")["bounds"]
        px = bounds["x"] + (x if x is not None else bounds["w"] // 2)
        py = bounds["y"] + (y if y is not None else bounds["h"] // 2)
        return (px, py) if 0 <= px < window["w"] and 0 <= py < window["h"] else None

    def _screen_point(self, component, x, y):
        """A point local to `component` as (screen x, screen y, component bounds): the coordinates every getState
        reports, relative to the main window's corner and not clipped to it, so a point in another window resolves."""
        bounds = self.app.call("getState", id=component)["bounds"]
        px = bounds["x"] + (x if x is not None else bounds["w"] // 2)
        py = bounds["y"] + (y if y is not None else bounds["h"] // 2)
        return px, py, bounds

    def _recorded_drag(self, args):
        """The `drag` command's own press, moves and release, one command each so a frame fits between them. A drag
        with an end outside the main window (a shape dropped from the main window onto another window's map) is
        filmed the same way in screen coordinates: the press goes to `from` in its own coordinates, and every move and
        the release go to `to` as `captured` steps (the window the press happened in keeps the gesture, as the OS does)
        with the screen point turned into `to`'s own coordinates; the composited screen frame draws both windows."""
        cross = False
        if args["from"] == args["to"]:
            # One component: its own local coordinates, sent to it, so it lands on the component's own native
            # window (a shape window is not the main window, whose coordinates the path below uses).
            target = args["from"]
            bounds = self.app.call("getState", id=target)["bounds"]
            start = (args.get("fromX", bounds["w"] // 2), args.get("fromY", bounds["h"] // 2))
            end = (args.get("toX", bounds["w"] // 2), args.get("toY", bounds["h"] // 2))
            press_target, move_target, shift = target, target, (0, 0)
            area = (bounds["w"], bounds["h"])
        else:
            start = self._point(args["from"], args.get("fromX"), args.get("fromY"))
            end = self._point(args["to"], args.get("toX"), args.get("toY"))
            press_target = move_target = "window"
            shift = (0, 0)
            window = self.app.call("getState", id="window")["bounds"]
            area = (window["w"], window["h"])
            if start is None or end is None:
                cross = True
                sx, sy, from_bounds = self._screen_point(args["from"], args.get("fromX"), args.get("fromY"))
                ex, ey, to_bounds = self._screen_point(args["to"], args.get("toX"), args.get("toY"))
                start, end = (sx, sy), (ex, ey)
                area = None
                press_target, move_target = args["from"], args["to"]
                press_origin = (from_bounds["x"], from_bounds["y"])
                shift = (to_bounds["x"], to_bounds["y"])
        steps = max(2, args.get("steps", 12))
        mods = {"mods": args["mods"]} if "mods" in args else {}
        held = {"captured": True} if cross else {}
        press = (start[0] - press_origin[0], start[1] - press_origin[1]) if cross else start
        self.app.call("mouseDown", id=press_target, x=press[0], y=press[1], **mods)
        self.recorder.frame(self.app, DRAG_HOLD_SECONDS)
        if self.scenario.realism.straight_drags:
            points = [(round(start[0] + (end[0] - start[0]) * i / steps), round(start[1] + (end[1] - start[1]) * i / steps))
                      for i in range(1, steps + 1)]
        else:  # a hand: tremble, overshoot and back, possibly past the edge (then the press's window keeps the gesture)
            points = runner_realism.hand_path(start, end, steps, runner_realism.drag_seed(self.scenario.name, self.step_index))
        for x, y in points:
            away = {"captured": True} if held or (area and runner_realism.outside((x, y), *area)) else {}
            self.app.call("mouseMoveHeld", id=move_target, x=x - shift[0], y=y - shift[1], **away, **mods)
            self.recorder.frame(self.app, DRAG_HOLD_SECONDS)
        self.app.call("mouseUp", id=move_target, x=end[0] - shift[0], y=end[1] - shift[1], **held, **mods)
        return {"from": args["from"], "to": args["to"]}

    def _recorded_approach(self, args):
        """Before a recorded `click`: the pointer glides in from APPROACH_OFFSET px up-left (kept inside the main
        window) in APPROACH_STEPS `hover`s with a frame after each, so a clip of a static state shows the pointer
        arriving and not one picture twice. Does nothing (the click is sent as it is) when the click point is not in
        main-window coordinates or the step says `approach: false`."""
        if args.get("approach") is False or "id" not in args:
            return
        try:
            end = self._point(args["id"], args.get("x"), args.get("y"))
        except TestModeError:
            return
        if end is None:
            return
        window = self.app.call("getState", id="window")["bounds"]
        start = (max(0, end[0] - APPROACH_OFFSET), max(0, end[1] - APPROACH_OFFSET))
        start = (min(start[0], window["w"] - 1), min(start[1], window["h"] - 1))
        for index in range(APPROACH_STEPS):
            fraction = index / APPROACH_STEPS  # the last hover stays short of the click point; the click lands there
            x = round(start[0] + (end[0] - start[0]) * fraction)
            y = round(start[1] + (end[1] - start[1]) * fraction)
            self.app.call("hover", id="window", x=x, y=y)
            self.recorder.frame(self.app, DRAG_HOLD_SECONDS)

    def _glide(self, args):
        """Sends `do`, then films the screen `frames` times in a row (runner_glide.py). The strip image tiles those frames."""
        return runner_glide.run_glide(self, args)

    def _mark(self, args):
        """The still (and the frame the clip is cut around) for one check. Raises TestModeError when no frame came."""
        still = os.path.join(self.out_dir, "proof", args["name"] + ".png")
        start = None
        if self.recorder is not None:
            start = self.recorder.frame(self.app, MARK_HOLD_SECONDS, copy_to=still)
        else:
            target = self.scenario.record_target or DEFAULT_TARGET
            os.makedirs(os.path.dirname(still), exist_ok=True)
            self.app.call("screenshot", id=target["id"], scale=target["scale"], path=still)
        if not os.path.isfile(still):
            raise TestModeError("mark %s: no frame could be taken" % args["name"])
        ax = write_ax_dump(self.app, os.path.splitext(still)[0] + ".ax.json")
        reply = {"check": args["check"], "name": args["name"], "still": still, "clip": None, "ax": ax}
        self.marks.append(dict(reply, start=start, before_s=args.get("before_s")))
        self.marks[-1]["reply"] = reply  # the run log's entry, completed when the clip is cut
        return reply

    def _do_step(self, index, command, args):
        """Runs one step. Returns (ok, error message)."""
        app = self.app
        if self.scenario.realism.real_window and command in ("key", "type"):
            reply = real_window.os_keys(app.pid, command, args, self.os_key_sender)  # the OS delivers it, as for a person
            self._record(index, command, args, reply, True)
            return True, ""
        if command == "expect":
            ok, message = run_expect(args, app, remembered=self.remembered)
            self._record(index, command, args, None, ok, message)
            return ok, "expect %s: %s" % (json.dumps(args), message) if not ok else ""
        if command == "remember":
            reply = {args["as"]: app.call("getState", id=args["id"]).get(args.get("field", "value"))}
            self.remembered.update(reply)
        elif command == "wait":
            reply = app.call("waitForIdle", **args)
        elif command == "type":
            extra = {"target": args["target"]} if args.get("target") else {}
            for char in str(args.get("text", "")):
                app.call("key", keys="space" if char == " " else char, **extra)  # the key parser splits on spaces
            reply = {"typed": len(str(args.get("text", "")))}
        elif command == "copyFixture":
            destination = os.path.join(self.settings_dir, args.get("to") or args["name"])
            source = os.path.expanduser(args["from"]) if args.get("from") else os.path.join(FIXTURES, args["name"])
            if not os.path.isdir(source):
                self._record(index, command, args, None, False, "no project at " + source)
                return False, "copyFixture: no project at %s" % source
            shutil.copytree(source, destination)
            for leftover in os.listdir(destination):  # openProject refuses a bundle with an autosave sidecar
                if leftover.startswith("autosave") and leftover.endswith(".json"):
                    os.remove(os.path.join(destination, leftover))
            reply = {"copied": destination}
        elif command == "restart":
            app.stop()  # the settings directory is the runner's own, so it stays
            self.slowed = False  # the new instance starts at real time
            self.fed_visual = False  # and with the audio callback attached
            self._launch()
            reply = {"port": self.app.port}
        elif command == "screenshot":
            args = dict(args)
            name = args.pop("name", "step-%02d" % index)
            args.setdefault("id", "window")
            args["path"] = os.path.join(self.out_dir, name + ".png")
            reply = app.call("screenshot", **args)
        elif command == "mark":
            reply = self._mark(args)
        elif command == "glide":
            reply = self._glide(args)
        elif command == "slow_motion":
            reply = app.call("slowMotion", factor=args["factor"])
            self.slowed = args["factor"] != 1
            self.slow_factor = float(args["factor"])
        elif command == "drag" and self.recorder is not None:
            reply = self._recorded_drag(args)
        elif command == "click" and self.recorder is not None:
            self._recorded_approach(args)
            reply = app.call("click", **{k: v for k, v in args.items() if k != "approach"})
        else:
            args = dict(args)
            timeout = args.pop("timeoutMs", None)
            reply = app.call(command, timeout_ms=timeout, **args) if timeout else app.call(command, **args)
        self._record(index, command, args, reply, True)
        if command in ("mouseDown", "mouseUp"):
            self.held = command == "mouseDown"  # a held press shows a gesture in flight: checked once released
        if self.recorder is not None and command not in UNCHANGED_BY:
            self.recorder.frame(self.app)
        stale = self._check_stale_pixels(index, command)
        return (False, stale) if stale else (True, "")

    def _save_project(self):
        """Saves over an earlier run's bundle. Returns an error message, or "" when it worked."""
        path = os.path.join(self.out_dir, self.scenario.save_project + ".agsproj")
        shutil.rmtree(path, ignore_errors=True)
        try:
            reply = self.app.call("saveProject", path=path)
        except TestModeError as error:
            self._record(0, "saveProject", {"path": path}, None, False, str(error))
            return str(error)
        self._record(0, "saveProject", {"path": path}, reply, True)
        moved = localise_scratch_files(path, self.settings_dir)
        if moved:
            self._record(0, "localiseFiles", {"files": moved}, None, True)
        return ""

    def _reattach_audio(self, passed, message):
        """feedVisual detached the audio callback so a display holds; a real app has it attached, so the end state of the
        scenario (and the project it saves) is read with it back. Returns the run's (passed, message)."""
        try:
            reply = self.app.call("audioPump", run=True)
        except TestModeError as error:
            self._record(0, "audioPump", {"run": True}, None, False, str(error))
            return (False, "audioPump after feedVisual: %s" % error) if passed else (passed, message)
        self._record(0, "audioPump", {"run": True}, reply, True)
        return passed, message

    def _reopen_check(self):
        """The saved project, opened in a fresh app, must still hold the scenario's last checks. Returns (passed, message)."""
        bundle = os.path.join(self.out_dir, self.scenario.save_project + ".agsproj")
        expects = [runner_reopen.project_part(_substitute(args, self.variables))
                   for args in runner_reopen.final_expects(self.scenario.steps)]
        expects = [args for args in expects if args]
        if self.scenario.realism.skip_reopen_check or not expects:
            return True, ""
        before = [runner_reopen.describe(args, self.app, list_ids) for args in expects]
        self.app.stop()  # the scenario's own instance has said what it holds; the fresh one reads the bundle
        self.slowed = False  # nothing is left to set back
        try:
            message = runner_reopen.reopen_message(self.app_factory, self.app_path, bundle, expects, before, self.remembered,
                                                   run_expect, list_ids, self._launch_arguments())
        except (TestModeError, OSError) as error:
            message = "the saved project could not be reopened: %s" % error
        self._record(0, "reopenCheck", {"bundle": bundle, "expects": len(expects)}, {"before": before}, not message, message or None)
        if message and "reopen" in self.exempt:
            print("note: %s is listed in known-realism-fails.txt: %s" % (self.scenario.name, message), file=sys.stderr)
            return True, ""
        return (False, message) if message else (True, "")

    def _write_video(self):
        video = os.path.join(self.out_dir, self.scenario.name + ".mp4")
        ok, message = self.recorder.write_video(video)
        self._record(0, "record", {"path": video}, {"frames": len(self.recorder.frames)}, ok, None if ok else message)
        if not ok:
            print("ui-verify: %s: no video: %s" % (self.scenario.name, message), file=sys.stderr)
            return
        for mark in self.marks:
            if mark["start"] is None:
                continue
            clip = os.path.join(self.out_dir, "%s-%s.mp4" % (self.scenario.name, mark["name"]))
            cut, clip_message = self.recorder.cut_clip(video, mark["start"], clip, before_s=mark["before_s"])
            if cut:
                mark["reply"]["clip"] = clip
            else:
                print("ui-verify: %s: no clip for %s: %s" % (self.scenario.name, mark["name"], clip_message),
                      file=sys.stderr)

    def _known_pops_exist(self):
        """Every listed step must be a step of this scenario that is pop-checked."""
        steps = self.scenario.steps
        for label in sorted(self.known_pops):
            number = int(KNOWN_STEP.match(label).group(1))
            command = steps[number - 1][0] if 1 <= number <= len(steps) else None
            if command == "glide":
                command = next(iter(steps[number - 1][1]["do"]))
            if command not in POP_CHECKED:
                return "known-pops.txt lists step %s of %s, which %s" % (
                    label, self.scenario.name, "does not exist" if command is None else "is %s, not a pop-checked action" % command)
        return ""

    def _run_steps(self):
        missing = self._known_pops_exist()
        if missing:
            return False, missing
        self._launch()
        if self.recorder is not None:
            self.recorder.frame(self.app)
        for index, (command, raw_args) in enumerate(self.scenario.steps, 1):
            args = _substitute(raw_args, self.variables)
            instant = args.pop("instant", None) if command not in ("expect", "mark") else None
            try:
                ok, message = self._step(index, command, args, instant)
            except TestModeError as error:
                self._record(index, command, args, None, False, str(error))
                return False, "step %d (%s): %s" % (index, command, error)
            if not ok:
                return False, "step %d: %s" % (index, message)
        if self.pop_problems:
            return False, "\n  ".join(self.pop_problems)
        return self._check_known_pops_ran()

    def _check_known_pops_ran(self):
        """A listed step that never ran (it moved, or was removed) must not stay listed."""
        for label, (ticket, _) in sorted(self.known_pops.items()):
            if label in self.pop_seen or (label[-1] in "ur" and not self.undo_available):
                continue
            return False, "known-pops.txt lists step %s of %s (%s), which has no pop check: remove it" % (
                label, self.scenario.name, ticket)
        return True, ""

    def run(self):
        """Returns (passed, message). The run log is in self.log."""
        os.makedirs(self.out_dir, exist_ok=True)
        self.settings_dir = tempfile.mkdtemp(prefix="agentsynth-scenario-")
        self.variables["SCRATCH"] = self.settings_dir
        if self.scenario.settings:
            with open(os.path.join(self.settings_dir, SETTINGS_FILE), "w", encoding="utf-8") as handle:
                handle.write(settings_xml(_substitute(self.scenario.settings, self.variables)))
        if self.scenario.record:
            frames_dir = os.path.join(self.settings_dir, "video-frames")
            os.makedirs(frames_dir)
            self.recorder = Recorder(frames_dir, self.scenario.record_target)
        try:
            passed, message = self._run_steps()
            if self.fed_visual and self.app is not None:
                passed, message = self._reattach_audio(passed, message)
            # A failed run saves too: the project and the video are how someone looks at what went wrong.
            if self.scenario.save_project and self.app is not None:
                save_error = self._save_project()
                if save_error and passed:
                    return False, "save_project: %s" % save_error
                if passed and not save_error:
                    passed, message = self._reopen_check()
            return passed, message
        finally:
            if self.app is not None:
                if self.slowed:
                    try:
                        self.app.call("slowMotion", factor=1)  # back to real time before the instance goes
                    except (TestModeError, OSError):
                        pass
                self.app.stop()
            if self.recorder is not None:
                self._write_video()
            shutil.rmtree(self.settings_dir, ignore_errors=True)
            with open(os.path.join(self.out_dir, "run.log"), "w", encoding="utf-8") as handle:
                for entry in self.log:
                    handle.write(json.dumps(entry) + "\n")


def _stale_known_scenarios():
    """Scenario names in known-pops.txt that have no file in the scenarios folder."""
    folder = os.path.join(HERE, "scenarios")
    names = {os.path.splitext(f)[0] for f in os.listdir(folder)} if os.path.isdir(folder) else set()
    listed = {name for name, _ in load_known_pops()} | {name for name, _ in runner_realism.load_known_realism()}
    return sorted(listed - names)


def _print_proof(runner):
    for mark in runner.marks:
        print("  check %d %s: still %s%s" % (mark["check"], mark["name"], mark["still"],
                                             ", clip %s" % mark["reply"]["clip"] if mark["reply"]["clip"] else ""))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("target", help="a scenario .yaml file, or a directory of them")
    parser.add_argument("--app", help="path to Agent Synth.app (default: build-ci-local)")
    parser.add_argument("--out", help="output directory (default Tools/ui-verify/out/<scenario>/)")
    parser.add_argument("--visible", action="store_true", help="show the app windows (default: hidden, no focus taken)")
    parser.add_argument("--real-window", action="store_true", help="also run `real_window: true` scenarios (visible, OS keys), "
                        "only while the machine is idle (or AGENTSYNTH_USER_AWAY=1); without it they are SKIPPED")
    parser.add_argument("--idle-seconds", type=int, default=real_window.IDLE_SECONDS,
                        help="how long the machine must be idle for --real-window (default %(default)s)")
    options = parser.parse_args(argv)
    try:
        scenarios = load_scenarios(options.target)
    except (ScenarioError, OSError) as error:
        print("ui-verify: %s" % error, file=sys.stderr)
        return 2
    try:
        stale = _stale_known_scenarios()
    except (ScenarioError, runner_realism.RealismError) as error:
        print("ui-verify: %s" % error, file=sys.stderr)
        return 2
    if stale:
        print("ui-verify: known-pops.txt or known-realism-fails.txt lists scenarios that do not exist: %s" % ", ".join(stale), file=sys.stderr)
        return 2
    failures = skipped = 0
    away = real_window.AwayGuard(options.real_window, options.idle_seconds)
    for scenario in scenarios:
        why = away.skip_reason() if scenario.realism.real_window else ""
        if why:
            skipped += 1
            print("SKIPPED %s: %s" % (scenario.name, why))
            continue
        base = os.path.abspath(options.out) if options.out else DEFAULT_OUT
        out_dir = base if options.out and len(scenarios) == 1 else os.path.join(base, scenario.name)
        started = time.time()
        try:
            runner = Runner(options.app, out_dir, scenario, visible=options.visible)
            passed, message = runner.run()
        except TestModeError as error:
            passed, message = False, "could not run: %s" % error
        seconds = time.time() - started
        if passed:
            print("PASS %s (%d steps, %.1fs) -> %s" % (scenario.name, len(scenario.steps), seconds, out_dir))
            _print_proof(runner)
        else:
            failures += 1
            print("FAIL %s (%.1fs): %s -> %s" % (scenario.name, seconds, message, out_dir))
            _print_proof(runner)
    print("%d passed, %d failed%s" % (len(scenarios) - failures - skipped, failures, ", %d skipped" % skipped if skipped else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
