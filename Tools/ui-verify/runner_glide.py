"""The `glide` step of run.py: send a command, film the screen `frames` times in a row, tile the strip, and prove the
frames hold the motion (different images inside the crop, and under `slow_motion` a landed last frame)."""

import os
import shutil
import sys
import time

from agentsynth_testmode import TestModeError
from recording import (GLIDE_DEFAULT_FRAMES, GLIDE_HOLD_SECONDS, GLIDE_LANDED_MAX_SHARE, GLIDE_LANDED_MIN_CHANGED)

GLIDE_MIN_DISTINCT_FRAMES = 3  # a glide whose crop shows fewer different frames than this filmed no motion


def run_glide(runner, args):
    """Sends `do`, then films the screen `frames` times in a row. The strip image tiles those frames."""
    (command, command_args), = args["do"].items()
    command_args = dict(command_args or {})
    timeout = command_args.pop("timeoutMs", None)
    started = time.time()
    app, recorder = runner.app, runner.recorder
    reply = app.call(command, timeout_ms=timeout, **command_args) if timeout else app.call(command, **command_args)
    if args.get("then"):
        (then_command, then_args), = args["then"].items()
        app.call(then_command, **dict(then_args or {}))
    first = len(recorder.frames)
    for _ in range(args.get("frames", GLIDE_DEFAULT_FRAMES)):
        recorder.frame(app, GLIDE_HOLD_SECONDS)
    taken = [path for path, _ in recorder.frames[first:]]
    if len(taken) < 4:
        raise TestModeError("glide %s: only %d frames could be taken" % (args["name"], len(taken)))
    distinct = recorder.distinct_frames(taken, crop=args.get("crop"))
    if distinct is not None and distinct < GLIDE_MIN_DISTINCT_FRAMES and not runner._step_instant:
        raise TestModeError("glide %s filmed no motion: the crop misses the change or the motion is faster than the "
                            "frames; fix the crop or add slow_motion (%d distinct frame%s in the crop)"
                            % (args["name"], distinct, "" if distinct == 1 else "s"))
    if runner.slowed and not runner._step_instant:
        check_landed(runner, args, taken)
    strip = os.path.join(runner.out_dir, "proof", args["name"] + "-strip.png")
    ok, message = recorder.write_strip(taken, strip, crop=args.get("crop"))
    if not ok:
        print("ui-verify: %s: no strip for %s: %s" % (runner.scenario.name, args["name"], message), file=sys.stderr)
    return {"do": command, "reply": reply, "frames": len(taken), "ms": int((time.time() - started) * 1000),
            "strip": strip if ok else None}


def check_landed(runner, args, taken):
    """A slowed glide must reach its end inside its frames. A message-thread stall (every recorded frame is one) moves
    an animation by a single 16 ms step, so `frames` frames cover only `frames` x 16 ms of it: a fade slowed too far
    is still mid-way on the last frame and pops to its colour on the next, which reads as no fade at all."""
    time.sleep(min(6.0, 0.4 * runner.slow_factor))  # real time again: the animation runs out
    recorder = runner.recorder
    settled = os.path.join(recorder.frames_dir, "settled-%s.png" % args["name"])
    target = recorder.target
    try:
        runner.app.call("screenshot", id=target["id"], scale=target["scale"], path=settled)
    except (TestModeError, OSError):
        return
    crop = args.get("crop")
    left = recorder.changed_fraction(taken[-1], settled, crop=crop)
    whole = recorder.changed_fraction(taken[0], settled, crop=crop)  # what the whole glide moved
    if left is None or whole is None or left <= GLIDE_LANDED_MIN_CHANGED or left <= whole * GLIDE_LANDED_MAX_SHARE:
        return
    for suffix, source in (("last", taken[-1]), ("landed", settled)):  # the two frames, for whoever reads the failure
        try:
            os.makedirs(os.path.join(runner.out_dir, "proof"), exist_ok=True)
            shutil.copyfile(source, os.path.join(runner.out_dir, "proof", "%s-%s.png" % (args["name"], suffix)))
        except OSError:
            pass
    raise TestModeError("glide %s ended mid-motion: its last frame differs from the landed state, so the clip shows a pop "
                        "at the end; lower the slow_motion factor or raise `frames` (a stalled frame moves an animation "
                        "16 ms, so 160 ms x factor needs about factor x 10 frames; %.1f%% of the crop still changes "
                        "after the last frame, the whole glide moved %.1f%%)" % (args["name"], left * 100, whole * 100))
