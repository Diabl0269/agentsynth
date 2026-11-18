"""The project a scenario saves must reopen in the state the scenario's last checks name.

After a scenario with `save_project`, the runner starts a fresh app (a new settings folder), opens the saved bundle and
runs the scenario's final `expect` steps again: the trailing run of `expect`s, skipping steps that change nothing (`wait`,
`mark`, `screenshot`, `getState`, `list`, `remember`), and of those the fields the project holds (not UI_ONLY_FIELDS). A difference fails the scenario with both states printed: what the
check read before the save and what it reads after the reopen. A scenario that opens a project outside the repository
copies it first (`copyFixture: {name, from}`) and opens the copy, so a run never changes or autosaves beside someone's
own file. Opt-out: `skip_reopen_check: "<reason>"`; scenarios listed in known-realism-fails.txt with rule `reopen` only
note the difference.
"""

import json
import os
import shutil
import tempfile

IDLE_STEPS = {"mark", "screenshot", "wait", "getState", "list", "ping", "remember"}  # change nothing a reopened project would show
EXPECT_META = {"id", "contains", "above", "exists", "count", "within_ms"}
# Fields that say what the window shows or where the pointer and keys are, not what the project holds: a fresh app
# does not show the same pop-up, selection or focus, so a check on them alone is not compared after the reopen.
UI_ONLY_FIELDS = {"showing", "visible", "hasFocus", "bounds", "tooltip", "selected", "selectedPoints", "selectedStrips",
                  "selectedInView", "cursor", "topBarRing", "cableFlowPhase", "cableEndGliding", "outlinedSteps", "animationMode",
                  "enabled", "heldKey", "litKeys", "landingOutline", "modHoverChip", "modHoverChipRows", "focusedColumn",
                  "focusedRow", "focusedRowIndex", "zonesCursor", "outlinedStrip", "tipText", "tipAtPointer", "message"}
OWN_PATHS = ("${FIXTURES}", "${SCRATCH}", "${OUT}")  # fixtures, the run's own copy, a bundle an earlier scenario saved


def final_expects(steps):
    """The scenario's last state assertions: its trailing `expect` steps, in order, with the steps that change nothing
    skipped. Empty when the scenario ends on a step that changes the project (nothing to compare)."""
    found = []
    for command, args in reversed(steps):
        if command == "expect":
            found.append(args)
        elif command not in IDLE_STEPS:
            break
    return found[::-1]


def project_part(args):
    """`args` cut to the fields the project holds, or None when nothing is left to compare (only UI-only fields). The
    `exists` and `count` checks stay: a card that was there is part of the project."""
    kept = {k: v for k, v in args.items() if k in EXPECT_META or k not in UI_ONLY_FIELDS}
    for key in ("contains", "above"):
        if key in kept:
            inner = {f: v for f, v in kept[key].items() if f not in UI_ONLY_FIELDS}
            kept.pop(key)
            if inner:
                kept[key] = inner
    fields = [k for k in kept if k not in EXPECT_META] + [k for k in ("contains", "above") if k in kept]
    return kept if fields or "count" in kept or "exists" in kept else None


def check_project_paths(steps, name):
    """Raises ValueError when an `openProject` names a project outside fixtures/ (and not a copy or a saved bundle)."""
    for index, (command, args) in enumerate(steps, 1):
        path = str(args.get("path", "")) if command == "openProject" else ""
        if path and not path.startswith(OWN_PATHS):
            raise ValueError(
                "%s: step %d opens %r, a project outside fixtures/: copy it first with `copyFixture: {name: <bundle name>, "
                "from: %s}` and open `${SCRATCH}/<bundle name>`, so the run never changes or autosaves beside the original"
                % (name, index, path, path))


def describe(args, app, list_ids):
    """What an `expect` reads now, as a dict: the fields it names for a component, or the count it counts."""
    if "count" in args:
        spec = args["count"]
        tree = app.call("list", root=spec.get("root", "window"), depth=spec.get("depth", 40))
        found = [v for v in list_ids(tree, spec.get("field", "id")) if str(v).startswith(spec.get("prefix", ""))]
        return {"count of %s starting with %r" % (spec.get("field", "id"), spec.get("prefix", "")): len(found)}
    wanted = [k for k in args if k not in EXPECT_META] + list(args.get("contains") or {}) + list(args.get("above") or {})
    try:
        state = app.call("getState", id=args["id"])
    except Exception as error:  # a missing component is a state too
        return {"id": args["id"], "state": str(error)}
    return dict({"id": args["id"]}, **{field: state.get(field, "<missing>") for field in wanted})


def reopen_message(app_factory, app_path, bundle, expects, before, remembered, run_expect, list_ids, extra):
    """"" when every expect holds again in a fresh app that opened `bundle`, else the message with both states.
    `before` is describe() of each expect read in the scenario's own app; `extra` are the app factory's launch arguments (motion, visible)."""
    settings = tempfile.mkdtemp(prefix="agentsynth-reopen-")
    app = None
    try:
        app = app_factory(app_path, settings_dir=settings, **extra)
        app.start()
        app.call("openProject", path=bundle)
        for args, was in zip(expects, before):
            ok, message = run_expect(args, app, remembered=remembered)
            if not ok:
                return ("the saved project does not reopen in the state the check names: expect %s: %s\n  before saving: %s\n"
                        "  after reopening %s: %s" % (json.dumps(args), message, json.dumps(was), os.path.basename(bundle),
                                                      json.dumps(describe(args, app, list_ids))))
        return ""
    finally:
        if app is not None:
            app.stop()
        shutil.rmtree(settings, ignore_errors=True)
