"""Unit tests for runner_glide.py: a glide filmed under `slow_motion` must land inside its frames. Fake app, no Agent
Synth."""

import os
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
import recording  # noqa: E402
import run  # noqa: E402
import runner_glide  # noqa: E402
from test_run import ShotLoggingApp  # noqa: E402


class GlideLandedTests(unittest.TestCase):
    def run_scenario(self, text, out):
        app = ShotLoggingApp({})
        return run.Runner(None, out, run.parse_scenario(text, "demo"), app_factory=lambda *a, **k: app).run()

    def glide_with(self, distinct, steps):
        with tempfile.TemporaryDirectory() as out, mock.patch.object(recording.Recorder, "distinct_frames", return_value=distinct):
            return self.run_scenario("record: true\nsteps:\n" + steps, out)

    def slowed_glide_with(self, left, whole, slowed=True):
        """A glide after `slow_motion`, whose last frame still differs from the landed state on `left` of the pixels (the
        whole glide moving `whole` of them)."""
        steps = ("  - slow_motion: {factor: %d}\n" % (8 if slowed else 1)
                 + "  - glide: {name: g, frames: 6, do: {key: {keys: cmd+z}}}\n")
        fractions = iter([left, whole])
        with mock.patch.object(recording.Recorder, "changed_fraction", side_effect=lambda *a, **k: next(fractions)), \
                mock.patch.object(runner_glide.time, "sleep"):
            return self.glide_with(6, steps)

    def test_a_slowed_glide_that_ends_mid_motion_fails_naming_the_fix(self):
        ok, message = self.slowed_glide_with(left=0.34, whole=0.39)
        self.assertFalse(ok)
        self.assertIn("glide g ended mid-motion", message)
        self.assertIn("lower the slow_motion factor or raise `frames`", message)

    def test_a_slowed_glide_that_landed_or_only_has_a_caption_changing_beside_it_passes(self):
        self.assertTrue(self.slowed_glide_with(left=0.0, whole=0.39)[0])  # the last frame is the landed state
        self.assertTrue(self.slowed_glide_with(left=0.049, whole=0.5)[0])  # a text elsewhere in the crop: a small share
        self.assertTrue(self.slowed_glide_with(left=0.004, whole=0.01)[0])  # under the floor: noise, not motion

    def test_a_glide_at_real_time_is_not_asked_whether_it_landed(self):
        self.assertTrue(self.slowed_glide_with(left=0.9, whole=0.9, slowed=False)[0])

    def test_changed_fraction_counts_the_pixels_that_differ_clearly_inside_the_crop(self):
        if recording.find_ffmpeg() is None:
            self.skipTest("no ffmpeg")
        try:
            import numpy  # noqa: F401
        except ImportError:
            self.skipTest("no numpy")
        with tempfile.TemporaryDirectory() as d:
            paths = []
            for index, colours in enumerate(((0, 0), (0, 255))):  # left half, right half
                path = os.path.join(d, "f%d.ppm" % index)
                with open(path, "wb") as handle:
                    handle.write(b"P6\n4 2\n255\n")
                    for _ in range(2):
                        handle.write(bytes([colours[0]] * 6 + [colours[1]] * 6))
                paths.append(path)
            recorder = recording.Recorder(d)
            self.assertEqual(recorder.changed_fraction(paths[0], paths[1]), 0.5)
            self.assertEqual(recorder.changed_fraction(paths[0], paths[1], {"x": 0, "y": 0, "w": 2, "h": 2}), 0.0)
            self.assertEqual(recorder.changed_fraction(paths[0], paths[0]), 0.0)



if __name__ == "__main__":
    unittest.main()
