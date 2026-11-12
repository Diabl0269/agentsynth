"""Unit tests for run.py: scenario parsing, the expect logic and the runner loop, against a fake app (no Agent Synth)."""

import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import recording  # noqa: E402
import run  # noqa: E402
from agentsynth_testmode import TestModeError  # noqa: E402


def write_png(path, width=4, height=4):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    rows = b"".join(b"\0" + b"\x80\x20\x20" * width for _ in range(height))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


class FakeApp:
    """Answers the commands run.py sends from a dict of component states."""

    def __init__(self, states=None, tree=None, app_path=None, settings_dir=None):
        self.states = states or {}
        self.tree = tree or {"id": "", "children": []}
        self.calls = []
        self.port = 1
        self.stopped = False

    def start(self):
        pass

    def stop(self):
        self.stopped = True

    def call(self, cmd, timeout_ms=None, **args):
        self.calls.append((cmd, args))
        if cmd == "getState":
            if args["id"] not in self.states:
                raise TestModeError('getState failed: no component "%s"' % args["id"])
            return self.states[args["id"]]
        if cmd == "list":
            return self.tree
        if cmd == "screenshot":
            return {"path": args["path"], "width": 1, "height": 1}
        return {}


class DrawingApp(FakeApp):
    """Screenshots write real PNGs, and saveProject a bundle, as the app does."""

    def call(self, cmd, timeout_ms=None, **args):
        if cmd == "screenshot":
            write_png(args["path"])
            return {"path": args["path"], "width": 4, "height": 4}
        if cmd == "saveProject":
            os.makedirs(args["path"])
            self.calls.append((cmd, args))
            return {"path": args["path"]}
        return super().call(cmd, timeout_ms=timeout_ms, **args)


