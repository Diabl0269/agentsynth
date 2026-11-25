"""Unit tests for runner_realism.py and its use in run.py (FRO2037): a hand's drag, the stall limit, feedVisual with a
mute check, audio re-attached at the end and a click under another window. Fake app, no Agent Synth."""

import os
import sys
import tempfile
import time
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
import recording  # noqa: E402
import run  # noqa: E402
import runner_realism  # noqa: E402
from test_run import DrawingApp, FakeApp  # noqa: E402

STATES = {"window": {"bounds": {"x": 0, "y": 0, "w": 800, "h": 600}},
          "a": {"bounds": {"x": 10, "y": 10, "w": 50, "h": 20}},
          "b": {"bounds": {"x": 40, "y": 10, "w": 20, "h": 20}},
          "edge": {"bounds": {"x": 700, "y": 300, "w": 90, "h": 40}}}


REAL_LOAD_FACTOR = runner_realism.load_factor
runner_realism.load_factor = lambda: 1.0  # a busy machine must not widen the limits these tests set


def run_text(text, app, name="demo", **options):
    options.setdefault("known_pops", {})
    options.setdefault("known_realism", {})
    with tempfile.TemporaryDirectory() as out:
        runner = run.Runner(None, out, run.parse_scenario(text, name), app_factory=lambda *a, **k: app, **options)
        with mock.patch.object(recording.Recorder, "write_video", lambda recorder, path, ffmpeg=None: (True, path)):
            return runner.run(), runner


class HandPathTests(unittest.TestCase):
    def test_a_path_repeats_for_one_seed_and_differs_for_another(self):
        first = runner_realism.hand_path((0, 0), (200, 0), 12, 7)
        self.assertEqual(first, runner_realism.hand_path((0, 0), (200, 0), 12, 7))
        self.assertNotEqual(first, runner_realism.hand_path((0, 0), (200, 0), 12, 8))

    def test_a_long_drag_overshoots_trembles_and_returns_exactly_to_the_target(self):
        path = runner_realism.hand_path((0, 100), (200, 100), 12, 3)
        self.assertEqual(len(path), 14)  # twelve out, one back to half way, one on the target
        self.assertEqual(path[-1], (200, 100))
        self.assertGreater(max(x for x, _ in path), 200)  # past the target
        self.assertTrue(any(y != 100 for _, y in path))  # off the straight line
        self.assertTrue(all(abs(y - 100) <= runner_realism.TREMBLE_PX + 1 for _, y in path))

    def test_a_short_drag_does_not_overshoot_and_ends_on_the_target(self):
        path = runner_realism.hand_path((0, 0), (10, 0), 5, 1)
        self.assertEqual(len(path), 5)
        self.assertEqual(path[-1], (10, 0))
        self.assertLessEqual(max(x for x, _ in path), 10)

    def test_the_seed_names_the_scenario_and_the_step(self):
        self.assertEqual(runner_realism.drag_seed("a", 1), runner_realism.drag_seed("a", 1))
        self.assertNotEqual(runner_realism.drag_seed("a", 1), runner_realism.drag_seed("a", 2))
        self.assertNotEqual(runner_realism.drag_seed("a", 1), runner_realism.drag_seed("b", 1))


