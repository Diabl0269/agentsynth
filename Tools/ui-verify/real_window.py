"""A small set of scenarios runs in a real, visible window while nobody is at the machine (`real_window: true`).

The hidden test-mode launch cannot show focus, the CPU a visible window costs or the keys the OS delivers. A
`real_window` scenario starts the app visible (`--automation-visible`) with motion on (`--automation-motion`) and sends
its `key` and `type` steps through the OS to the app's process: System Events brings that pid to the front and types
into it (needs Accessibility permission for the terminal, so run it outside the sandbox). Mouse steps still go through
test mode. Because it takes the keyboard and the screen, it runs only with `--real-window` and only when the user is
away: the machine has had no input for `--idle-seconds` (300), or `AGENTSYNTH_USER_AWAY=1` is set by an unattended
launcher. Otherwise the scenario is reported SKIPPED with the reason, which is not a failure. The default run skips them.
"""

import os
import re
import subprocess
import time

IDLE_SECONDS = 300
AWAY_ENV = "AGENTSYNTH_USER_AWAY"
KEY_CODES = {"return": 36, "enter": 36, "tab": 48, "space": 49, "delete": 51, "backspace": 51, "escape": 53, "left": 123,
             "right": 124, "down": 125, "up": 126}
MODIFIERS = {"cmd": "command down", "command": "command down", "shift": "shift down", "alt": "option down",
             "option": "option down", "ctrl": "control down", "control": "control down"}
KEY_PAUSE_SECONDS = 0.15  # between two keys, so the app handles each before the next


def user_idle_seconds(run=subprocess.run):
    """Seconds since the last keyboard or mouse input (macOS HIDIdleTime from ioreg), or None when it cannot be read."""
    try:
        text = run(["ioreg", "-c", "IOHIDSystem"], capture_output=True, text=True, timeout=10).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    found = re.search(r'"HIDIdleTime"\s*=\s*(\d+)', text or "")
    return int(found.group(1)) / 1e9 if found else None


class AwayGuard:
    """Whether a real-window scenario may run now. Once the user has been seen away the run goes on: its own key events
    reset the OS idle time, so the second scenario would otherwise look like a user came back."""

    def __init__(self, enabled, minimum=IDLE_SECONDS, env=None, idle_reader=None):
        self.enabled = enabled
        self.minimum = minimum
        self.env = os.environ if env is None else env
        self.idle_reader = idle_reader or (lambda: user_idle_seconds())  # looked up when read, so a test can replace it
        self.confirmed = False

    def skip_reason(self):
        """"" when the scenario may run, else why it is skipped."""
        if not self.enabled:
            return "a real-window scenario runs only with --real-window, while nobody is at the machine"
        if self.confirmed or self.env.get(AWAY_ENV) == "1":
            return ""
        idle = self.idle_reader()
        if idle is None:
            return "cannot tell whether anyone is at the machine (ioreg gave no idle time) and %s=1 is not set" % AWAY_ENV
        if idle < self.minimum:
            return "someone used the machine %d s ago and %d s of idle are needed (or %s=1)" % (idle, self.minimum, AWAY_ENV)
        self.confirmed = True
        return ""


def _quoted(text):
    return '"%s"' % text.replace("\\", "\\\\").replace('"', '\\"')


def key_script(pid, chord):
    """The AppleScript that brings process `pid` to the front and presses one chord (`cmd+z`, `escape`, `shift+tab`)."""
    parts = chord.split("+") if len(chord) > 1 and not chord.endswith("+") else [chord]
    key, held = parts[-1], parts[:-1]
    unknown = [m for m in held if m.lower() not in MODIFIERS]
    if unknown:
        raise ValueError("unknown modifier %r in %r" % (unknown[0], chord))
    using = " using {%s}" % ", ".join(MODIFIERS[m.lower()] for m in held) if held else ""
    if key.lower() in KEY_CODES:
        press = "key code %d%s" % (KEY_CODES[key.lower()], using)
    else:
        press = "keystroke %s%s" % (_quoted(key), using)
    return ('tell application "System Events"\n  set frontmost of (first process whose unix id is %d) to true\n  %s\nend tell'
            % (pid, press))


def send_chord(pid, chord, run=subprocess.run, sleep=time.sleep):
    """Presses one chord in the app with that pid through System Events. Raises RuntimeError when the OS refuses."""
    done = run(["osascript", "-e", key_script(pid, chord)], capture_output=True, text=True, timeout=15)
    if done.returncode != 0:
        raise RuntimeError("System Events refused the key %r: %s (Accessibility permission for the terminal, and not sandboxed)"
                           % (chord, (done.stderr or "").strip()))
    sleep(KEY_PAUSE_SECONDS)


def os_keys(pid, command, args, send=send_chord):
    """A `key` or `type` step as OS key presses to `pid`. Returns the reply recorded in the run log."""
    if command == "type":
        chords = ["space" if char == " " else char for char in str(args.get("text", ""))]
    else:
        chords = str(args.get("keys", "")).split()
    for chord in chords:
        send(pid, chord)
    return {"os_keys": chords, "pid": pid}
