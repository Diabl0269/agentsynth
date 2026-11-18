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
                  "focusedRow", "focusedRowIndex", "zonesCursor", "outlinedStrip", "tipText", "tipAtPointer", "message",
                  "pickedPad", "pickedKind", "modDots", "modHoverChipAlpha", "cableEndOnKnob", "nameFits", "compactLook",
                  "pixelsPerSemitone", "frameRunning", "frameCount", "firstRowText", "firstStopText", "severity",
                  "filterSpectrumAtFloor"}
# Components of the window's view, not of the project: an editor, panel or pop-up a fresh app does not have open (or
# opens in its own default look). A check on one is not compared after the reopen; the id is its first segment (`a > b`).
UI_ONLY_IDS = {"drumMachineState", "curveEditorState", "controllerSurfaceState", "timelineClipSizingState", "eqWindowStrip",
               "portConnectionsPanel", "modDotPopover", "modulatorBandState", "pianoRollZoomState", "samplerMapDrumHint",
               "modCurveSnap", "soundBrowser", "timelineToolSplit", "timelineSnapCombo", "toastState", "timelinePlayhead",
               "depthPicker", "High key", "Plays on state", "drumPlayClip", "statusBar"}
UI_ONLY_ID_PREFIXES = ("Filter shapes:", "packDisclosure:")
UI_ONLY_ID_PARTS = (" mod lane on ",)  # a modulation lane row exists only while its card's lanes are open
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


def view_only(component):
    """True when `component` (an id, maybe `root > child`) belongs to the window's view, not the project."""
    root = str(component).split(" > ")[0]
    return root in UI_ONLY_IDS or root.startswith(UI_ONLY_ID_PREFIXES) or any(part in root for part in UI_ONLY_ID_PARTS)


def project_part(args):
    """`args` cut to the fields the project holds, or None when nothing is left to compare (only UI-only fields or a
    view-only component). `exists` and a `count` of cards stay: a card that was there is part of the project; a count of
    anything else (track name labels, menu rows) is what the open view shows."""
    if "id" in args and view_only(args["id"]):
        return None
    if "count" in args and not str(args["count"].get("prefix", "")).startswith("card:"):
        return None
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
            if not ok and "no component" in message and not str(args.get("id", "")).startswith("card:"):
                continue  # a panel or pop-up a fresh app does not have open: the view, not the project
            if not ok:
                return ("the saved project does not reopen in the state the check names: expect %s: %s\n  before saving: %s\n"
                        "  after reopening %s: %s" % (json.dumps(args), message, json.dumps(was), os.path.basename(bundle),
                                                      json.dumps(describe(args, app, list_ids))))
        return ""
    finally:
        if app is not None:
            app.stop()
        shutil.rmtree(settings, ignore_errors=True)
