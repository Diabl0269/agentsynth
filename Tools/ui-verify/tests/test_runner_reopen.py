"""Unit tests for runner_reopen.py and its use in run.py (FRO2038): the saved project reopens in the state the
scenario's last checks name. Fake apps, no Agent Synth."""

import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
import run  # noqa: E402
import runner_reopen  # noqa: E402
from test_run import DrawingApp  # noqa: E402

SAVE = "save_project: end\nclick_through_windows: fake app\n%ssteps:\n  - click: {id: knob}\n%s"
KEEPS = "  - expect: {id: knob, value: 5}\n"


def knob(value):
    return {"knob": {"value": value}}


def run_with(text, *apps, **options):
    """Runs a scenario; the factory hands out `apps` in the order they are started."""
    queue = list(apps)
    launches = []

    def factory(app_path, settings_dir=None, **extra):
        launches.append((settings_dir, extra))
        return queue.pop(0)

    with tempfile.TemporaryDirectory() as out:
        runner = run.Runner(None, out, run.parse_scenario(text, "demo"), app_factory=factory, known_pops={},
                            known_realism=options.pop("known_realism", {}), **options)
        return runner.run(), launches, out


class FinalExpectsTests(unittest.TestCase):
    def steps(self, text):
        return run.parse_scenario(text, "x").steps

    def test_the_trailing_expects_are_taken_skipping_steps_that_change_nothing(self):
        steps = self.steps("steps:\n  - expect: {id: a, v: 1}\n  - click: {id: b}\n  - expect: {id: c, v: 2}\n  - wait: {}\n"
                           "  - mark: {check: 1, name: m}\n  - screenshot: {name: s}\n  - expect: {id: d, v: 3}\n")
        self.assertEqual([a["id"] for a in runner_reopen.final_expects(steps)], ["c", "d"])

    def test_a_scenario_that_ends_on_a_change_has_none(self):
        self.assertEqual(runner_reopen.final_expects(self.steps("steps:\n  - expect: {id: a, v: 1}\n  - click: {id: b}\n")), [])


class ProjectPartTests(unittest.TestCase):
    def test_ui_only_fields_are_cut_and_a_check_with_nothing_left_is_dropped(self):
        part = runner_reopen.project_part
        self.assertIsNone(part({"id": "panel", "showing": True, "hasFocus": False}))
        self.assertEqual(part({"id": "knob", "value": 5, "showing": True}), {"id": "knob", "value": 5})
        self.assertEqual(part({"id": "k", "contains": {"title": "LFO", "tooltip": "x"}}), {"id": "k", "contains": {"title": "LFO"}})
        self.assertIsNone(part({"id": "k", "contains": {"tooltip": "x"}}))
        self.assertEqual(part({"id": "card:x", "exists": False}), {"id": "card:x", "exists": False})
        self.assertEqual(part({"count": {"prefix": "card:", "equals": 2}}), {"count": {"prefix": "card:", "equals": 2}})

    def test_view_components_and_view_counts_are_not_compared(self):
        part = runner_reopen.project_part
        self.assertIsNone(part({"id": "drumMachineState", "hitCount": 9}))
        self.assertIsNone(part({"id": "ADSR mod lane on Resonance > modulatorRowState", "swapsStarted": 1}))
        self.assertIsNone(part({"id": "Filter shapes: Filter > Show Response", "toggleState": False}))
        self.assertIsNone(part({"id": "card:Drum Kit > drumPads", "pickedPad": 2}))
        self.assertIsNone(part({"count": {"prefix": "trackNameLabel", "equals": 3}}))
        self.assertEqual(part({"id": "card:Delay", "bypass": True}), {"id": "card:Delay", "bypass": True})

    def test_a_scenario_whose_last_checks_are_all_ui_only_starts_no_second_app(self):
        text = SAVE % ("", "  - expect: {id: knob, hasFocus: true, showing: true}\n")
        (ok, message), launches, _ = run_with(text, DrawingApp({"knob": {"hasFocus": True, "showing": True}}))
        self.assertTrue(ok, message)
        self.assertEqual(len(launches), 1)