class RecordedHandDragTests(unittest.TestCase):
    def drag(self, drag, extra=""):
        app = DrawingApp(STATES)
        (ok, message), _ = run_text("record: true\n%ssteps:\n  - %s\n" % (extra, drag), app)
        self.assertTrue(ok, message)
        return [c for c in app.calls if c[0].startswith("mouse")]

    def test_a_recorded_drag_sends_more_moves_than_steps_and_ends_on_the_target(self):
        mouse = self.drag("drag: {from: a, to: a, fromX: 5, fromY: 5, toX: 45, toY: 5, steps: 6}")
        moves = [c for c in mouse if c[0] == "mouseMoveHeld"]
        self.assertGreater(len(moves), 6)
        self.assertEqual((mouse[-1][1]["x"], mouse[-1][1]["y"]), (45, 5))
        self.assertEqual(mouse[-1][0], "mouseUp")

    def test_a_run_repeats_its_drag_move_for_move(self):
        text = "drag: {from: a, to: a, fromX: 5, fromY: 5, toX: 45, toY: 5, steps: 6}"
        self.assertEqual(self.drag(text), self.drag(text))

    def test_a_drag_to_the_window_edge_leaves_the_window_as_a_captured_move(self):
        mouse = self.drag("drag: {from: window, to: window, fromX: 400, fromY: 300, toX: 799, toY: 300, steps: 8}")
        outside = [c for c in mouse if c[0] == "mouseMoveHeld" and c[1]["x"] >= 800]
        self.assertTrue(outside)
        self.assertTrue(all(c[1].get("captured") for c in outside))
        inside = [c for c in mouse if c[0] == "mouseMoveHeld" and c[1]["x"] < 800]
        self.assertTrue(all("captured" not in c[1] for c in inside))

    def test_explicit_mouse_steps_are_sent_unchanged_and_a_drag_ends_exactly_on_target(self):
        mouse = self.drag("mouseDown: {id: a, x: 5, y: 5}\n  - mouseMoveHeld: {id: a, x: 30, y: 5}\n"
                          "  - mouseMoveHeld: {id: a, x: 45, y: 5}\n  - mouseUp: {id: a, x: 45, y: 5}")
        self.assertEqual([(c[0], c[1]["x"], c[1]["y"]) for c in mouse],
                         [("mouseDown", 5, 5), ("mouseMoveHeld", 30, 5), ("mouseMoveHeld", 45, 5), ("mouseUp", 45, 5)])
        for seed in range(40):  # whatever the tremble and overshoot, the last move and the release are the target
            path = runner_realism.hand_path((0, 0), (137, 12), 12, seed)
            self.assertEqual(path[-1], (137, 12))

    def test_straight_drags_needs_a_reason_and_then_sends_the_plain_line(self):
        for bad in ("true", "''", "5"):
            with self.assertRaises(run.ScenarioError, msg=bad):
                run.parse_scenario("straight_drags: %s\nsteps:\n  - wait: {}\n" % bad, "x")
        mouse = self.drag("drag: {from: a, to: a, fromX: 5, fromY: 5, toX: 45, toY: 5, steps: 4}",
                          "straight_drags: the check counts the moves\n")
        self.assertEqual(len([c for c in mouse if c[0] == "mouseMoveHeld"]), 4)


class SlowApp(FakeApp):
    """Answers a click only after `delay` seconds, as an app that is frozen."""

    def __init__(self, delay, **kwargs):
        super().__init__(**kwargs)
        self.delay = delay

    def call(self, cmd, timeout_ms=None, **args):
        if cmd == "click":
            time.sleep(self.delay)
        return super().call(cmd, timeout_ms=timeout_ms, **args)


class StallTests(unittest.TestCase):
    CLICK = "click_through_windows: fake app\n%ssteps:\n  - click: {id: a}\n  - key: {keys: x}\n"

    def test_a_step_that_answers_after_the_limit_fails_naming_it(self):
        text = self.CLICK % "max_step_ms: {ms: 20, reason: tight on purpose}\n"
        (ok, message), _ = run_text(text, SlowApp(0.08, states=STATES))
        self.assertFalse(ok)
        self.assertIn("step 1", message)
        self.assertIn("click froze the app", message)
        self.assertIn("limit is 20 ms", message)

    def test_a_quick_step_passes_and_the_default_limit_is_400_ms(self):
        (ok, message), _ = run_text(self.CLICK % "", SlowApp(0.01, states=STATES))
        self.assertTrue(ok, message)
        self.assertEqual(run.parse_scenario("- wait: {}\n", "x").realism.max_step_ms, 400)

    def test_a_busy_machine_widens_the_limit_by_its_load(self):
        timed = mock.Mock(slowest=(900, 900, "key"))
        with mock.patch.object(runner_realism, "load_factor", lambda: 1.0):
            self.assertIn("key froze the app", runner_realism.stall_message(timed, 400))
        with mock.patch.object(runner_realism, "load_factor", lambda: 3.0):
            self.assertEqual(runner_realism.stall_message(timed, 400), "")
            self.assertIn("limit is 600 ms", runner_realism.stall_message(mock.Mock(slowest=(2000, 2000, "key")), 200))

    def test_the_load_factor_is_one_on_an_idle_machine_and_the_load_per_core_when_busy(self):
        with mock.patch.object(runner_realism.os, "getloadavg", lambda: (0.5, 0, 0)):
            self.assertEqual(REAL_LOAD_FACTOR(), 1.0)
        with mock.patch.object(runner_realism.os, "getloadavg", lambda: (32.0, 0, 0)), \
                mock.patch.object(runner_realism.os, "cpu_count", lambda: 16):
            self.assertEqual(REAL_LOAD_FACTOR(), 2.0)

    def test_a_longer_limit_lets_a_slow_step_pass(self):
        text = self.CLICK % "max_step_ms: {ms: 500, reason: loads a big plugin}\n"
        (ok, message), _ = run_text(text, SlowApp(0.08, states=STATES))
        self.assertTrue(ok, message)

    def test_a_limit_needs_a_reason(self):
        for bad in ("500", "{ms: 500}", "{ms: 0, reason: x}", "{ms: 5, reason: ''}", "{ms: true, reason: x}"):
            with self.assertRaises(run.ScenarioError, msg=bad):
                run.parse_scenario("max_step_ms: %s\nsteps:\n  - wait: {}\n" % bad, "x")

    def test_a_drag_command_may_take_its_moves(self):
        class SlowDrag(FakeApp):
            def call(self, cmd, timeout_ms=None, **args):
                if cmd == "drag":
                    time.sleep(0.06)
                return super().call(cmd, timeout_ms=timeout_ms, **args)
        text = "max_step_ms: {ms: 20, reason: tight}\nsteps:\n  - drag: {from: a, to: b, steps: 12}\n"
        (ok, message), _ = run_text(text, SlowDrag(states=STATES))
        self.assertTrue(ok, message)  # 12 moves allow 480 ms more

    def test_a_listed_scenario_notes_the_stall_instead_of_failing(self):
        text = self.CLICK % "max_step_ms: {ms: 20, reason: tight}\n"
        (ok, _), _ = run_text(text, SlowApp(0.08, states=STATES), known_realism={("demo", "stall"): ("FRO1", "slow")})
        self.assertTrue(ok)