class RecordingTests(unittest.TestCase):
    STATES = {"window": {"bounds": {"x": 0, "y": 0, "w": 800, "h": 600}},
              "a": {"bounds": {"x": 10, "y": 20, "w": 100, "h": 40}},
              "b": {"bounds": {"x": 300, "y": 400, "w": 50, "h": 50}},
              "popup": {"bounds": {"x": 900, "y": 20, "w": 50, "h": 50}}}

    def run_scenario(self, text, app, out):
        runner = run.Runner(None, out, run.parse_scenario(text, "demo"), app_factory=lambda *a, **k: app)
        encoded = []

        def write_video(recorder, path, ffmpeg=None):
            encoded.append(len(recorder.frames))
            return True, path

        with mock.patch.object(recording.Recorder, "write_video", write_video):
            return runner.run(), encoded

    def frames(self, app):
        return [c for c in app.calls if c[0] == "screenshot"]

    def test_options_parse_and_a_project_name_cannot_leave_the_output_folder(self):
        s = run.parse_scenario("record: true\nsave_project: end.agsproj\nsteps:\n  - wait: {}\n", "x")
        self.assertTrue(s.record)
        self.assertEqual(s.save_project, "end")
        self.assertFalse(run.parse_scenario("- wait: {}\n", "x").record)
        for bad in ["../up", "a/b", ".hidden", "''"]:
            with self.assertRaises(run.ScenarioError, msg=bad):
                run.parse_scenario("save_project: %s\nsteps:\n  - wait: {}\n" % bad, "x")

    def test_without_the_options_nothing_extra_is_sent(self):
        app = DrawingApp(self.STATES)
        with tempfile.TemporaryDirectory() as out:
            (ok, _), encoded = self.run_scenario("- drag: {from: a, to: b}\n", app, out)
        self.assertTrue(ok)
        self.assertEqual(encoded, [])
        self.assertEqual([c[0] for c in app.calls], ["drag"])

    def test_a_frame_follows_each_step_that_can_change_the_window(self):
        app = DrawingApp(self.STATES)
        text = "record: true\nsteps:\n  - click: {id: a, approach: false}\n  - expect: {id: a, exists: true}\n  - getState: {id: a}\n  - wait: {}\n"
        with tempfile.TemporaryDirectory() as out:
            (ok, _), encoded = self.run_scenario(text, app, out)
        self.assertTrue(ok)
        self.assertEqual(encoded, [3])  # the launch, the click, the wait

    def test_a_recorded_drag_sends_the_same_gesture_with_a_frame_at_each_move(self):
        app = DrawingApp(self.STATES)
        text = "record: true\nsteps:\n  - drag: {from: a, to: b, fromX: 5, fromY: 6, steps: 4, mods: shift}\n"
        with tempfile.TemporaryDirectory() as out:
            (ok, _), encoded = self.run_scenario(text, app, out)
        self.assertTrue(ok)
        mouse = [c for c in app.calls if c[0].startswith("mouse")]
        self.assertEqual([c[0] for c in mouse], ["mouseDown"] + ["mouseMoveHeld"] * 4 + ["mouseUp"])
        self.assertEqual((mouse[0][1]["x"], mouse[0][1]["y"]), (15, 26))  # a's origin + fromX, fromY
        self.assertEqual((mouse[-1][1]["x"], mouse[-1][1]["y"]), (325, 425))  # b's centre
        self.assertEqual(mouse[-2][1]["x"], 325)  # the last move arrives before the release
        self.assertTrue(all(c[1]["id"] == "window" and c[1]["mods"] == "shift" for c in mouse))
        self.assertEqual(encoded, [7])  # the launch, the press, four moves, the end of the step

    def test_a_recorded_drag_inside_one_component_is_sent_to_that_component_in_its_own_coordinates(self):
        # Regression test for FRO1192: a component in its own native window (a shape window) was dragged through
        # the main window's coordinates, so the recorded drag pressed somewhere else and drew nothing.
        app = DrawingApp(self.STATES)
        text = "record: true\nsteps:\n  - drag: {from: a, to: a, fromX: 5, fromY: 6, toX: 45, toY: 6, steps: 4}\n"
        with tempfile.TemporaryDirectory() as out:
            (ok, _), _ = self.run_scenario(text, app, out)
        self.assertTrue(ok)
        mouse = [c for c in app.calls if c[0].startswith("mouse")]
        self.assertEqual([c[0] for c in mouse], ["mouseDown"] + ["mouseMoveHeld"] * 4 + ["mouseUp"])
        self.assertTrue(all(c[1]["id"] == "a" for c in mouse))
        self.assertEqual([(c[1]["x"], c[1]["y"]) for c in (mouse[0], mouse[-1])], [(5, 6), (45, 6)])

    def approach_run(self, text):
        """Runs `text` against an app whose screenshots differ by pointer position. Returns the app, the frames that
        fall in the first mark's clip window (as image bytes) and the run's ok."""
        class PointerApp(DrawingApp):
            pointer = (0, 0)

            def call(self, cmd, timeout_ms=None, **args):
                if cmd in ("hover", "click"):
                    self.pointer = (args.get("x", 0), args.get("y", 0))
                if cmd == "screenshot":
                    write_png(args["path"], width=4 + self.pointer[0] % 50, height=4 + self.pointer[1] % 50)
                    return {"path": args["path"], "width": 4, "height": 4}
                return super().call(cmd, timeout_ms=timeout_ms, **args)

        app = PointerApp(self.STATES)
        seen = {}

        def write_video(recorder, path, ffmpeg=None):
            start, length = recorder.clip_window(seen["mark"])
            seen["frames"] = [open(p, "rb").read() for (p, _), t in zip(recorder.frames, recorder.timeline)
                              if start <= t < start + length]
            return True, path

        original = run.Runner._mark

        def mark(runner, args):
            reply = original(runner, args)
            seen["mark"] = runner.marks[-1]["start"]
            return reply

        with tempfile.TemporaryDirectory() as out, mock.patch.object(recording.Recorder, "write_video", write_video), \
                mock.patch.object(run.Runner, "_mark", mark):
            runner = run.Runner(None, out, run.parse_scenario(text, "demo"), app_factory=lambda *a, **k: app)
            ok, _ = runner.run()
        return app, seen["frames"], ok

    def test_a_recorded_click_films_the_pointer_arriving_so_a_click_and_mark_clip_has_four_distinct_frames(self):
        text = "record: true\nsteps:\n  - click: {id: b}\n  - mark: {check: 1, name: dot-selected}\n"
        app, frames, ok = self.approach_run(text)
        self.assertTrue(ok)
        hovers = [c[1] for c in app.calls if c[0] == "hover"]
        self.assertEqual(len(hovers), 4)
        self.assertEqual(hovers[0], {"id": "window", "x": 265, "y": 365})  # b's centre (325, 425) less 60 px
        self.assertTrue(all(0 <= h["x"] < 800 and 0 <= h["y"] < 600 for h in hovers))
        names = [c[0] for c in app.calls if c[0] in ("hover", "click")]
        self.assertEqual(names, ["hover"] * 4 + ["click"])
        self.assertGreaterEqual(len(set(frames)), 4)

    def test_approach_false_keeps_the_single_click_frame(self):
        text = "record: true\nsteps:\n  - click: {id: b, approach: false}\n  - mark: {check: 1, name: dot-selected}\n"
        app, frames, ok = self.approach_run(text)
        self.assertTrue(ok)
        self.assertEqual([c for c in app.calls if c[0] == "hover"], [])
        click = [c[1] for c in app.calls if c[0] == "click"][0]
        self.assertNotIn("approach", click)  # the opt-out is the runner's, not the protocol's
        self.assertLessEqual(len(set(frames)), 2)

    def test_a_click_whose_point_cannot_be_resolved_in_the_main_window_is_sent_as_before(self):
        for target in ("popup", "missing"):
            text = "record: true\nsteps:\n  - click: {id: %s}\n  - mark: {check: 1, name: x}\n" % target
            app, _, _ = self.approach_run(text)
            self.assertEqual([c for c in app.calls if c[0] == "hover"], [], target)
            self.assertEqual(len([c for c in app.calls if c[0] == "click"]), 1, target)

    def test_a_click_near_the_window_corner_keeps_the_approach_inside_the_window(self):
        app = DrawingApp(self.STATES)
        text = "record: true\nsteps:\n  - click: {id: a, x: 2, y: -15}\n"
        with tempfile.TemporaryDirectory() as out:
            (ok, _), _ = self.run_scenario(text, app, out)
        self.assertTrue(ok)
        for c in app.calls:
            if c[0] == "hover":
                self.assertTrue(0 <= c[1]["x"] and 0 <= c[1]["y"], c)

    def test_a_drag_leaving_the_main_window_stays_one_command(self):
        app = DrawingApp(self.STATES)
        with tempfile.TemporaryDirectory() as out:
            (ok, _), _ = self.run_scenario("record: true\nsteps:\n  - drag: {from: a, to: popup}\n", app, out)
        self.assertTrue(ok)
        self.assertEqual(len([c for c in app.calls if c[0] == "drag"]), 1)
        self.assertEqual([c for c in app.calls if c[0].startswith("mouse")], [])

    def test_save_project_writes_beside_the_screenshots_over_an_earlier_run(self):
        app = DrawingApp(self.STATES)
        with tempfile.TemporaryDirectory() as out:
            os.makedirs(os.path.join(out, "end.agsproj", "old"))
            (ok, _), _ = self.run_scenario("save_project: end\nsteps:\n  - click: {id: a}\n", app, out)
            self.assertTrue(ok)
            self.assertEqual(os.listdir(os.path.join(out, "end.agsproj")), [])
        self.assertEqual(app.calls[-1][0], "saveProject")

    def test_a_failed_scenario_still_leaves_its_video_and_project(self):
        app = DrawingApp(self.STATES)
        text = "record: true\nsave_project: end\nsteps:\n  - click: {id: a}\n  - expect: {id: a, exists: false, within_ms: 0}\n"
        with tempfile.TemporaryDirectory() as out:
            (ok, message), encoded = self.run_scenario(text, app, out)
            self.assertTrue(os.path.isdir(os.path.join(out, "end.agsproj")))
        self.assertFalse(ok)
        self.assertIn("step 2", message)
        self.assertEqual(encoded, [6])  # the launch, four approach frames, the click

    def test_a_save_that_fails_fails_a_passing_scenario(self):
        class NoSave(DrawingApp):
            def call(self, cmd, timeout_ms=None, **args):
                if cmd == "saveProject":
                    raise run.TestModeError("saveProject failed: could not save")
                return super().call(cmd, timeout_ms=timeout_ms, **args)

        with tempfile.TemporaryDirectory() as out:
            (ok, message), _ = self.run_scenario("save_project: end\nsteps:\n  - wait: {}\n", NoSave(self.STATES), out)
        self.assertFalse(ok)
        self.assertIn("save_project", message)

    def test_no_ffmpeg_means_no_video_and_says_so_without_failing_the_scenario(self):
        app = DrawingApp(self.STATES)
        with tempfile.TemporaryDirectory() as out, mock.patch.object(recording, "find_ffmpeg", lambda: None), \
                mock.patch("sys.stderr") as stderr:
            runner = run.Runner(None, out, run.parse_scenario("record: true\nsteps:\n  - wait: {}\n", "demo"),
                                app_factory=lambda *a, **k: app)
            ok, _ = runner.run()
            self.assertFalse(os.path.exists(os.path.join(out, "demo.mp4")))
        self.assertTrue(ok)
        self.assertIn("ffmpeg not found", "".join(str(c) for c in stderr.write.call_args_list))

    @unittest.skipUnless(recording.find_ffmpeg(), "needs ffmpeg")
    def test_ffmpeg_joins_frames_of_odd_and_changing_sizes_into_a_video(self):
        with tempfile.TemporaryDirectory() as d:
            recorder = recording.Recorder(d)

            class Sized:
                sizes = iter([(5, 7), (8, 8)])

                def call(self, cmd, **args):
                    width, height = next(self.sizes)
                    write_png(args["path"], width, height)
                    return {"width": width, "height": height}

            recorder.frame(Sized())
            recorder.frame(Sized())
            video = os.path.join(d, "out.mp4")
            ok, message = recorder.write_video(video)
            self.assertTrue(ok, message)
            probe = subprocess.run([recording.find_ffmpeg(), "-hide_banner", "-i", video], capture_output=True, text=True)
            self.assertIn("h264", probe.stderr)
            self.assertIn("yuv420p", probe.stderr)
            self.assertIn("6x8", probe.stderr)  # the first frame's size, made even


