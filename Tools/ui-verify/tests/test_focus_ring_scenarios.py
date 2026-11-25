"""Guard for the stale-pixels kind on focus rings: a scenario that moves keyboard focus between rings (the
`focus-rings-*` and `dock-tab-ring-*` scenarios) runs with `stale_pixel_check` and `motion`, so a ring that a focus or
tab change leaves half-painted on the old control, or never draws on the new one, fails the step that moved it. A
scenario that only filmed the rest states passed with the ring's top edge left behind on the old tab."""

import glob
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import run  # noqa: E402

SCENARIOS = os.path.join(os.path.dirname(HERE), "scenarios")
PATTERNS = ("focus-rings-*.yaml", "dock-tab-ring-*.yaml")


class FocusRingScenarioTests(unittest.TestCase):
    def ring_files(self):
        names = sorted(n for p in PATTERNS for n in glob.glob(os.path.join(SCENARIOS, p)))
        self.assertGreater(len(names), 0)
        return names

    def test_a_scenario_that_moves_focus_between_rings_checks_for_stale_pixels(self):
        for path in self.ring_files():
            with open(path, encoding="utf-8") as f:
                scenario = run.parse_scenario(f.read(), os.path.basename(path))
            if not any(step[0] == "key" for step in scenario.steps):
                continue
            with self.subTest(os.path.basename(path)):
                self.assertIsNotNone(scenario.stale_pixel_check, "add `stale_pixel_check: true`")
                self.assertTrue(scenario.motion, "add `motion: true`: most stale pixels come from an animation's end")


if __name__ == "__main__":
    unittest.main()