class RestoreRatioTests(unittest.TestCase):
    """An undo and its redo should answer alike; a redo far slower than its undo fails."""

    def test_a_redo_about_as_fast_as_its_undo_passes(self):
        self.assertEqual(runner_realism.restore_ratio_message(10, 12), "")
        self.assertEqual(runner_realism.restore_ratio_message(2, 9), "")  # tiny numbers are noise
        self.assertEqual(runner_realism.restore_ratio_message(0, 0), "")

    def test_a_redo_much_slower_than_its_undo_fails_naming_both(self):
        message = runner_realism.restore_ratio_message(20, 400)
        self.assertIn("the redo answered after 400 ms", message)
        self.assertIn("undo of the same edit after 20 ms", message)

    def test_a_busy_machine_widens_the_allowance(self):
        with mock.patch.object(runner_realism, "load_factor", lambda: 4.0):
            self.assertEqual(runner_realism.restore_ratio_message(20, 400), "")
            self.assertNotEqual(runner_realism.restore_ratio_message(20, 900), "")


class FeedVisualTests(unittest.TestCase):
    def test_a_scenario_that_feeds_a_visual_and_checks_mute_bypass_or_a_note_does_not_parse(self):
        for check in ("{id: Mute, toggleState: true}", "{id: \"card:Filter\", bypassed: true}",
                      "{id: statusBar, above: {voices: 0}}", "{id: \"card:Filter\", noteOn: true}"):
            with self.assertRaises(run.ScenarioError, msg=check) as caught:
                run.parse_scenario("steps:\n  - feedVisual: {id: \"card:Filter\", kind: sine}\n  - expect: %s\n" % check, "demo")
            self.assertIn("feedVisual", str(caught.exception))
            self.assertIn("step 2", str(caught.exception))

    def test_a_glide_that_feeds_counts_too(self):
        text = ("record: true\nsteps:\n  - glide: {name: g, do: {feedVisual: {id: c, kind: silence}}}\n"
                "  - expect: {id: Bypass, toggleState: false}\n")
        with self.assertRaises(run.ScenarioError):
            run.parse_scenario(text, "demo")

    def test_a_feed_with_display_checks_and_a_mute_check_without_a_feed_both_parse(self):
        run.parse_scenario("steps:\n  - feedVisual: {id: c, kind: sine}\n  - expect: {id: c, filterSpectrumAtFloor: false}\n", "x")
        run.parse_scenario("steps:\n  - expect: {id: Mute, toggleState: true}\n", "x")

    def test_the_shipped_scenarios_parse(self):
        run.load_scenarios(os.path.join(os.path.dirname(HERE), "scenarios"))


class AudioReattachTests(unittest.TestCase):
    def test_audio_is_attached_again_after_a_feed_visual(self):
        app = FakeApp(STATES)
        (ok, message), _ = run_text("steps:\n  - feedVisual: {id: c, kind: sine}\n  - wait: {}\n", app)
        self.assertTrue(ok, message)
        self.assertEqual(app.calls[-1], ("audioPump", {"run": True}))

    def test_a_feed_inside_a_glide_counts_and_no_feed_sends_nothing(self):
        app = FakeApp(STATES)
        run_text("record: true\nsteps:\n  - glide: {name: g, do: {feedVisual: {id: c, kind: sine}}}\n", app)
        self.assertEqual(app.calls[-1][0], "audioPump")
        quiet = FakeApp(STATES)
        run_text("steps:\n  - wait: {}\n", quiet)
        self.assertNotIn("audioPump", [c[0] for c in quiet.calls])

    def test_a_restart_gives_a_new_instance_with_audio_attached(self):
        app = FakeApp(STATES)
        run_text("steps:\n  - feedVisual: {id: c, kind: sine}\n  - restart: {}\n", app)
        self.assertNotIn("audioPump", [c[0] for c in app.calls])

    def test_audio_is_attached_before_the_project_is_saved(self):
        app = DrawingApp(STATES)
        run_text("save_project: end\nsteps:\n  - feedVisual: {id: c, kind: sine}\n", app)
        names = [c[0] for c in app.calls]
        self.assertLess(names.index("audioPump"), names.index("saveProject"))