class ParseTests(unittest.TestCase):
    def test_a_list_of_single_key_steps(self):
        s = run.parse_scenario("- click: {id: a}\n- wait: {}\n- key: {keys: cmd+z}\n", "x")
        self.assertEqual([c for c, _ in s.steps], ["click", "wait", "key"])
        self.assertEqual(s.steps[0][1], {"id": "a"})
        self.assertEqual(s.settings, {})

    def test_a_mapping_carries_settings_and_steps(self):
        s = run.parse_scenario("settings: {k: '1'}\nsteps:\n  - wait: {}\n", "x")
        self.assertEqual(s.settings, {"k": "1"})
        self.assertEqual(len(s.steps), 1)

    def test_a_bare_command_has_no_args(self):
        self.assertEqual(run.parse_scenario("- wait:\n", "x").steps, [("wait", {})])

    def test_malformed_steps_are_refused(self):
        for text in ["", "- a: {}\n  b: {}\n", "- click: 3\n", "- just a string\n", "steps: []\n"]:
            with self.assertRaises(run.ScenarioError, msg=text):
                run.parse_scenario(text, "x")

    def test_variables_are_substituted_in_nested_values(self):
        out = run._substitute({"a": ["${FIXTURES}/p"], "b": 3}, {"FIXTURES": "/f"})
        self.assertEqual(out, {"a": ["/f/p"], "b": 3})

    def test_settings_xml_escapes_and_writes_values(self):
        xml = run.settings_xml({"shortcut_a": "0:0", "flag": True, "q": 'a"<b'})
        self.assertIn('<VALUE name="shortcut_a" val="0:0"/>', xml)
        self.assertIn('<VALUE name="flag" val="1"/>', xml)
        self.assertIn("a&quot;&lt;b", xml)

    def test_a_directory_loads_every_yaml_sorted_and_skips_underscored(self):
        with tempfile.TemporaryDirectory() as d:
            for name in ("b.yaml", "a.yaml", "_skip.yaml", "note.txt"):
                with open(os.path.join(d, name), "w") as f:
                    f.write("- wait: {}\n")
            self.assertEqual([s.name for s in run.load_scenarios(d)], ["a", "b"])


