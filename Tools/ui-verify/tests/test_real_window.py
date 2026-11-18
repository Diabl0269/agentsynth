"""Unit tests for real_window.py and the runner's real-window mode (FRO2040). Fake apps, no OS keys, no Agent Synth."""

import io
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
import real_window  # noqa: E402
import run  # noqa: E402
from test_run import FakeApp  # noqa: E402

REAL = "real_window: true\nclick_through_windows: fake app\nsteps:\n  - key: {keys: \"cmd+z shift+tab\"}\n  - type: {text: a b}\n  - click: {id: x}\n"


class IdleTests(unittest.TestCase):
    def test_hid_idle_time_is_read_from_ioreg_in_seconds(self):
        out = subprocess.CompletedProcess([], 0, stdout='  | |   "HIDIdleTime" = 301500000000\n', stderr="")
        self.assertEqual(real_window.user_idle_seconds(lambda *a, **k: out), 301.5)

    def test_an_unreadable_idle_time_is_none(self):
        self.assertIsNone(real_window.user_idle_seconds(lambda *a, **k: subprocess.CompletedProcess([], 0, stdout="", stderr="")))
        def broken(*a, **k):
            raise OSError("no ioreg")
        self.assertIsNone(real_window.user_idle_seconds(broken))


class AwayGuardTests(unittest.TestCase):
    def guard(self, idle, enabled=True, env=None, minimum=300):
        reads = []
        guard = real_window.AwayGuard(enabled, minimum, env=env or {}, idle_reader=lambda: reads.append(1) or idle)
        guard.reads = reads
        return guard

    def test_without_the_flag_the_scenario_is_skipped_whoever_is_away(self):
        guard = self.guard(9999, enabled=False)
        self.assertIn("--real-window", guard.skip_reason())
        self.assertEqual(guard.reads, [])

    def test_an_active_user_skips_with_the_reason(self):
        reason = self.guard(12).skip_reason()
        self.assertIn("12 s ago", reason)
        self.assertIn("300 s", reason)

    def test_an_idle_user_lets_it_run_and_the_run_stays_confirmed(self):
        guard = self.guard(400)
        self.assertEqual(guard.skip_reason(), "")
        guard.idle_reader = lambda: 0  # the run's own keys reset the OS idle time
        self.assertEqual(guard.skip_reason(), "")

    def test_the_unattended_launcher_variable_lets_it_run_without_reading_idle(self):
        guard = self.guard(0, env={"AGENTSYNTH_USER_AWAY": "1"})
        self.assertEqual(guard.skip_reason(), "")
        self.assertEqual(guard.reads, [])

    def test_an_unknown_idle_time_skips(self):
        self.assertIn("ioreg", self.guard(None).skip_reason())


class KeyScriptTests(unittest.TestCase):
    def test_a_chord_becomes_a_system_events_script_for_that_pid(self):
        script = real_window.key_script(4242, "cmd+z")
        self.assertIn("unix id is 4242", script)
        self.assertIn('keystroke "z" using {command down}', script)

    def test_named_keys_use_key_codes_and_modifiers_stack(self):
        self.assertIn("key code 53", real_window.key_script(1, "escape"))
        self.assertIn("key code 48 using {shift down}", real_window.key_script(1, "shift+tab"))
        self.assertIn("using {command down, option down}", real_window.key_script(1, "cmd+alt+x"))

    def test_a_comma_a_plus_and_a_quote_are_typed_as_themselves(self):
        self.assertIn('keystroke "," using {command down}', real_window.key_script(1, "cmd+,"))
        self.assertIn('keystroke "+"', real_window.key_script(1, "+"))
        self.assertIn('keystroke "\\""', real_window.key_script(1, '"'))

    def test_an_unknown_modifier_is_an_error(self):
        with self.assertRaises(ValueError):
            real_window.key_script(1, "hyper+z")

    def test_a_refused_key_says_how_to_fix_it(self):
        failed = subprocess.CompletedProcess([], 1, stdout="", stderr="not allowed assistive access")
        with self.assertRaises(RuntimeError) as caught:
            real_window.send_chord(1, "a", run=lambda *a, **k: failed, sleep=lambda s: None)
        self.assertIn("Accessibility", str(caught.exception))