class CoverApp(FakeApp):
    """The screen frame lists the main window and a pop-up on top of it."""

    def __init__(self, popup, **kwargs):
        super().__init__(**kwargs)
        self.popup = popup

    def call(self, cmd, timeout_ms=None, **args):
        if cmd == "screenshot" and args.get("id") == "screen":
            self.calls.append((cmd, args))
            main = {"id": "window", "x": 5, "y": 5, "width": 800, "height": 600}
            return {"path": args["path"], "windows": [main, dict(self.popup, x=self.popup["x"] + 5, y=self.popup["y"] + 5)]}
        return super().call(cmd, timeout_ms=timeout_ms, **args)


POPUP = {"id": "pickerPanel", "x": 30, "y": 0, "width": 100, "height": 100}


class CoveredClickTests(unittest.TestCase):
    def test_a_click_under_another_window_fails_naming_it(self):
        app = CoverApp(POPUP, states=STATES)
        (ok, message), _ = run_text("steps:\n  - click: {id: a, x: 30, y: 10}\n", app)
        self.assertFalse(ok)
        self.assertIn("pickerPanel", message)
        self.assertNotIn("click", [c[0] for c in app.calls])  # it was never sent

    def test_a_click_on_the_window_that_covers_is_its_own(self):
        app = CoverApp(POPUP, states=STATES)
        (ok, message), _ = run_text("steps:\n  - click: {id: b}\n", app)
        self.assertTrue(ok, message)

    def test_a_click_outside_every_other_window_passes(self):
        (ok, message), _ = run_text("steps:\n  - click: {id: a, x: 5, y: 5}\n", CoverApp(POPUP, states=STATES))
        self.assertTrue(ok, message)

    def test_a_tooltip_lets_the_click_through(self):
        (ok, message), _ = run_text("steps:\n  - click: {id: a, x: 30, y: 10}\n",
                                    CoverApp(dict(POPUP, id="tooltipWindow"), states=STATES))
        self.assertTrue(ok, message)

    def test_the_opt_out_needs_a_reason_and_skips_the_check(self):
        with self.assertRaises(run.ScenarioError):
            run.parse_scenario("click_through_windows: true\nsteps:\n  - wait: {}\n", "x")
        app = CoverApp(POPUP, states=STATES)
        (ok, message), _ = run_text("click_through_windows: the pop-up is meant to be clicked through\n"
                                    "steps:\n  - click: {id: a, x: 30, y: 10}\n", app)
        self.assertTrue(ok, message)

    def test_covering_window_geometry(self):
        windows = [{"id": "window", "x": 0, "y": 0, "width": 100, "height": 100},
                   {"id": "menu", "x": 50, "y": 50, "width": 40, "height": 40}]
        target = {"x": 10, "y": 10, "w": 20, "h": 20}
        self.assertIsNone(runner_realism.covering_window(windows, (20, 20), target))
        self.assertEqual(runner_realism.covering_window(windows, (60, 60), dict(target, x=45, y=45, w=30, h=30)), "menu")
        self.assertIsNone(runner_realism.covering_window(windows, (60, 60), {"x": 52, "y": 52, "w": 10, "h": 10}))
        self.assertIsNone(runner_realism.covering_window([], (1, 1), target))

    def test_the_main_window_never_covers_a_card_scrolled_partly_off_it(self):
        windows = [{"id": "macros - Agent Synth", "x": 0, "y": 0, "width": 100, "height": 100}]
        self.assertIsNone(runner_realism.covering_window(windows, (20, 20), {"x": 10, "y": 60, "w": 30, "h": 80}))


class KnownRealismFileTests(unittest.TestCase):
    def test_the_file_parses_comments_and_refuses_a_malformed_line(self):
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "known.txt")
            with open(path, "w") as handle:
                handle.write("# c\n\nscn stall FRO1 a slow load\n")
            self.assertEqual(runner_realism.load_known_realism(path), {("scn", "stall"): ("FRO1", "a slow load")})
            for bad in ("scn stall FRO1\n", "scn other FRO1 reason\n"):
                with open(path, "w") as handle:
                    handle.write(bad)
                with self.assertRaises(runner_realism.RealismError):
                    runner_realism.load_known_realism(path)

    def test_every_scenario_in_the_shipped_file_exists(self):
        folder = os.path.join(os.path.dirname(HERE), "scenarios")
        names = {os.path.splitext(f)[0] for f in os.listdir(folder)}
        self.assertEqual({n for n, _ in runner_realism.load_known_realism()} - names, set())


if __name__ == "__main__":
    unittest.main()
