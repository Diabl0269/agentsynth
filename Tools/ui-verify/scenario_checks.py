"""Parse-time checks of one scenario step (mark, glide, slow_motion, instant), apart from the runner."""

import re

from recording import GLIDE_DEFAULT_FRAMES

RUNNER_STEPS = {"expect", "screenshot", "wait", "type", "restart", "copyFixture", "mark", "glide", "slow_motion", "remember"}
SLOW_MOTION_MAX = 20
MARK_NAME = re.compile(r"^[a-z0-9-]+$")


class ScenarioError(ValueError):
    """The scenario file is malformed."""


def _check_instant(args, index, name):
    """`instant: "<reason>"` on a step: that step may land in one frame on purpose. The reason must say why."""
    if "instant" in args and (not isinstance(args["instant"], str) or not args["instant"].strip()):
        raise ScenarioError("%s: step %d: `instant` needs a reason string saying why it is deliberately instant" % (name, index))


def _check_mark(args, index, name, seen):
    """A `mark` step: {check: int >= 1, name: [a-z0-9-]+, before_s?: number in (0, 30]}, the name unique in the scenario."""
    where = "%s: step %d (mark)" % (name, index)
    if not {"check", "name"} <= set(args) or not set(args) <= {"check", "name", "before_s"}:
        raise ScenarioError("%s takes `check` and `name` and optionally `before_s`, got %r" % (where, sorted(args)))
    before = args.get("before_s")
    if "before_s" in args and (isinstance(before, bool) or not isinstance(before, (int, float)) or not 0 < before <= 30):
        raise ScenarioError("%s: `before_s` must be a number above 0 and at most 30, got %r" % (where, before))
    check, mark_name = args["check"], args["name"]
    if isinstance(check, bool) or not isinstance(check, int) or check < 1:
        raise ScenarioError("%s: `check` must be an integer of 1 or more, got %r" % (where, check))
    if not isinstance(mark_name, str) or not MARK_NAME.match(mark_name):
        raise ScenarioError("%s: `name` must be lower-case letters, digits and dashes, got %r" % (where, mark_name))
    if mark_name in seen:
        raise ScenarioError("%s: the name %r is already used by an earlier mark" % (where, mark_name))
    seen.add(mark_name)


def _check_slow_motion(args, index, name):
    """`slow_motion: {factor: N}`: every animation that starts afterwards runs N times longer (1 restores real time)."""
    factor = args.get("factor")
    if set(args) != {"factor"} or isinstance(factor, bool) or not isinstance(factor, (int, float)) \
            or not 1 <= factor <= SLOW_MOTION_MAX:
        raise ScenarioError("%s: step %d (slow_motion) takes only `factor`, a number from 1 to %d, got %r"
                            % (name, index, SLOW_MOTION_MAX, args))


def _check_glide(args, index, name, record, seen):
    """A `glide` step: {name: slug, do: {<command>: {args}}, then?: {<command>: {args}}, frames: 4..40}, only in a
    recorded scenario. `then` is a second command sent straight after `do`, while the motion `do` started still runs."""
    where = "%s: step %d (glide)" % (name, index)
    if not record:
        raise ScenarioError("%s needs `record: true`: it films the motion" % where)
    if not set(args) <= {"name", "do", "then", "frames", "crop", "instant"} or not {"name", "do"} <= set(args):
        raise ScenarioError("%s takes `name`, `do` and optionally `then`, `frames` and `crop`, got %r" % (where, sorted(args)))
    crop = args.get("crop")
    if crop is not None and (not isinstance(crop, dict) or set(crop) != {"x", "y", "w", "h"}
                             or not all(isinstance(v, int) and not isinstance(v, bool) and v >= 0 for v in crop.values())
                             or crop["w"] < 1 or crop["h"] < 1):
        raise ScenarioError("%s: `crop` must be {x, y, w, h} in whole pixels of the frame, got %r" % (where, crop))
    if not isinstance(args["name"], str) or not MARK_NAME.match(args["name"]) or args["name"] in seen:
        raise ScenarioError("%s: `name` must be a unique slug of lower-case letters, digits and dashes, got %r" % (where, args["name"]))
    seen.add(args["name"])
    for key in ("do", "then"):
        if key not in args:
            continue
        action = args[key]
        if not isinstance(action, dict) or len(action) != 1 or not isinstance(next(iter(action.values())) or {}, dict):
            raise ScenarioError("%s: `%s` must be one command with a mapping of arguments, got %r" % (where, key, action))
        if next(iter(action)) in RUNNER_STEPS:
            raise ScenarioError("%s: `%s` must be a protocol command (click, key, ...), got %r" % (where, key, next(iter(action))))
    frames = args.get("frames", GLIDE_DEFAULT_FRAMES)
    if isinstance(frames, bool) or not isinstance(frames, int) or not 4 <= frames <= 40:
        raise ScenarioError("%s: `frames` must be an integer from 4 to 40, got %r" % (where, frames))