class ExpectTests(unittest.TestCase):
    def app(self):
        tree = {"id": "", "children": [{"id": "card:A", "title": "A", "children": [{"id": "card:B", "title": "x", "children": []}]},
                                       {"id": "other", "title": "card-ish", "children": []}]}
        return FakeApp({"c": {"selected": True, "title": "Record (Ctrl+R)", "macro": ""}}, [tree])

    def test_equality_passes_and_fails_with_the_field_named(self):
        app = self.app()
        self.assertIsNone(run.check_expect({"id": "c", "selected": True}, app))
        self.assertIn("macro of 'c': expected 'M', got ''", run.check_expect({"id": "c", "macro": "M"}, app))

    def test_contains_is_case_insensitive(self):
        app = self.app()
        self.assertIsNone(run.check_expect({"id": "c", "contains": {"title": "ctrl+r"}}, app))
        self.assertIn("contain", run.check_expect({"id": "c", "contains": {"title": "Cmd"}}, app))

    def test_exists_false_passes_only_when_the_component_is_missing(self):
        app = self.app()
        self.assertIsNone(run.check_expect({"id": "gone", "exists": False}, app))
        self.assertIn("exists but should not", run.check_expect({"id": "c", "exists": False}, app))
        self.assertIn("no state", run.check_expect({"id": "gone"}, app))

    def test_count_over_ids_and_titles(self):
        app = self.app()
        self.assertIsNone(run.check_expect({"count": {"prefix": "card:", "equals": 2}}, app))
        self.assertIn("expected 3, got 2", run.check_expect({"count": {"prefix": "card:", "equals": 3}}, app))
        self.assertIsNone(run.check_expect({"count": {"prefix": "card", "field": "title", "equals": 1}}, app))

    def test_an_expect_needs_an_id_or_a_count(self):
        self.assertIn("needs", run.check_expect({"selected": True}, self.app()))

    def test_retry_waits_until_the_value_arrives_and_then_gives_up(self):
        app = self.app()
        clock = {"t": 0.0}
        flips = iter([False, False, True])

        def sleep(s):
            clock["t"] += s
            if next(flips, False):
                app.states["c"]["macro"] = "M"

        ok, _ = run.run_expect({"id": "c", "macro": "M", "within_ms": 1000}, app, sleep, lambda: clock["t"])
        self.assertTrue(ok)
        ok, message = run.run_expect({"id": "c", "macro": "Z", "within_ms": 100}, app, sleep, lambda: clock["t"])
        self.assertFalse(ok)
        self.assertIn("expected 'Z'", message)


class RunnerTests(unittest.TestCase):
    def run_steps(self, text, app):
        with tempfile.TemporaryDirectory() as out:
            scenario = run.parse_scenario(text, "demo")
            runner = run.Runner(None, out, scenario, app_factory=lambda *a, **k: app)
            result = runner.run()
            with open(os.path.join(out, "run.log")) as f:
                log = [json.loads(line) for line in f]
            return result, log

    def test_a_passing_scenario_logs_every_step(self):
        app = FakeApp({"c": {"selected": True}})
        (ok, _), log = self.run_steps("- click: {id: c}\n- type: {text: ab}\n- wait: {}\n"
                                      "- expect: {id: c, selected: true}\n- screenshot: {name: shot}\n", app)
        self.assertTrue(ok)
        self.assertEqual([e["cmd"] for e in log], ["click", "type", "wait", "expect", "screenshot"])
        self.assertTrue(all(e["pass"] for e in log))
        self.assertEqual([c for c in app.calls if c[0] == "key"], [("key", {"keys": "a"}), ("key", {"keys": "b"})])
        self.assertTrue(app.stopped)

    def test_visible_is_passed_to_the_app_only_when_asked(self):
        seen = []

        def factory(*a, **k):
            seen.append(k)
            return FakeApp({})

        for visible in (False, True):
            with tempfile.TemporaryDirectory() as out:
                run.Runner(None, out, run.parse_scenario("- wait: {}\n", "demo"), app_factory=factory, visible=visible).run()
        self.assertNotIn("visible", seen[0])
        self.assertTrue(seen[1]["visible"])

    def test_type_sends_a_space_as_the_space_key(self):
        app = FakeApp({})
        (ok, _), _ = self.run_steps("- type: {text: a b}\n", app)
        self.assertTrue(ok)
        self.assertEqual([c[1]["keys"] for c in app.calls if c[0] == "key"], ["a", "space", "b"])

    def test_settings_fill_in_the_fixtures_folder(self):
        app = FakeApp({})
        with tempfile.TemporaryDirectory() as out:
            scenario = run.parse_scenario("settings:\n  folder: \"${FIXTURES}/sounds\"\nsteps:\n  - wait: {}\n", "demo")
            runner = run.Runner(None, out, scenario, app_factory=lambda *a, **k: app)
            written = {}
            real_launch = runner._launch
            def launch():
                with open(os.path.join(runner.settings_dir, run.SETTINGS_FILE)) as f:
                    written["xml"] = f.read()
                real_launch()
            runner._launch = launch
            runner.run()
        self.assertIn(os.path.join(run.FIXTURES, "sounds"), written["xml"])

    def test_a_failed_expect_stops_the_scenario_and_is_named(self):
        app = FakeApp({"c": {"selected": False}})
        (ok, message), log = self.run_steps("- expect: {id: c, selected: true, within_ms: 0}\n- click: {id: c}\n", app)
        self.assertFalse(ok)
        self.assertIn("step 1", message)
        self.assertIn("selected of 'c': expected True, got False", message)
        self.assertEqual(len(log), 1)
        self.assertFalse(log[0]["pass"])

    def test_a_command_error_fails_the_step(self):
        app = FakeApp()
        (ok, message), log = self.run_steps("- getState: {id: nope}\n", app)
        self.assertFalse(ok)
        self.assertIn("no component", message)
        self.assertEqual(log[0]["pass"], False)

    def test_restart_starts_the_app_again(self):
        starts = []

        class Counting(FakeApp):
            def start(self):
                starts.append(1)

        app = Counting()
        (ok, _), _ = self.run_steps("- restart: {}\n- wait: {}\n", app)
        self.assertTrue(ok)
        self.assertEqual(len(starts), 2)  # the launch and the restart

    def test_copy_fixture_copies_a_fixture_and_scratch_resolves_to_it(self):
        with tempfile.TemporaryDirectory() as fixtures:
            project = os.path.join(fixtures, "demo.agsproj")
            os.makedirs(os.path.join(project, "Audio"))
            with open(os.path.join(project, "Audio", "tone.wav"), "w") as f:
                f.write("wav")
            seen = {}

            class Checking(FakeApp):
                def call(self, cmd, timeout_ms=None, **args):
                    if cmd == "openProject":
                        seen["path"] = args["path"]
                        seen["copied"] = os.path.isfile(os.path.join(args["path"], "Audio", "tone.wav"))
                    return super().call(cmd, timeout_ms=timeout_ms, **args)

            with mock.patch.object(run, "FIXTURES", fixtures):
                (ok, _), log = self.run_steps('- copyFixture: {name: demo.agsproj}\n'
                                              '- openProject: {path: "${SCRATCH}/demo.agsproj"}\n', Checking())
            self.assertTrue(ok)
            self.assertTrue(seen["copied"])
            self.assertEqual(os.path.basename(seen["path"]), "demo.agsproj")
            self.assertNotEqual(os.path.dirname(seen["path"]), fixtures)
            self.assertTrue(os.path.isfile(os.path.join(project, "Audio", "tone.wav")))  # the original is untouched
            self.assertEqual([e["cmd"] for e in log], ["copyFixture", "openProject"])