class RunnerTests(unittest.TestCase):
    def run_real(self, text=REAL):
        app = FakeApp({"x": {"bounds": {"x": 0, "y": 0, "w": 5, "h": 5}}})
        app.pid = 4242
        sent, launches = [], []

        def factory(path, settings_dir=None, **extra):
            launches.append(extra)
            return app

        with tempfile.TemporaryDirectory() as out:
            runner = run.Runner(None, out, run.parse_scenario(text, "demo"), app_factory=factory, known_pops={}, known_realism={},
                                os_key_sender=lambda pid, chord: sent.append((pid, chord)))
            return runner.run(), app, sent, launches

    def test_a_real_window_scenario_starts_visible_with_motion(self):
        _, _, _, launches = self.run_real()
        self.assertEqual(launches[0], {"extra_args": ["--automation-motion"], "visible": True})

    def test_keys_go_to_the_apps_pid_through_the_os_and_not_through_test_mode(self):
        (ok, message), app, sent, _ = self.run_real()
        self.assertTrue(ok, message)
        self.assertEqual(sent, [(4242, "cmd+z"), (4242, "shift+tab"), (4242, "a"), (4242, "space"), (4242, "b")])
        self.assertEqual([c[0] for c in app.calls if c[0] in ("key", "type")], [])
        self.assertIn("click", [c[0] for c in app.calls])  # the mouse still goes through test mode

    def test_an_ordinary_scenario_keeps_its_keys_in_test_mode(self):
        (ok, _), app, sent, launches = self.run_real("steps:\n  - key: {keys: escape}\n")
        self.assertEqual(sent, [])
        self.assertEqual(launches[0], {})
        self.assertIn("key", [c[0] for c in app.calls])

    def test_real_window_is_true_or_false(self):
        with self.assertRaises(run.ScenarioError):
            run.parse_scenario("real_window: yes please\nsteps:\n  - wait: {}\n", "x")


class MainTests(unittest.TestCase):
    def main(self, argv, env=None, idle=0):
        out = io.StringIO()
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "real.yaml")
            with open(path, "w") as handle:
                handle.write("real_window: true\nsteps:\n  - wait: {}\n")
            other = os.path.join(folder, "plain.yaml")
            with open(other, "w") as handle:
                handle.write("steps:\n  - wait: {}\n")
            os.environ.pop("AGENTSYNTH_USER_AWAY", None)
            fake = mock.Mock()
            fake.return_value.run.return_value = (True, "")
            fake.return_value.marks = []
            with mock.patch.object(run, "Runner", fake), mock.patch.object(real_window, "user_idle_seconds", lambda: idle), \
                    mock.patch.dict(os.environ, env or {}, clear=False), redirect_stdout(out):
                code = run.main([folder] + argv)
        return code, out.getvalue(), fake

    def test_the_default_run_skips_real_window_scenarios_and_still_passes(self):
        code, text, fake = self.main([])
        self.assertEqual(code, 0)
        self.assertIn("SKIPPED real: a real-window scenario runs only with --real-window", text)
        self.assertIn("1 passed, 0 failed, 1 skipped", text)
        self.assertEqual(fake.call_count, 1)  # only the plain one ran

    def test_with_the_flag_but_a_user_at_the_machine_it_is_skipped_with_the_reason(self):
        code, text, fake = self.main(["--real-window"], idle=3)
        self.assertEqual(code, 0)
        self.assertIn("SKIPPED real: someone used the machine", text)
        self.assertEqual(fake.call_count, 1)

    def test_with_the_flag_and_an_idle_machine_both_run(self):
        code, text, fake = self.main(["--real-window"], idle=900)
        self.assertEqual(code, 0)
        self.assertNotIn("SKIPPED", text)
        self.assertEqual(fake.call_count, 2)

    def test_the_unattended_variable_runs_it_without_an_idle_reading(self):
        _, text, fake = self.main(["--real-window"], env={"AGENTSYNTH_USER_AWAY": "1"}, idle=0)
        self.assertNotIn("SKIPPED", text)
        self.assertEqual(fake.call_count, 2)

    def test_the_shipped_example_is_marked_and_the_others_are_not_skipped_by_default(self):
        scenarios = run.load_scenarios(os.path.join(os.path.dirname(HERE), "scenarios"))
        marked = [s.name for s in scenarios if s.realism.real_window]
        self.assertEqual(marked, ["real-window-settings-key"])
        self.assertLessEqual(len(marked), 3)


if __name__ == "__main__":
    unittest.main()
