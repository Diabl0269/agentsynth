"""Guard for Edit Layout scenarios: a drag on an outline-only cell does nothing (lane cells, the mode row, the footer and
tab cells never move), so a scenario that presses on a control in Edit Layout must first `expect` that the control's
outline says `Drag to move`, and press it by its `layoutOutline:<param id>` id, never by a window pixel that goes stale
when a card's layout changes."""

import glob
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import run  # noqa: E402

SCENARIOS = os.path.join(os.path.dirname(HERE), "scenarios")
PRESSES = ("mouseDown", "drag")


def steps_of(path):
    with open(path, encoding="utf-8") as f:
        scenario = run.parse_scenario(f.read(), os.path.basename(path))
    return scenario.steps


def command_and_args(step):
    return step[0], step[1]


class EditLayoutScenarioTests(unittest.TestCase):
    def edit_layout_files(self):
        names = glob.glob(os.path.join(SCENARIOS, "edit-layout-*.yaml")) + glob.glob(os.path.join(SCENARIOS, "on-card-*.yaml"))
        self.assertGreater(len(names), 0)
        return sorted(names)

    def test_a_press_in_edit_layout_follows_an_expect_that_the_outline_drags(self):
        for path in self.edit_layout_files():
            steps = steps_of(path)
            if not any(command_and_args(s)[0] in PRESSES for s in steps):
                continue
            with self.subTest(os.path.basename(path)):
                seen_expect = False
                for step in steps:
                    command, args = command_and_args(step)
                    if command == "expect" and "Drag to move" in str(args):
                        seen_expect = True
                    if command in PRESSES:
                        self.assertTrue(seen_expect, "press an outline only after expecting its tooltip starts 'Drag to move'")
                        self.assertIn("layoutOutline:", str(args), "press the outline by its id, not a window pixel")
                        break


if __name__ == "__main__":
    unittest.main()
