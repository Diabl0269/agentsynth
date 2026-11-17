"""The accessibility dump saved beside a proof mark's still (FRO2031)."""
import json
import sys

from agentsynth_testmode import TestModeError


def ax_controls(tree):
    """The showing controls of a `list` tree as `{id, name, title, type}` entries, depth first, empty ones left out."""
    found = []

    def walk(node):
        if node.get("showing", True) is not False:
            entry = {k: node[k] for k in ("id", "name", "title", "type") if node.get(k)}
            if entry.keys() & {"id", "name", "title"}:
                found.append(entry)
            for child in node.get("children", []):
                walk(child)

    for root in tree if isinstance(tree, list) else [tree]:
        walk(root)
    return found


def write_ax_dump(app, path):
    """Saves the controls on screen beside a mark's still so the proof hook can check the control a check names.

    Best effort: a dump that cannot be taken is a warning, never a failed mark. Returns the path or None."""
    try:
        controls = ax_controls(app.call("list", depth=40))
        with open(path, "w", encoding="utf-8") as f:
            json.dump({"controls": controls}, f, indent=1)
        return path
    except (TestModeError, OSError, AttributeError, TypeError) as error:
        print("ui-verify: no accessibility dump %s: %s" % (path, error), file=sys.stderr)
        return None
