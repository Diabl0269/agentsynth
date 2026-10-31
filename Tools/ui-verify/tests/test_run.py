"""Unit tests for run.py: scenario parsing, the expect logic and the runner loop, against a fake app (no Agent Synth)."""

import json
import os
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import run  # noqa: E402
from agentsynth_testmode import TestModeError  # noqa: E402


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
