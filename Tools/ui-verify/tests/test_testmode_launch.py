"""Unit tests for TestModeApp.start against fake executables (no Agent Synth): a build without test mode is refused
before it runs, every launch carries the keychain and audio-device opt-outs, and the startup timeout holds when the
app prints nothing."""

import os
import stat
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from agentsynth_testmode import TestModeApp, TestModeError  # noqa: E402


def fake_app(folder, body):
    path = os.path.join(folder, "Agent Synth")
    with open(path, "w") as f:
        f.write("#!/bin/sh\n" + body + "\n")
    os.chmod(path, os.stat(path).st_mode | stat.S_IEXEC)
    return path


class LaunchTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def test_a_build_without_test_mode_is_refused_before_it_runs(self):
        ran = os.path.join(self.tmp.name, "ran")
        app = TestModeApp(app_path=fake_app(self.tmp.name, "touch '%s'" % ran), settings_dir=self.tmp.name)
        with self.assertRaisesRegex(TestModeError, "without test mode"):
            app.start()
        self.assertFalse(os.path.exists(ran), "an old build would read the keychain, so it must never start")

    def test_every_launch_opts_out_of_the_keychain_and_the_audio_device(self):
        seen = os.path.join(self.tmp.name, "seen")
        body = (
            "# AGENTSYNTH_AUTOMATION_PORT\n"
            'echo "$@ $AGENTSYNTH_NO_KEYCHAIN $AGENTSYNTH_NO_AUDIO_DEVICE" > \'%s\'\n'
            "echo AGENTSYNTH_AUTOMATION_PORT 1 $$\nsleep 5" % seen
        )
        app = TestModeApp(app_path=fake_app(self.tmp.name, body), settings_dir=self.tmp.name, startup_timeout=5)
        with self.assertRaises(OSError):  # nothing listens on port 1; the launch itself is what is checked
            app.start()
        app.stop()
        with open(seen) as f:
            words = f.read().split()
        for word in ("--automation", "--no-keychain", "--no-audio-device"):
            self.assertIn(word, words)
        self.assertEqual(words[-2:], ["1", "1"], "AGENTSYNTH_NO_KEYCHAIN and AGENTSYNTH_NO_AUDIO_DEVICE set")

    def test_the_startup_timeout_holds_when_the_app_prints_nothing(self):
        body = "# AGENTSYNTH_AUTOMATION_PORT\nsleep 30"
        app = TestModeApp(app_path=fake_app(self.tmp.name, body), settings_dir=self.tmp.name, startup_timeout=1)
        started = time.time()
        with self.assertRaisesRegex(TestModeError, "did not announce"):
            app.start()
        self.assertLess(time.time() - started, 20, "the old readline loop waited for the app forever")


if __name__ == "__main__":
    unittest.main()
