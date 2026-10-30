"""Unit tests for TestModeApp.start against fake executables (no Agent Synth): a build without test mode is refused
before it runs, every launch carries the keychain and audio-device opt-outs, and the startup timeout holds when the
app prints nothing."""

import os
import stat
import sys
import tempfile
import time
import types
import unittest
import unittest.mock

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

    def launched_words(self, **options):
        seen = os.path.join(self.tmp.name, "seen")
        body = "# AGENTSYNTH_AUTOMATION_PORT\necho \"$@\" > '%s'\necho AGENTSYNTH_AUTOMATION_PORT 1 $$\nsleep 5" % seen
        app = TestModeApp(app_path=fake_app(self.tmp.name, body), settings_dir=self.tmp.name, startup_timeout=5, **options)
        with self.assertRaises(OSError):
            app.start()
        app.stop()
        with open(seen) as f:
            return f.read().split()

    def test_the_app_is_hidden_unless_visible_is_asked(self):
        self.assertNotIn("--automation-visible", self.launched_words())
        self.assertIn("--automation-visible", self.launched_words(visible=True))

    def test_a_hidden_app_is_never_activated(self):
        app = TestModeApp(app_path="unused", settings_dir=self.tmp.name)
        app.pid = 1
        fake_appkit = types.ModuleType("AppKit")
        fake_appkit.NSRunningApplication = unittest.mock.MagicMock()
        with unittest.mock.patch.dict(sys.modules, {"AppKit": fake_appkit}):
            app.activate()
            fake_appkit.NSRunningApplication.runningApplicationWithProcessIdentifier_.assert_not_called()
            app.visible = True
            app.activate()
            fake_appkit.NSRunningApplication.runningApplicationWithProcessIdentifier_.assert_called_once_with(1)

    def test_the_startup_timeout_holds_when_the_app_prints_nothing(self):
        body = "# AGENTSYNTH_AUTOMATION_PORT\nsleep 30"
        app = TestModeApp(app_path=fake_app(self.tmp.name, body), settings_dir=self.tmp.name, startup_timeout=1)
        started = time.time()
        with self.assertRaisesRegex(TestModeError, "did not announce"):
            app.start()
        self.assertLess(time.time() - started, 20, "the old readline loop waited for the app forever")


if __name__ == "__main__":
    unittest.main()
