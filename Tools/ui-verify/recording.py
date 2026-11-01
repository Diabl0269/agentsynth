"""Turns a scenario run into a silent MP4: window screenshots through the test-mode protocol, joined by ffmpeg.

Frames come from the app's own `screenshot` command, so recording needs no Screen Recording permission and works while
the window is hidden. The protocol answers one command at a time, so a recorded drag is sent as its press, moves and
release with a frame after each move (see Runner._recorded_drag), and plays back at DRAG_HOLD_SECONDS per frame.
"""

import os
import shutil
import subprocess

from agentsynth_testmode import TestModeError

STEP_HOLD_SECONDS = 0.8
DRAG_HOLD_SECONDS = 0.1  # ten frames a second
LAST_FRAME_HOLD_SECONDS = 1.5
FFMPEG_FALLBACKS = ("/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg")


def find_ffmpeg():
    found = shutil.which("ffmpeg")
    if found:
        return found
    return next((path for path in FFMPEG_FALLBACKS if os.path.isfile(path)), None)


def even(value):
    return value + value % 2  # yuv420p needs even dimensions


class Recorder:
    def __init__(self, frames_dir):
        self.frames_dir = frames_dir
        self.frames = []  # [(path, seconds on screen)]
        self.size = None  # the first frame's; a later frame of another size is fitted inside it

    def frame(self, app, hold=STEP_HOLD_SECONDS):
        """One frame of the main window. A frame that cannot be taken is skipped: the step's own result decides pass or fail."""
        path = os.path.join(self.frames_dir, "frame-%05d.png" % len(self.frames))
        try:
            reply = app.call("screenshot", id="window", path=path)
        except (TestModeError, OSError):
            return
        if self.size is None:
            self.size = (even(reply["width"]), even(reply["height"]))
        self.frames.append((path, hold))

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
