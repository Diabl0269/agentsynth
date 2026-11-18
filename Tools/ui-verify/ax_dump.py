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


MAX_DEPTH = 40
BIG_REPLY_BYTES = 40000  # a `list` reply near 147 KB makes the app drop the connection, which ends the run (seen on a 250-module project)


def list_within_limit(app):
    """The deepest `list` tree whose reply stays small: one level deeper at a time, stopping when a level adds nothing
    or the reply is already big (the next level of a big canvas is several times bigger)."""
    reply, size = None, -1
    for depth in range(2, MAX_DEPTH + 1):
        reply, previous = app.call("list", depth=depth), size
        size = len(json.dumps(reply))
        if size == previous or size > BIG_REPLY_BYTES:
            break
    return reply


def write_ax_dump(app, path):
    """Saves the controls on screen beside a mark's still so the proof hook can check the control a check names.

    Best effort: a dump that cannot be taken is a warning, never a failed mark. Returns the path or None."""
    try:
        controls = ax_controls(list_within_limit(app))
        with open(path, "w", encoding="utf-8") as f:
            json.dump({"controls": controls}, f, indent=1)
        return path
    except (TestModeError, OSError, AttributeError, TypeError, ValueError) as error:  # ValueError: a cut-off reply on a huge project
        print("ui-verify: no accessibility dump %s: %s" % (path, error), file=sys.stderr)
        return None
