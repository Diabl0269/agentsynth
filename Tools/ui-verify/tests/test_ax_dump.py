"""Unit tests for ax_dump.py: the accessibility dump saved beside a proof mark's still."""

import json
import os
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))


class AxDumpTests(unittest.TestCase):
    def test_a_reply_that_cannot_be_read_is_a_warning_not_a_failed_mark(self):
        import ax_dump

        class BrokenListApp:
            def call(self, cmd, **args):
                return json.loads('{"cut off')  # what a huge project's `list` reply looked like

        with tempfile.TemporaryDirectory() as folder:
            with mock.patch("sys.stderr"):
                self.assertIsNone(ax_dump.write_ax_dump(BrokenListApp(), os.path.join(folder, "x.ax.json")))

    def test_a_huge_canvas_is_listed_only_as_deep_as_stays_small(self):
        import ax_dump

        asked = []

        class BigCanvasApp:
            def call(self, cmd, depth=0, **args):
                asked.append(depth)
                if depth > 4:
                    raise AssertionError("asked for depth %d: the reply would drop the connection" % depth)
                return {"id": "window", "children": [{"id": "c%d" % i, "type": "x" * (depth * 1000)} for i in range(depth * 3)]}

        tree = ax_dump.list_within_limit(BigCanvasApp())
        self.assertEqual(max(asked), 4)
        self.assertTrue(tree["children"])

    def test_a_small_canvas_is_listed_to_its_full_depth(self):
        import ax_dump

        asked = []

        class SmallApp:
            def call(self, cmd, depth=0, **args):
                asked.append(depth)
                return {"id": "window", "children": [{"id": "leaf%d" % i} for i in range(min(depth, 3))]}

        ax_dump.list_within_limit(SmallApp())
        self.assertEqual(asked, [2, 3, 4])  # depth 4 answers as depth 3 does: nothing deeper exists


if __name__ == "__main__":
    unittest.main()