class StalePixelCheckTests(unittest.TestCase):
    def run_steps(self, text, app):
        with tempfile.TemporaryDirectory() as out:
            runner = run.Runner(None, out, run.parse_scenario(text, "demo"), app_factory=lambda *a, **k: app)
            return runner.run(), out

    def test_the_option_parses_as_true_or_a_mapping_and_refuses_anything_else(self):
        self.assertIsNone(run.parse_scenario("- wait: {}\n", "x").stale_pixel_check)
        self.assertEqual(run.parse_scenario("stale_pixel_check: true\nsteps:\n  - wait: {}\n", "x").stale_pixel_check, {})
        s = run.parse_scenario("stale_pixel_check: {ignore: [caret], settle_ms: 900}\nsteps:\n  - wait: {}\n", "x")
        self.assertEqual(s.stale_pixel_check, {"ignore": ["caret"], "settle_ms": 900})
        for bad in ["3", "{nope: 1}", "{ignore: caret}"]:
            with self.assertRaises(run.ScenarioError, msg=bad):
                run.parse_scenario("stale_pixel_check: %s\nsteps:\n  - wait: {}\n" % bad, "x")

    def test_a_check_follows_each_step_that_can_change_the_window_but_not_a_held_press(self):
        app = FakeApp({})
        text = ("stale_pixel_check: {ignore: [caret], settle_ms: 900}\nsteps:\n  - click: {id: a}\n"
                "  - expect: {id: a, exists: false}\n  - wait: {}\n  - mouseDown: {id: a}\n  - mouseMoveHeld: {id: a}\n"
                "  - key: {keys: escape}\n  - mouseUp: {id: a}\n  - key: {keys: cmd+z}\n")
        (ok, _), _ = self.run_steps(text, app)
        self.assertTrue(ok)
        names = [c[0] for c in app.calls]
        self.assertEqual(names[0], "trackPresentedFrame")
        checks = [c[1] for c in app.calls if c[0] == "checkStalePixels"]
        self.assertEqual([os.path.basename(c["dir"]) for c in checks], ["stale-step-01", "stale-step-07", "stale-step-08"])
        self.assertEqual(checks[0]["ignore"], ["caret"])
        self.assertEqual(checks[0]["settleMs"], 900)

    def test_without_the_option_nothing_is_checked(self):
        app = FakeApp({})
        (ok, _), _ = self.run_steps("- click: {id: a}\n", app)
        self.assertTrue(ok)
        self.assertEqual([c[0] for c in app.calls], ["click"])

    def test_a_stale_pixel_fails_the_scenario_at_that_step_with_the_app_s_message(self):
        class Stale(FakeApp):
            def call(self, cmd, timeout_ms=None, **args):
                if cmd == "checkStalePixels" and len([c for c in self.calls if c[0] == "checkStalePixels"]) == 1:
                    self.calls.append((cmd, args))
                    raise TestModeError("checkStalePixels failed: 12 pixels differ from a full repaint")
                return super().call(cmd, timeout_ms=timeout_ms, **args)

        app = Stale({})
        (ok, message), _ = self.run_steps("stale_pixel_check: true\nsteps:\n  - click: {id: a}\n"
                                          "  - key: {keys: cmd+z}\n  - click: {id: b}\n", app)
        self.assertFalse(ok)
        self.assertIn("step 2", message)
        self.assertIn("stale pixels after key", message)
        self.assertIn("12 pixels differ", message)
        self.assertNotIn(("click", {"id": "b"}), app.calls)

    def test_a_restart_follows_the_new_instance(self):
        app = FakeApp({})
        (ok, _), _ = self.run_steps("stale_pixel_check: true\nsteps:\n  - restart: {}\n  - click: {id: a}\n", app)
        self.assertTrue(ok)
        self.assertEqual([c[0] for c in app.calls], ["trackPresentedFrame", "trackPresentedFrame", "click",
                                                     "checkStalePixels"])


