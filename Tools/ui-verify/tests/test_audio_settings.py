"""Unit tests for the `settings: {audio: ...}` scenario setting (audio_settings.py) and `above: {remembered: name}`."""

import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import run  # noqa: E402
from test_run import FakeApp  # noqa: E402


class AudioSettingTests(unittest.TestCase):
    def test_the_audio_setting_becomes_a_silent_device_state(self):
        s = run.parse_scenario("settings: {audio: {rate: 48000, block: 256, latency: 480}}\nsteps:\n  - wait: {}\n", "x")
        self.assertEqual(s.settings, {"audioDeviceState": '<DEVICESETUP deviceType="Silent" audioOutputDeviceName='
                                      '"Silent test output" audioDeviceRate="48000" audioDeviceBufferSize="256" '
                                      'silentOutputLatency="480"/>'})
        defaults = run.parse_scenario("settings: {audio: {}, k: '1'}\nsteps:\n  - wait: {}\n", "x")
        self.assertIn('audioDeviceRate="44100" audioDeviceBufferSize="512" silentOutputLatency="0"',
                      defaults.settings["audioDeviceState"])
        self.assertEqual(defaults.settings["k"], "1")
        self.assertNotIn("audio", defaults.settings)

    def test_the_state_is_written_as_an_escaped_attribute(self):
        s = run.parse_scenario("settings: {audio: {rate: 48000}}\nsteps:\n  - wait: {}\n", "x")
        self.assertIn('deviceType=&quot;Silent&quot;', run.settings_xml(s.settings))

    def test_a_bad_audio_setting_is_refused_at_parse_time(self):
        for bad in ("{rate: 48000, sampleRate: 1}", "{rate: 0}", "{block: big}", "{latency: -1}", "[1]"):
            with self.assertRaises(run.ScenarioError, msg=bad):
                run.parse_scenario("settings: {audio: %s}\nsteps:\n  - wait: {}\n" % bad, "x")


class RememberedAboveTests(unittest.TestCase):
    def test_above_can_be_a_remembered_number(self):
        app = FakeApp(states={"p": {"frameCount": 12}})
        above = {"id": "p", "above": {"frameCount": {"remembered": "f"}}}
        self.assertIsNone(run.check_expect(above, app, {"f": 11}))
        self.assertIsNotNone(run.check_expect(above, app, {"f": 12}))
        self.assertIsNotNone(run.check_expect(above, app, {}))


if __name__ == "__main__":
    unittest.main()
