"""Client for Agent Synth's in-app test mode (docs/development/test-mode.md). Standard library only.

    with TestModeApp() as app:
        app.call("openProject", path="/abs/path/project.agsproj")
        print(app.call("getState", id="card:LFO 1")["selected"])
"""

import json
import os
import queue
import shutil
import socket
import subprocess
import tempfile
import threading
import time

PORT_PREFIX = "AGENTSYNTH_AUTOMATION_PORT"
# Passed on every launch, though --automation implies both: a build that predates test mode ignores --automation and
# would otherwise read the keychain (a sign-in prompt nobody is there to answer) and open the audio device.
SAFETY_ARGS = ["--no-keychain", "--no-audio-device"]
SAFETY_ENV = {"AGENTSYNTH_NO_KEYCHAIN": "1", "AGENTSYNTH_NO_AUDIO_DEVICE": "1"}
HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))


class TestModeError(RuntimeError):
    """The app answered ok:false (or the connection broke)."""


def default_app_path():
    """The executable inside build-ci-local's Agent Synth.app, found the way scripts/ci-local.sh finds the bundle."""
    build = os.path.join(REPO_ROOT, "build-ci-local")
    for root, dirs, _files in os.walk(build):
        if "Agent Synth.app" in dirs:
            return os.path.join(root, "Agent Synth.app")
        dirs[:] = [d for d in dirs if not d.endswith(".app") and d not in ("CMakeFiles", "_deps")]
    raise TestModeError("no 'Agent Synth.app' under %s; build it with scripts/ci-local.sh" % build)


def _executable(app_path):
    if app_path.endswith(".app"):
        return os.path.join(app_path, "Contents", "MacOS", "Agent Synth")
    return app_path


def has_test_mode(executable):
    """True when the binary was built with test mode: it carries the port line it prints on start."""
    marker = PORT_PREFIX.encode("ascii")
    tail = b""
    with open(executable, "rb") as f:
        while True:
            chunk = f.read(1 << 20)
            if not chunk:
                return False
            if marker in tail + chunk:
                return True
            tail = chunk[-len(marker) :]


class TestModeApp:
    """Launches the app with --automation 0 (an OS-picked port, never a fixed one) and its own settings directory.

    The app is hidden by default (no window on screen, no Dock icon, never takes focus), so a run does not disturb
    whoever is using the machine. `visible=True` passes --automation-visible to watch it, and brings it to the front."""

    def __init__(self, app_path=None, settings_dir=None, startup_timeout=60.0, extra_args=(), visible=False):
        self.app_path = app_path or default_app_path()
        self._own_settings_dir = settings_dir is None
        self.settings_dir = settings_dir or tempfile.mkdtemp(prefix="agentsynth-testmode-")
        self.startup_timeout = startup_timeout
        self.extra_args = list(extra_args)
        self.visible = visible
        self.process = None
        self.port = None
        self.pid = None
        self._sock = None
        self._reader = None
        self._next_id = 1
        self._lock = threading.Lock()

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *_exc):
        self.stop()

    def start(self):
        executable = _executable(self.app_path)
        if not os.path.isfile(executable):
            raise TestModeError("no app at %s; build it with scripts/ci-local.sh" % executable)
        if not has_test_mode(executable):
            raise TestModeError(
                "%s was built without test mode (older than it, or a packaged build); build this worktree with "
                "scripts/ci-local.sh rather than borrowing another build" % executable
            )
        visible_args = ["--automation-visible"] if self.visible else []
        args = [executable, "--automation", "0", "--settings-dir", self.settings_dir] + SAFETY_ARGS + visible_args + self.extra_args
        env = dict(os.environ, **SAFETY_ENV)
        self.process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, env=env)
        # A reader thread, so the timeout holds even when the app prints nothing at all; it also keeps the pipe
        # draining afterwards, or a chatty app would block on a full stdout.
        lines = queue.Queue()

        def drain():
            for line in self.process.stdout:
                lines.put(line)
            lines.put(None)

        threading.Thread(target=drain, daemon=True).start()
        deadline = time.time() + self.startup_timeout
        while self.port is None and time.time() < deadline:
            try:
                line = lines.get(timeout=max(0.0, deadline - time.time()))
            except queue.Empty:
                break
            if line is None:
                break
            if line.startswith(PORT_PREFIX):
                _, port, pid = line.split()
                self.port, self.pid = int(port), int(pid)
        if self.port is None:
            self.stop()
            raise TestModeError("the app did not announce a test-mode port within %.0fs" % self.startup_timeout)
        self._sock = socket.create_connection(("127.0.0.1", self.port), timeout=10)
        self._reader = self._sock.makefile("r", encoding="utf-8", newline="\n")

    def activate(self):
        """Brings the app to the front: key events only reach the focused window of the ACTIVE application, so a
        scripted run started from a terminal (or with another app's window on top) would otherwise type into nothing.
        Best effort: needs pyobjc (AppKit) on macOS, a no-op elsewhere. A hidden app (the default) is never activated:
        test-mode input goes straight to the window, and activating would take focus from the user."""
        if not self.visible:
            return
        try:
            from AppKit import NSRunningApplication  # pyobjc, optional

            app = NSRunningApplication.runningApplicationWithProcessIdentifier_(self.pid)
            if app is not None:
                app.activateWithOptions_(3)  # ignoring other apps | all windows
        except ImportError:
            pass

    def call(self, cmd, timeout_ms=None, **args):
        """Runs one command; returns its result or raises TestModeError."""
        if cmd in ("key", "click", "drag"):
            self.activate()
        with self._lock:
            request = {"id": self._next_id, "cmd": cmd, "args": args}
            self._next_id += 1
            if timeout_ms:
                request["timeoutMs"] = timeout_ms
            self._sock.settimeout((timeout_ms or 30000) / 1000.0 + 10)
            self._sock.sendall((json.dumps(request) + "\n").encode("utf-8"))
            line = self._reader.readline()
        if not line:
            raise TestModeError("the app closed the connection during %r" % cmd)
        reply = json.loads(line)
        if not reply.get("ok"):
            raise TestModeError("%s failed: %s" % (cmd, reply.get("error")))
        return reply.get("result")

    def stop(self):
        if self.process is not None and self.process.poll() is None:
            try:
                if self._sock is not None:
                    self.call("quit", timeout_ms=5000)
            except (TestModeError, OSError):
                pass
            try:
                # With no connection there is nobody to ask to quit, so waiting would only add 15 s.
                self.process.wait(timeout=15 if self._sock is not None else 0.5)
            except subprocess.TimeoutExpired:
                self.process.terminate()  # this instance only, by its own pid
                try:
                    self.process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self.process.kill()
        if self._sock is not None:
            self._sock.close()
            self._sock = None
        if self._own_settings_dir:
            shutil.rmtree(self.settings_dir, ignore_errors=True)