class PopApp(FakeApp):
    """Answers popWatchCheck from a script: one reply per check, in order (the last repeats)."""

    def __init__(self, replies, **kwargs):
        super().__init__(**kwargs)
        self.replies = list(replies)

    def call(self, cmd, timeout_ms=None, **args):
        if cmd == "popWatchCheck":
            self.calls.append((cmd, args))
            return self.replies.pop(0) if len(self.replies) > 1 else self.replies[0]
        return super().call(cmd, timeout_ms=timeout_ms, **args)


SETTLED = {"changed": False, "framesSeen": 3}
POPPED = {"changed": True, "pop": True, "regionX": 1, "regionY": 2, "regionW": 30, "regionH": 40, "regionPixels": 900,
          "framesSeen": 2, "intermediateFrames": 0}
FADED = dict(POPPED, pop=False, intermediateFrames=4)


def edit(reply, before=1, after=2):
    return dict(reply, undoSerialBefore=before, undoSerialAfter=after)


class PopCheckTests(unittest.TestCase):
    MOTION = "motion: true\nsteps:\n"

    def run_steps(self, text, app, known=None):
        with tempfile.TemporaryDirectory() as out:
            runner = run.Runner(None, out, run.parse_scenario(text, "demo"), app_factory=lambda *a, **k: app,
                                known_pops=known or {})
            return runner.run(), out, runner

    def names(self, app):
        return [c[0] for c in app.calls]

    def test_the_check_is_on_under_motion_and_off_without_it(self):
        self.assertEqual(run.parse_scenario(self.MOTION + "  - wait: {}\n", "x").pop_check, {})
        self.assertIsNone(run.parse_scenario("- wait: {}\n", "x").pop_check)
        s = run.parse_scenario("motion: true\npop_check: {ignore: [caret], ms: 400, undo: false}\nsteps:\n  - wait: {}\n", "x")
        self.assertEqual(s.pop_check, {"ignore": ["caret"], "ms": 400, "undo": False})

    def test_turning_it_off_needs_a_reason(self):
        text = "motion: true\npop_check: %s\nsteps:\n  - wait: {}\n"
        self.assertIsNone(run.parse_scenario(text % '{off: "a text editor"}', "x").pop_check)
        for bad in ["false", "{off: ''}", "{off: 3}", "{off: why, ms: 5}", "{nope: 1}", "{ms: -1}", "{undo: 2}", "{ignore: a}"]:
            with self.assertRaises(run.ScenarioError, msg=bad):
                run.parse_scenario(text % bad, "x")

    def test_an_instant_step_needs_a_reason(self):
        with self.assertRaises(run.ScenarioError):
            run.parse_scenario(self.MOTION + "  - click: {id: a, instant: ''}\n", "x")

    def test_a_watch_goes_around_each_action_that_can_change_the_window_and_only_those(self):
        app = PopApp([SETTLED], states={"a": {"id": "a"}})
        text = (self.MOTION + "  - click: {id: a}\n  - hover: {id: a}\n  - type: {text: x}\n  - wait: {}\n  - mouseDown: {id: a}\n"
                "  - mouseMoveHeld: {id: a}\n  - mouseUp: {id: a}\n  - keyDown: {keys: right}\n  - keyUp: {}\n"
                "  - expect: {id: a, exists: true}\n  - getState: {id: a}\n  - drag: {from: a, to: a}\n"
                "  - wheel: {id: a, dy: 1}\n  - key: {keys: escape}\n")
        (ok, message), out, _ = self.run_steps(text, app)
        self.assertTrue(ok, message)
        checked = [c[1]["dir"] for c in app.calls if c[0] == "popWatchCheck"]
        self.assertEqual([os.path.basename(d) for d in checked],
                         ["pop-step-01", "pop-step-07", "pop-step-09", "pop-step-12", "pop-step-13", "pop-step-14"])
        self.assertEqual(self.names(app).count("popWatchStart"), 6)
        # The watch starts before the action and the check comes after it.
        self.assertEqual(self.names(app)[:3], ["popWatchStart", "click", "popWatchCheck"])
        self.assertNotIn("instant", " ".join(self.names(app)))

    def test_nothing_is_watched_without_the_option(self):
        app = PopApp([POPPED])
        (ok, _), _, _ = self.run_steps("- click: {id: a}\n", app)
        self.assertTrue(ok)
        self.assertEqual(self.names(app), ["click"])

    def test_a_pop_on_an_unlisted_step_fails_with_the_region_and_the_folder(self):
        app = PopApp([POPPED])
        (ok, message), out, _ = self.run_steps(self.MOTION + "  - click: {id: a}\n  - click: {id: b}\n", app)
        self.assertFalse(ok)
        self.assertIn("step 1", message)
        self.assertIn("x 1 y 2 w 30 h 40", message)
        self.assertIn(os.path.join(out, "pop-step-01"), message)
        self.assertIn("step 2", message)  # the run goes on, so one run reports every pop

    def test_a_fade_and_a_step_that_changes_nothing_pass(self):
        app = PopApp([FADED, SETTLED])
        (ok, message), _, _ = self.run_steps(self.MOTION + "  - click: {id: a}\n  - click: {id: b}\n", app)
        self.assertTrue(ok, message)

    def test_an_instant_step_may_pop_and_the_reason_is_not_sent_to_the_app(self):
        app = PopApp([POPPED])
        (ok, message), _, _ = self.run_steps(self.MOTION + "  - click: {id: a, instant: a text value}\n", app)
        self.assertTrue(ok, message)
        self.assertIn(("click", {"id": "a"}), app.calls)

    def test_a_known_pop_passes_and_says_so(self):
        app = PopApp([POPPED])
        known = {("demo", "1"): ("FRO9", "[timeline] swaps in one frame")}
        with mock.patch("sys.stdout") as stdout:
            (ok, message), _, _ = self.run_steps(self.MOTION + "  - click: {id: a}\n", app, known)
        self.assertTrue(ok, message)
        self.assertIn("known pop", "".join(c.args[0] for c in stdout.write.call_args_list))

    def test_a_listed_step_that_no_longer_pops_passes_with_a_note_to_remove_it(self):
        app = PopApp([FADED])
        known = {("demo", "1"): ("FRO9", "[timeline] swaps")}
        with mock.patch("sys.stderr") as stderr:
            (ok, message), _, _ = self.run_steps(self.MOTION + "  - click: {id: a}\n", app, known)
        self.assertTrue(ok, message)
        self.assertIn("remove it from known-pops.txt", "".join(c.args[0] for c in stderr.write.call_args_list))

    def test_a_listed_step_that_does_not_exist_or_is_not_checked_fails(self):
        for label, steps in [("3", "  - click: {id: a}\n"), ("2", "  - click: {id: a}\n  - hover: {id: a}\n")]:
            app = PopApp([POPPED])
            (ok, message), _, _ = self.run_steps(self.MOTION + steps, app, {("demo", label): ("FRO9", "x")})
            self.assertFalse(ok, label)
            self.assertIn("known-pops.txt lists step %s" % label, message)
            self.assertEqual(app.calls, [])

    def test_undo_and_redo_are_checked_only_after_an_edit(self):
        app = PopApp([SETTLED, edit(SETTLED), SETTLED, SETTLED])
        (ok, message), _, _ = self.run_steps(self.MOTION + "  - click: {id: a}\n  - click: {id: b}\n", app)
        self.assertTrue(ok, message)
        buttons = [c[1]["id"] for c in app.calls if c[0] == "click" and c[1]["id"].endswith("Button")]
        self.assertEqual(buttons, ["undoButton", "redoButton"])
        dirs = [os.path.basename(c[1]["dir"]) for c in app.calls if c[0] == "popWatchCheck"]
        self.assertEqual(dirs, ["pop-step-01", "pop-step-02", "pop-step-02u", "pop-step-02r"])

    def test_a_pop_in_an_undo_fails_unless_that_step_is_listed(self):
        replies = [edit(SETTLED), POPPED, SETTLED]
        (ok, message), _, _ = self.run_steps(self.MOTION + "  - click: {id: a}\n", PopApp(replies))
        self.assertFalse(ok)
        self.assertIn("undo", message)
        known = {("demo", "1u"): ("FRO9", "[graph] undo swaps"), }
        (ok, message), _, _ = self.run_steps(self.MOTION + "  - click: {id: a}\n", PopApp(replies), known)
        self.assertTrue(ok, message)

    def test_undo_checks_can_be_turned_off(self):
        app = PopApp([edit(SETTLED)])
        text = "motion: true\npop_check: {undo: false}\nsteps:\n  - click: {id: a}\n"
        (ok, _), _, _ = self.run_steps(text, app)
        self.assertTrue(ok)
        self.assertEqual(self.names(app).count("click"), 1)

    def test_without_an_undo_serial_undo_is_skipped_and_one_line_says_so(self):
        app = PopApp([SETTLED])
        with mock.patch("sys.stdout") as stdout:
            (ok, _), _, _ = self.run_steps(self.MOTION + "  - click: {id: a}\n  - click: {id: b}\n", app)
        self.assertTrue(ok)
        self.assertEqual(self.names(app).count("click"), 2)
        self.assertEqual("".join(c.args[0] for c in stdout.write.call_args_list).count("no undo serial"), 1)

    def test_a_glide_is_watched_by_its_do_and_the_frames_are_still_filmed(self):
        app = PopApp([SETTLED])
        text = ("record: true\nmotion: true\nsteps:\n  - glide: {name: g, do: {click: {id: a}}, frames: 4, instant: why}\n"
                "  - glide: {name: h, do: {hover: {id: a}}, frames: 4}\n")
        with tempfile.TemporaryDirectory() as out:
            runner = run.Runner(None, out, run.parse_scenario(text, "demo"), app_factory=lambda *a, **k: app, known_pops={})
            runner._glide = lambda args: {"do": next(iter(args["do"]))}
            runner.recorder = None
            ok, message = runner._run_steps()
        self.assertTrue(ok, message)
        self.assertEqual(self.names(app).count("popWatchStart"), 1)

    def test_known_pops_file_parses_comments_and_refuses_a_malformed_line(self):
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "k.txt")
            with open(path, "w") as f:
                f.write("# c\n\nsc 05u FRO1 [area] it pops\nsc 7 FRO2 reason\n")
            self.assertEqual(run.load_known_pops(path), {("sc", "5u"): ("FRO1", "[area] it pops"), ("sc", "7"): ("FRO2", "reason")})
            with open(path, "w") as f:
                f.write("sc x FRO1 reason\n")
            with self.assertRaises(run.ScenarioError):
                run.load_known_pops(path)

    def test_every_listed_scenario_in_the_shipped_file_exists(self):
        self.assertEqual(run._stale_known_scenarios(), [])