class ReopenTests(unittest.TestCase):
    def test_a_project_that_reopens_as_checked_passes_in_a_fresh_app_with_a_new_settings_folder(self):
        first, second = DrawingApp(knob(5)), DrawingApp(knob(5))
        (ok, message), launches, out = run_with(SAVE % ("", KEEPS), first, second)
        self.assertTrue(ok, message)
        self.assertEqual(len(launches), 2)
        self.assertNotEqual(launches[0][0], launches[1][0])
        self.assertIn(("openProject", {"path": os.path.join(out, "end.agsproj")}), second.calls)
        self.assertTrue(first.stopped)  # the first instance is gone before the second reads the bundle

    def test_a_difference_fails_with_both_states_printed(self):
        (ok, message), _, _ = run_with(SAVE % ("", KEEPS), DrawingApp(knob(5)), DrawingApp(knob(0)))
        self.assertFalse(ok)
        self.assertIn("does not reopen", message)
        self.assertIn('before saving: {"id": "knob", "value": 5}', message)
        self.assertIn('after reopening end.agsproj: {"id": "knob", "value": 0}', message)

    def test_a_panel_the_fresh_app_has_not_open_is_skipped_but_a_missing_card_fails(self):
        text = SAVE % ("", "  - expect: {id: panel, value: 5}\n")
        (ok, message), _, _ = run_with(text, DrawingApp({"panel": {"value": 5}}), DrawingApp({}))
        self.assertTrue(ok, message)
        text = SAVE % ("", "  - expect: {id: 'card:Delay', value: 5}\n")
        (ok, message), _, _ = run_with(text, DrawingApp({"card:Delay": {"value": 5}}), DrawingApp({}))
        self.assertFalse(ok)
        self.assertIn("does not reopen", message)

    def test_a_count_check_prints_its_count_on_both_sides(self):
        text = SAVE % ("", "  - expect: {count: {prefix: 'card:', equals: 0}}\n")
        (ok, message), _, _ = run_with(text, DrawingApp(knob(5)), DrawingApp(knob(5), tree={"id": "card:x", "children": []}))
        self.assertFalse(ok)
        self.assertIn("before saving: {\"count of id starting with 'card:'\": 0}", message)
        self.assertIn("after reopening end.agsproj: {\"count of id starting with 'card:'\": 1}", message)

    def test_no_trailing_expect_a_failed_run_and_the_opt_out_start_no_second_app(self):
        for text, apps in ((SAVE % ("", ""), [DrawingApp(knob(5))]),
                           (SAVE % ("skip_reopen_check: the project drops the knob on purpose\n", KEEPS), [DrawingApp(knob(5))]),
                           (SAVE % ("", "  - expect: {id: knob, value: 9}\n"), [DrawingApp(knob(5))])):
            (_, _), launches, _ = run_with(text, *apps)
            self.assertEqual(len(launches), 1, text)

    def test_the_opt_out_needs_a_reason(self):
        with self.assertRaises(run.ScenarioError):
            run.parse_scenario("skip_reopen_check: true\nsteps:\n  - wait: {}\n", "x")

    def test_a_listed_scenario_notes_the_difference_instead_of_failing(self):
        (ok, _), _, _ = run_with(SAVE % ("", KEEPS), DrawingApp(knob(5)), DrawingApp(knob(0)),
                                 known_realism={("demo", "reopen"): ("FRO1", "the panel is not saved")})
        self.assertTrue(ok)

    def test_the_second_app_gets_the_same_launch_arguments(self):
        (_, _), launches, _ = run_with("motion: true\n" + SAVE % ("", KEEPS), DrawingApp(knob(5)), DrawingApp(knob(5)))
        self.assertEqual(launches[0][1], launches[1][1])
        self.assertEqual(launches[1][1], {"extra_args": ["--automation-motion"]})

    def test_a_scenario_that_slows_motion_still_ends_cleanly(self):
        text = SAVE % ("", "  - slow_motion: {factor: 2}\n" + KEEPS)
        (ok, message), _, _ = run_with(text, DrawingApp(knob(5)), DrawingApp(knob(5)))
        self.assertTrue(ok, message)


class UserProjectTests(unittest.TestCase):
    def test_opening_a_project_outside_fixtures_is_refused_with_the_way_to_copy_it(self):
        for path in ("~/Music/AgentSynth/Load test.agsproj", "/Users/x/p.agsproj", "relative/p.agsproj"):
            with self.assertRaises(run.ScenarioError, msg=path) as caught:
                run.parse_scenario("steps:\n  - openProject: {path: \"%s\"}\n" % path, "demo")
            self.assertIn("copyFixture", str(caught.exception))
            self.assertIn("from: %s" % path, str(caught.exception))

    def test_fixtures_a_scratch_copy_and_an_earlier_scenarios_bundle_open(self):
        for path in ("${FIXTURES}/a.agsproj", "${SCRATCH}/a.agsproj", "${OUT}/a.agsproj"):
            run.parse_scenario("steps:\n  - openProject: {path: \"%s\"}\n" % path, "demo")

    def test_a_copy_of_a_user_project_is_made_by_copy_fixture_from(self):
        with tempfile.TemporaryDirectory() as source:
            bundle = os.path.join(source, "mine.agsproj")
            os.makedirs(bundle)
            open(os.path.join(bundle, "project.json"), "w").close()
            open(os.path.join(bundle, "autosave-1.json"), "w").close()
            text = ("steps:\n  - copyFixture: {name: mine.agsproj, from: %s}\n  - openProject: {path: \"${SCRATCH}/mine.agsproj\"}\n"
                    % bundle)
            app = DrawingApp({})
            (ok, message), _, _ = run_with(text, app)
            self.assertTrue(ok, message)
            opened = [c[1]["path"] for c in app.calls if c[0] == "openProject"][0]
            self.assertTrue(opened.endswith("/mine.agsproj"))
            self.assertNotEqual(os.path.dirname(opened), source)
            self.assertTrue(os.path.exists(os.path.join(bundle, "autosave-1.json")))  # the original is untouched

    def test_every_shipped_scenario_that_saves_parses_and_opens_only_its_own_projects(self):
        run.load_scenarios(os.path.join(os.path.dirname(HERE), "scenarios"))


if __name__ == "__main__":
    unittest.main()
