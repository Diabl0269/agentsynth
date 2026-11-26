"""generateProject and test_account scenario options (no Agent Synth needed)."""

import os
import stat
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import run  # noqa: E402
from agentsynth_testmode import TestModeApp  # noqa: E402


class GenerateProjectTest(unittest.TestCase):
    def test_expands_to_the_steps_a_person_takes(self):
        scenario = run.parse_scenario("steps:\n  - generateProject: {module: osc}\n  - expect: {id: x}\n")
        commands = [c for c, _ in scenario.steps]
        self.assertNotIn("generateProject", commands)
        self.assertIn(("type", {"text": "osc"}), scenario.steps)
        self.assertEqual(scenario.steps[-1], ("expect", {"id": "x"}))

    def test_needs_a_module(self):
        for bad in ("{}", "{module: ''}", "{module: osc, extra: 1}"):
            with self.assertRaises(run.ScenarioError):
                run.parse_scenario("steps:\n  - generateProject: %s\n" % bad)


class TestAccountOptionTest(unittest.TestCase):
    def test_option_is_read_and_off_by_default(self):
        on = run.parse_scenario("test_account: true\nsteps:\n  - wait: {}\n")
        off = run.parse_scenario("steps:\n  - wait: {}\n")
        self.assertTrue(on.test_account)
        self.assertFalse(off.test_account)

    def test_runner_launches_with_it_only_when_asked(self):
        on = run.parse_scenario("test_account: true\nsteps:\n  - wait: {}\n")
        off = run.parse_scenario("steps:\n  - wait: {}\n")
        self.assertTrue(run.Runner("app", "out", on)._launch_arguments().get("test_account"))
        self.assertNotIn("test_account", run.Runner("app", "out", off)._launch_arguments())

    def test_client_passes_flag_and_env_only_when_asked(self):
        with tempfile.TemporaryDirectory() as tmp:
            seen = os.path.join(tmp, "seen")
            exe = os.path.join(tmp, "fake")
            with open(exe, "w") as f:
                f.write('#!/bin/sh\n# AGENTSYNTH_AUTOMATION_PORT\necho "$@ $AGENTSYNTH_TEST_ACCOUNT" > \'%s\'\n'
                        "echo AGENTSYNTH_AUTOMATION_PORT 1 $$\nsleep 5\n" % seen)
            os.chmod(exe, os.stat(exe).st_mode | stat.S_IEXEC)
            for asked in (True, False):
                app = TestModeApp(app_path=exe, settings_dir=tmp, startup_timeout=3, test_account=asked)
                try:
                    app.start()
                except Exception:
                    pass  # the fake never listens; only the launch line matters
                app.stop()
                with open(seen) as f:
                    words = f.read().split()
                self.assertEqual("--test-account" in words, asked)
                self.assertEqual(words[-1] == "1", asked)


if __name__ == "__main__":
    unittest.main()
