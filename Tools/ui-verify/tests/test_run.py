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
        text = "record: true\nsteps:\n  - click: {id: a}\n  - expect: {id: a, exists: true}\n  - getState: {id: a}\n  - wait: {}\n"
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
        self.assertEqual(encoded, [2])

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


if __name__ == "__main__":
    unittest.main()