class ShotLoggingApp(DrawingApp):
    def call(self, cmd, timeout_ms=None, **args):
        if cmd == "screenshot":
            self.calls.append((cmd, args))
        return super().call(cmd, timeout_ms=timeout_ms, **args)


class ProofMarkTests(unittest.TestCase):
    def run_scenario(self, text, out):
        app = ShotLoggingApp({})
        runner = run.Runner(None, out, run.parse_scenario(text, "demo"), app_factory=lambda *a, **k: app)
        return runner, app, runner.run()

    def test_a_record_frame_is_the_screen_unless_a_target_is_given(self):
        with tempfile.TemporaryDirectory() as out:
            _, app, (ok, _) = self.run_scenario("record: true\nsteps:\n  - wait: {}\n", out)
            self.assertTrue(ok)
            self.assertEqual({c[1]["id"] for c in app.calls if c[0] == "screenshot"}, {"screen"})
            _, app, _ = self.run_scenario("record: true\nrecord_target: {id: picker, scale: 2}\nsteps:\n  - wait: {}\n", out)
            self.assertEqual({c[1]["id"] for c in app.calls if c[0] == "screenshot"}, {"picker"})

    def test_a_mark_saves_the_still_and_cuts_a_clip_around_it(self):
        text = "record: true\nsteps:\n  - wait: {}\n  - mark: {check: 3, name: picker-open}\n  - wait: {}\n"
        with tempfile.TemporaryDirectory() as out:
            runner, _, (ok, message) = self.run_scenario(text, out)
            self.assertTrue(ok, message)
            still = os.path.join(out, "proof", "picker-open.png")
            self.assertTrue(os.path.getsize(still) > 0)
            entry = [e for e in runner.log if e["cmd"] == "mark"][0]
            self.assertEqual((entry["reply"]["check"], entry["reply"]["name"], entry["reply"]["still"]),
                             (3, "picker-open", still))
            if recording.find_ffmpeg() is None:
                self.skipTest("no ffmpeg, so no clip")
            clip = os.path.join(out, "demo-picker-open.mp4")
            self.assertTrue(os.path.getsize(clip) > 0)
            self.assertEqual(entry["reply"]["clip"], clip)

    def test_a_mark_without_record_still_saves_the_still_and_no_clip(self):
        with tempfile.TemporaryDirectory() as out:
            runner, _, (ok, _) = self.run_scenario("steps:\n  - mark: {check: 1, name: only-still}\n", out)
            self.assertTrue(ok)
            self.assertTrue(os.path.isfile(os.path.join(out, "proof", "only-still.png")))
            self.assertEqual([f for f in os.listdir(out) if f.endswith(".mp4")], [])
            self.assertIsNone(runner.marks[0]["reply"]["clip"])

    def test_malformed_marks_are_refused_at_parse_time(self):
        good = "check: 1, name: a-b"
        bad = ["{name: a}", "{check: 1}", "{check: x, name: a}", "{check: 0, name: a}", "{check: 1.5, name: a}",
               "{check: true, name: a}", "{check: 1, name: Upper}", "{check: 1, name: has_underscore}",
               "{check: 1, name: ''}", "{check: 1, name: a, extra: 1}"]
        run.parse_scenario("steps:\n  - mark: {%s}\n" % good, "x")
        for args in bad:
            with self.assertRaises(run.ScenarioError, msg=args):
                run.parse_scenario("steps:\n  - mark: %s\n" % args, "x")
        with self.assertRaises(run.ScenarioError):  # a name is used once per scenario
            run.parse_scenario("steps:\n  - mark: {%s}\n  - mark: {check: 2, name: a-b}\n" % good, "x")

    def test_a_glide_films_a_burst_of_frames_after_its_command_and_tiles_a_strip(self):
        text = ("record: true\nsteps:\n  - glide: {name: slide, frames: 6, do: {click: {id: a, x: 1, y: 2}}}\n"
                "  - mark: {check: 1, name: landed}\n")
        with tempfile.TemporaryDirectory() as out:
            runner, app, (ok, message) = self.run_scenario(text, out)
            self.assertTrue(ok, message)
            self.assertEqual([c[0] for c in app.calls].count("click"), 1)
            self.assertGreaterEqual([c[0] for c in app.calls].count("screenshot"), 7)  # six glide frames, then the mark's
            entry = [e for e in runner.log if e["cmd"] == "glide"][0]
            self.assertEqual(entry["reply"]["frames"], 6)
            if recording.find_ffmpeg() is None:
                self.skipTest("no ffmpeg, so no strip")
            self.assertTrue(os.path.getsize(os.path.join(out, "proof", "slide-strip.png")) > 0)

    def test_malformed_glides_are_refused_at_parse_time(self):
        ok = "record: true\nsteps:\n  - glide: %s\n"
        run.parse_scenario(ok % "{name: a, do: {key: {keys: cmd+z}}, crop: {x: 0, y: 0, w: 5, h: 5}}", "x")
        bad = ["{do: {click: {id: a}}}", "{name: a}", "{name: A, do: {click: {id: a}}}", "{name: a, do: {mark: {}}}",
               "{name: a, do: {click: {id: a}}, frames: 3}", "{name: a, do: {click: {id: a}}, frames: 41}",
               "{name: a, do: {click: {id: a}}, crop: {x: 0, y: 0, w: 0, h: 5}}", "{name: a, do: {click: 1}}"]
        for args in bad:
            with self.assertRaises(run.ScenarioError, msg=args):
                run.parse_scenario(ok % args, "x")
        with self.assertRaises(run.ScenarioError):  # it films, so it needs record
            run.parse_scenario("steps:\n  - glide: {name: a, do: {click: {id: a}}}\n", "x")

    def test_the_clip_window_is_two_and_a_half_before_to_three_after_within_four_to_six_seconds(self):
        recorder = recording.Recorder("unused")
        recorder.frames = [("f", 1.0)] * 40  # 39 s and the last frame's hold
        start, length = recorder.clip_window(20.0)
        self.assertEqual((start, length), (17.5, 5.5))
        start, length = recorder.clip_window(0.5)  # near the start: clamped, still four seconds
        self.assertEqual((start, length), (0.0, 4.0))
        start, length = recorder.clip_window(39.0)  # near the end: slides back to keep four seconds
        self.assertEqual(round(start + length, 3), round(recorder.duration(), 3))
        self.assertGreaterEqual(length, 4.0)


if __name__ == "__main__":
    unittest.main()
