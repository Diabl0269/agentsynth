"""Unit tests for the pure parts of idle_cpu.py: the settings file, the gap rule and the claim table."""

import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import idle_cpu  # noqa: E402


class IdleCpuTests(unittest.TestCase):
    def test_animations_are_pinned_never_left_to_follow(self):
        self.assertIn('name="animationMode" val="full"', idle_cpu.animations_settings_xml(True))
        self.assertIn('name="animationMode" val="off"', idle_cpu.animations_settings_xml(False))

    def test_a_gap_over_a_quarter_is_a_finding(self):
        rows = [{"project": "Load test", "animations": "on", "real": 27.0, "test": 16.0},
                {"project": "new project", "animations": "on", "real": 5.0, "test": 4.5}]
        table = idle_cpu.table_markdown(rows).splitlines()
        self.assertIn("FINDING", table[2])
        self.assertIn("(ok)", table[3])

    def test_a_silent_real_launch_has_no_gap(self):
        self.assertEqual(idle_cpu.gap_fraction(0.0, 3.0), 0.0)

    def test_cpu_seconds_reads_days_and_minutes(self):
        class Out:
            stdout = "1-02:03:04.50"

        original = idle_cpu.subprocess.run
        idle_cpu.subprocess.run = lambda *a, **k: Out()
        try:
            self.assertAlmostEqual(idle_cpu.cpu_seconds(1), 86400 + 2 * 3600 + 3 * 60 + 4.5)
        finally:
            idle_cpu.subprocess.run = original


if __name__ == "__main__":
    unittest.main()
