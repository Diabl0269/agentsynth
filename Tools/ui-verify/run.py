#!/usr/bin/env python3
"""Runs ui-verify scenarios (YAML) against Agent Synth's in-app test mode. Standard library + PyYAML.

    python3 Tools/ui-verify/run.py <scenario.yaml | directory> [--app PATH] [--out DIR]

Each scenario launches its own test-mode instance (port 0, temporary settings directory), runs its steps, saves the
screenshots it takes, writes a JSON-lines run log and prints a summary. The exit status is non-zero when any step
failed. The format is described in docs/development/test-mode.md (Scenarios).

A scenario file is either a list of steps or a mapping with `steps:` and optional `settings:` (values, with ${FIXTURES} and ${OUT} filled in, written to the
instance's settings file before it first starts), `motion: true` (start with --automation-motion), `record: true` (<out>/<scenario>.mp4, no sound) and
`save_project: <name>` (<out>/<name>.agsproj, saved after the last step). One mapping per step, `- <command>: {args}`:

    - openProject: {path: ${FIXTURES}/macros.agsproj}   # any protocol command (click, drag, key, getState, ...)
    - click: {id: "card:LFO 1", x: 40, y: 12}
    - type: {text: delay}                               # one `key` per character
    - key: {keys: cmd+z}
    - wait: {}                                          # waitForIdle
    - restart: {}                                       # quit and start again on the same settings directory
    - screenshot: {id: window, name: after-undo}        # <out>/after-undo.png
    - expect: {id: "card:LFO 1", selected: true}        # equality on getState fields (several allowed)
    - expect: {id: timelineTransportRecord, contains: {tooltip: "Ctrl+R"}}
    - expect: {id: "card:LFO 1", exists: false}
    - expect: {count: {prefix: "card:", equals: 3}}     # list ids (or field: title) starting with the prefix

`${FIXTURES}`, `${OUT}` and `${SCRATCH}` (the run's temporary settings directory) are replaced in every string.
`- copyFixture: {name: x.agsproj}` copies `${FIXTURES}/x.agsproj` into `${SCRATCH}/x.agsproj` for a scenario that
changes its project and must not change the committed copy. An `expect` is retried for `within_ms` (default 500) so a
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
from recording import DRAG_HOLD_SECONDS, Recorder  # noqa: E402

FIXTURES = os.path.join(HERE, "fixtures")
DEFAULT_OUT = os.path.join(HERE, "out")
SETTINGS_FILE = "Agent Synth.settings"
EXPECT_KEYS = {"id", "contains", "exists", "count", "within_ms"}
RUNNER_STEPS = {"expect", "screenshot", "wait", "type", "restart", "copyFixture"}
UNCHANGED_BY = {"expect", "screenshot", "getState", "list", "ping", "copyFixture"}  # no video frame after these


class ScenarioError(ValueError):
    """The scenario file is malformed."""


class Scenario:
    def __init__(self, name, steps, settings, path=None, motion=False, record=False, save_project=None):
        self.name = name
        self.steps = steps  # [(command, args dict)]
        self.settings = settings  # {name: value}
        self.path = path
        self.motion = motion  # start with --automation-motion, for a check that needs a slide or fade to run
        self.record = record  # write <out>/<name>.mp4
        self.save_project = save_project  # write <out>/<save_project>.agsproj after the last step


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


def parse_scenario(text, name="scenario", path=None):
    """Parses scenario YAML. A step is a mapping with exactly one key (the command); its value is the args mapping."""
    import yaml

    try:
        document = yaml.safe_load(text)
    except yaml.YAMLError as error:
        raise ScenarioError("%s: not valid YAML: %s" % (name, error))
    settings = {}
    motion = record = False
    save_project = None
    if isinstance(document, dict):
        motion = bool(document.get("motion"))
        record = bool(document.get("record"))
        if document.get("save_project") is not None:
            save_project = _project_name(document["save_project"], name)
        settings = document.get("settings") or {}
        document = document.get("steps")
        if not isinstance(settings, dict):
            raise ScenarioError("%s: `settings` must be a mapping" % name)
    if not isinstance(document, list) or not document:
        raise ScenarioError("%s: expected a non-empty list of steps" % name)
    steps = []
    for index, raw in enumerate(document, 1):
        if not isinstance(raw, dict) or len(raw) != 1:
            raise ScenarioError("%s: step %d must be a mapping with one command, got %r" % (name, index, raw))
        command, args = next(iter(raw.items()))
        if args is None:
            args = {}
        if not isinstance(args, dict):
            raise ScenarioError("%s: step %d (%s) takes a mapping of arguments, got %r" % (name, index, command, args))
        steps.append((str(command), args))
    return Scenario(name, steps, settings, path, motion, record, save_project)


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


def check_expect(args, app):
    """Evaluates one expect once. Returns None when it holds, else a message naming what differed."""
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
        for key in ("contains", "exists", "within_ms"):
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
            got = state.get(field, "<missing>")
            if got != want:
                problems.append("%s of %r: expected %r, got %r" % (field, args["id"], want, got))
        for field, needle in (args.get("contains") or {}).items():
            got = str(state.get(field, ""))
            if str(needle).lower() not in got.lower():
                problems.append("%s of %r: expected it to contain %r, got %r" % (field, args["id"], needle, got))
    elif equals:
        problems.append("fields %s need an `id`" % sorted(equals))
    unknown -= {"contains", "exists", "within_ms"}
    if not args.keys() & {"id", "count"}:
        problems.append("an expect needs `id` or `count`")
    return "; ".join(problems) or None


def run_expect(args, app, sleep=time.sleep, clock=time.monotonic):
    """check_expect, retried until `within_ms` has passed. Returns (ok, message)."""
    deadline = clock() + args.get("within_ms", 500) / 1000.0
    while True:
        message = check_expect(args, app)
        if message is None:
            return True, ""
        if clock() >= deadline:
            return False, message
        sleep(0.05)


class Runner:
    def __init__(self, app_path, out_dir, scenario, app_factory=TestModeApp, visible=False):
        self.scenario = scenario
        self.out_dir = out_dir
        self.app_path = app_path
        self.app_factory = app_factory
        self.visible = visible
        self.log = []
        self.app = None
        self.settings_dir = None
        self.recorder = None
        self.variables = {"FIXTURES": FIXTURES, "OUT": out_dir}

    def _launch(self):
        extra = {"extra_args": ["--automation-motion"]} if self.scenario.motion else {}
        if self.visible:
            extra["visible"] = True
        self.app = self.app_factory(self.app_path, settings_dir=self.settings_dir, **extra)
        self.app.start()

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

    def _recorded_drag(self, args):
        """The `drag` command's own press, moves and release, one command each so a frame fits between them. A drag
        with an end outside the main window (a popup) stays one command and shows only its result."""
        start = self._point(args["from"], args.get("fromX"), args.get("fromY"))
        end = self._point(args["to"], args.get("toX"), args.get("toY"))
        if start is None or end is None:
            return self.app.call("drag", **args)
        steps = max(2, args.get("steps", 12))
        mods = {"mods": args["mods"]} if "mods" in args else {}
        self.app.call("mouseDown", id="window", x=start[0], y=start[1], **mods)
        self.recorder.frame(self.app, DRAG_HOLD_SECONDS)
        for index in range(1, steps + 1):
            x = round(start[0] + (end[0] - start[0]) * index / steps)
            y = round(start[1] + (end[1] - start[1]) * index / steps)
            self.app.call("mouseMoveHeld", id="window", x=x, y=y, **mods)
            self.recorder.frame(self.app, DRAG_HOLD_SECONDS)
        self.app.call("mouseUp", id="window", x=end[0], y=end[1], **mods)
        return {"from": args["from"], "to": args["to"]}

    def _step(self, index, command, args):
        """Runs one step. Returns (ok, error message)."""
        app = self.app
        if command == "expect":
            ok, message = run_expect(args, app)
            self._record(index, command, args, None, ok, message)
            return ok, "expect %s: %s" % (json.dumps(args), message) if not ok else ""
        if command == "wait":
            reply = app.call("waitForIdle", **args)
        elif command == "type":
            for char in str(args.get("text", "")):
                app.call("key", keys="space" if char == " " else char)  # the key parser splits on spaces
            reply = {"typed": len(str(args.get("text", "")))}
        elif command == "copyFixture":
            destination = os.path.join(self.settings_dir, args["name"])
            shutil.copytree(os.path.join(FIXTURES, args["name"]), destination)
            reply = {"copied": destination}
        elif command == "restart":
            app.stop()  # the settings directory is the runner's own, so it stays
            self._launch()
            reply = {"port": self.app.port}
        elif command == "screenshot":
            args = dict(args)
            name = args.pop("name", "step-%02d" % index)
            args.setdefault("id", "window")
            args["path"] = os.path.join(self.out_dir, name + ".png")
            reply = app.call("screenshot", **args)
        elif command == "drag" and self.recorder is not None:
            reply = self._recorded_drag(args)
        else:
            args = dict(args)
            timeout = args.pop("timeoutMs", None)
            reply = app.call(command, timeout_ms=timeout, **args) if timeout else app.call(command, **args)
        self._record(index, command, args, reply, True)
        if self.recorder is not None and command not in UNCHANGED_BY:
            self.recorder.frame(self.app)
        return True, ""

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
        return ""

    def _write_video(self):
        video = os.path.join(self.out_dir, self.scenario.name + ".mp4")
        ok, message = self.recorder.write_video(video)
        self._record(0, "record", {"path": video}, {"frames": len(self.recorder.frames)}, ok, None if ok else message)
        if not ok:
            print("ui-verify: %s: no video: %s" % (self.scenario.name, message), file=sys.stderr)

    def _run_steps(self):
        self._launch()
        if self.recorder is not None:
            self.recorder.frame(self.app)
        for index, (command, raw_args) in enumerate(self.scenario.steps, 1):
            args = _substitute(raw_args, self.variables)
            try:
                ok, message = self._step(index, command, args)
            except TestModeError as error:
                self._record(index, command, args, None, False, str(error))
                return False, "step %d (%s): %s" % (index, command, error)
            if not ok:
                return False, "step %d: %s" % (index, message)
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
            self.recorder = Recorder(frames_dir)
        try:
            passed, message = self._run_steps()
            # A failed run saves too: the project and the video are how someone looks at what went wrong.
            if self.scenario.save_project and self.app is not None:
                save_error = self._save_project()
                if save_error and passed:
                    return False, "save_project: %s" % save_error
            return passed, message
        finally:
            if self.app is not None:
                self.app.stop()
            if self.recorder is not None:
                self._write_video()
            shutil.rmtree(self.settings_dir, ignore_errors=True)
            with open(os.path.join(self.out_dir, "run.log"), "w", encoding="utf-8") as handle:
                for entry in self.log:
                    handle.write(json.dumps(entry) + "\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("target", help="a scenario .yaml file, or a directory of them")
    parser.add_argument("--app", help="path to Agent Synth.app (default: build-ci-local)")
    parser.add_argument("--out", help="output directory (default Tools/ui-verify/out/<scenario>/)")
    parser.add_argument("--visible", action="store_true", help="show the app windows (default: hidden, no focus taken)")
    options = parser.parse_args(argv)
    try:
        scenarios = load_scenarios(options.target)
    except (ScenarioError, OSError) as error:
        print("ui-verify: %s" % error, file=sys.stderr)
        return 2
    failures = 0
    for scenario in scenarios:
        base = os.path.abspath(options.out) if options.out else DEFAULT_OUT
        out_dir = base if options.out and len(scenarios) == 1 else os.path.join(base, scenario.name)
        started = time.time()
        try:
            passed, message = Runner(options.app, out_dir, scenario, visible=options.visible).run()
        except TestModeError as error:
            passed, message = False, "could not run: %s" % error
        seconds = time.time() - started
        if passed:
            print("PASS %s (%d steps, %.1fs) -> %s" % (scenario.name, len(scenario.steps), seconds, out_dir))
        else:
            failures += 1
            print("FAIL %s (%.1fs): %s -> %s" % (scenario.name, seconds, message, out_dir))
    print("%d passed, %d failed" % (len(scenarios) - failures, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
