"""Turns a scenario run into a silent MP4: screen frames through the test-mode protocol, joined by ffmpeg.

Frames come from the app's own `screenshot` command (id `screen`: the main window with every other window of the app,
such as a menu, a call-out or the shape editor window, and the pointer), so recording needs no Screen Recording
permission and works while the window is hidden. The protocol answers one command at a time, so a recorded drag is sent as its press, moves and
release with a frame after each move (see Runner._recorded_drag), and plays back at DRAG_HOLD_SECONDS per frame.
"""

import os
import shutil
import subprocess

from agentsynth_testmode import TestModeError

STEP_HOLD_SECONDS = 0.8
DRAG_HOLD_SECONDS = 0.1  # ten frames a second
LAST_FRAME_HOLD_SECONDS = 1.5
MARK_LEAD_SECONDS = 2.5  # a proof clip starts this long before its mark frame ...
MARK_TAIL_SECONDS = 3.0  # ... and ends this long after it
MARK_MIN_SECONDS = 4.0
MARK_MAX_SECONDS = 6.0
MARK_HOLD_SECONDS = 1.5
DEFAULT_TARGET = {"id": "screen", "scale": 1}
FFMPEG_FALLBACKS = ("/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg")


def find_ffmpeg():
    found = shutil.which("ffmpeg")
    if found:
        return found
    return next((path for path in FFMPEG_FALLBACKS if os.path.isfile(path)), None)


def even(value):
    return value + value % 2  # yuv420p needs even dimensions


class Recorder:
    def __init__(self, frames_dir, target=None):
        self.frames_dir = frames_dir
        self.target = target or dict(DEFAULT_TARGET)  # what a frame shows
        self.frames = []  # [(path, seconds on screen)]
        self.timeline = []  # the second each frame appears at in the video, parallel to `frames`
        self.size = None  # the first frame's; a later frame of another size is fitted inside it

    def frame(self, app, hold=STEP_HOLD_SECONDS, copy_to=None):
        """One frame of the target (the screen by default). A frame that cannot be taken is skipped: the step's own
        result decides pass or fail. Returns the second it starts at in the video, or None when skipped; `copy_to` also
        keeps the frame's image at that path."""
        path = os.path.join(self.frames_dir, "frame-%05d.png" % len(self.frames))
        try:
            reply = app.call("screenshot", id=self.target["id"], scale=self.target["scale"], path=path)
            if copy_to:
                os.makedirs(os.path.dirname(copy_to), exist_ok=True)
                shutil.copyfile(path, copy_to)
        except (TestModeError, OSError):
            return None
        if self.size is None:
            self.size = (even(reply["width"]), even(reply["height"]))
        start = self.duration()
        self.frames.append((path, hold))
        self.timeline.append(start)
        return start

    def duration(self):
        """Seconds the video lasts: every frame's hold, the last one for LAST_FRAME_HOLD_SECONDS."""
        if not self.frames:
            return 0.0
        return sum(hold for _, hold in self.frames[:-1]) + LAST_FRAME_HOLD_SECONDS

    def clip_window(self, mark_start):
        """(start, length) of the clip around a mark frame: 2.5 s before to 3 s after, clamped to the video, at least 4 s
        and at most 6 s when the video allows."""
        total = self.duration()
        start = max(0.0, mark_start - MARK_LEAD_SECONDS)
        end = min(total, mark_start + MARK_TAIL_SECONDS)
        if end - start < MARK_MIN_SECONDS:
            end = min(total, start + MARK_MIN_SECONDS)
            start = max(0.0, min(start, end - MARK_MIN_SECONDS))
        return start, min(end - start, MARK_MAX_SECONDS)

    def cut_clip(self, video_path, mark_start, clip_path, ffmpeg=None):
        """Re-encodes the clip around a mark out of the written video. Returns (ok, message)."""
        ffmpeg = ffmpeg or find_ffmpeg()
        if not ffmpeg:
            return False, "ffmpeg not found; install it to get a clip"
        start, length = self.clip_window(mark_start)
        command = [ffmpeg, "-y", "-loglevel", "error", "-ss", "%.3f" % start, "-i", video_path, "-t", "%.3f" % length,
                   "-c:v", "libx264", "-crf", "28", "-pix_fmt", "yuv420p", "-movflags", "+faststart", "-an", clip_path]
        try:
            done = subprocess.run(command, capture_output=True, text=True, timeout=300)
        except (OSError, subprocess.TimeoutExpired) as error:
            return False, "ffmpeg did not finish: %s" % error
        if done.returncode != 0:
            return False, "ffmpeg failed: %s" % done.stderr.strip()
        return True, clip_path

    def write_video(self, video_path, ffmpeg=None):
        """Returns (ok, message). A scenario with no frames, or a machine with no ffmpeg, gets no video."""
        ffmpeg = ffmpeg or find_ffmpeg()
        if not self.frames:
            return False, "no frames to join"
        if not ffmpeg:
            return False, "ffmpeg not found; install it to get a video"
        list_path = os.path.join(self.frames_dir, "frames.txt")
        with open(list_path, "w", encoding="utf-8") as handle:
            for index, (path, hold) in enumerate(self.frames):
                last = index == len(self.frames) - 1
                handle.write("file '%s'\nduration %.3f\n" % (path, LAST_FRAME_HOLD_SECONDS if last else hold))
            handle.write("file '%s'\n" % self.frames[-1][0])  # the concat demuxer ignores the last duration otherwise
        width, height = self.size
        fit = "scale=%d:%d:force_original_aspect_ratio=decrease,pad=%d:%d:(ow-iw)/2:(oh-ih)/2,format=yuv420p" % (
            width, height, width, height)
        command = [ffmpeg, "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", list_path, "-vf", fit,
                   "-r", "30", "-c:v", "libx264", "-crf", "28", "-movflags", "+faststart", "-an", video_path]
        try:
            done = subprocess.run(command, capture_output=True, text=True, timeout=300)
        except (OSError, subprocess.TimeoutExpired) as error:
            return False, "ffmpeg did not finish: %s" % error
        if done.returncode != 0:
            return False, "ffmpeg failed: %s" % done.stderr.strip()
        return True, video_path
