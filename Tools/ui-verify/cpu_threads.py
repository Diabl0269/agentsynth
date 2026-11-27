"""Splits a process's CPU per thread, the way Activity Monitor's single % CPU number hides it.

Two sources, used together by idle_cpu.py:

- `ps -M -p <pid>` lists every thread with its user and system CPU time and its scheduling priority. Two snapshots a
  window apart give each thread's share of a core over that window. The first thread is the main (message) thread, and
  a thread at a real-time priority of 90 or more is the audio device's I/O thread (CoreAudio runs the app's callback on
  a priority-97 thread); everything else is "other" (JUCE timers, the window server's render threads, plugin scan,
  network).
- A `sample` call tree, which names each thread and shows where it spends its time. A sample of a thread that sits in a
  wait (mach_msg, a condition variable, a semaphore) is idle; every other sample is CPU. `sample_split` turns one call
  tree into busy % per thread, and `top_self` lists the functions a thread burns its own time in.
"""

import re
import subprocess

AUDIO_PRIORITY = 90  # CoreAudio's I/O thread runs at 97; nothing else in the app is that high
_THREAD_ROW = re.compile(r"(\d+\.\d)\s+\S+\s+(\d+)\S*\s+([\d:.-]+)\s+([\d:.-]+)")


def clock_seconds(text):
    """`[[dd-]hh:]mm:ss.cc` as seconds."""
    days = 0
    if "-" in text:
        d, text = text.split("-", 1)
        days = int(d)
    secs = 0.0
    for part in text.split(":"):
        secs = secs * 60 + float(part)
    return days * 86400 + secs


def parse_ps_threads(text):
    """`ps -M -p <pid>` output as a list of (priority, cpu seconds), one per thread, in ps's order (main thread first)."""
    threads = []
    for line in text.splitlines()[1:]:
        found = _THREAD_ROW.search(line)
        if found:
            threads.append((int(found.group(2)), round(clock_seconds(found.group(3)) + clock_seconds(found.group(4)), 3)))
    return threads


def thread_times(pid, run=subprocess.run):
    return parse_ps_threads(run(["ps", "-M", "-p", str(pid)], capture_output=True, text=True).stdout)


def thread_role(index, priority):
    if index == 0:
        return "main"
    return "audio" if priority >= AUDIO_PRIORITY else "other"


def split_between(before, after, seconds):
    """Each role's % of a core between two thread_times snapshots. Threads pair up by position while their priority
    matches; a thread that started inside the window counts all of its time, one that ended counts nothing."""
    split = {"audio": 0.0, "main": 0.0, "other": 0.0}
    if seconds <= 0:
        return split
    for index, (priority, cpu) in enumerate(after):
        start = 0.0
        if index < len(before) and before[index][0] == priority:
            start = before[index][1]
        split[thread_role(index, priority)] += max(0.0, cpu - start)
    return {role: round(100.0 * value / seconds, 1) for role, value in split.items()}


# Leaf functions a thread sits in while it waits: a sample that ends in one of them cost no CPU.
WAIT_LEAVES = ("mach_msg2_trap", "semaphore_wait_signal_trap", "mach_msg_trap", "mach_msg_overwrite", "__psynch_cvwait", "__psynch_mutexwait",
               "__semwait_signal", "semaphore_wait_trap", "semaphore_timedwait_trap", "kevent", "kevent64", "kevent_id",
               "__workq_kernreturn", "__select", "__ulock_wait", "__ulock_wait2", "__sigsuspend", "__accept", "poll",
               "__recvfrom", "__read_nocancel", "__open", "mach_wait_until", "__wait4", "__sleep", "nanosleep",
               "start_wqthread", "_pthread_wqthread", "thread_start")
_TREE_ROW = re.compile(r"^([ +!:|]*)(\d+) (.*)$")
_THREAD_HEAD = re.compile(r"^\s{4}(\d+) (Thread_\S+)(.*)$")


def _symbol(rest):
    return rest.split("  (in ")[0].split(" + ")[0].strip()


def _thread_label(rest):
    """`Thread_123   DispatchQueue_1: com.apple.main-thread  (serial)` -> `com.apple.main-thread`."""
    label = rest.split(":", 1)[1] if ":" in rest else rest
    return label.replace("(serial)", "").strip() or "unnamed"


def parse_sample(text):
    """A `sample` call graph as threads: [{"label", "total", "busy", "self": {symbol: busy samples}, "audio"}]."""
    threads, current, stack = [], None, []
    in_graph = False
    for line in text.splitlines():
        if line.startswith("Call graph:"):
            in_graph = True
            continue
        if not in_graph:
            continue
        if line.strip().startswith("Total number in stack") or line.startswith("Binary Images:"):
            break
        head = _THREAD_HEAD.match(line)
        if head:
            while current is not None and stack:
                _close(current, stack.pop())
            current = {"label": _thread_label(head.group(2) + head.group(3)), "total": int(head.group(1)), "busy": 0,
                       "self": {}, "audio": False}
            threads.append(current)
            stack = [(len(line) - len(line.lstrip()), int(head.group(1)), "thread", [0])]
            continue
        row = _TREE_ROW.match(line)
        if current is None or not row:
            continue
        depth, count, symbol = len(row.group(1)), int(row.group(2)), _symbol(row.group(3))
        if "IOWorkLoop" in symbol or "audioDeviceIOCallback" in symbol:
            current["audio"] = True
        while stack and stack[-1][0] >= depth:
            _close(current, stack.pop())
        if stack:
            stack[-1][3][0] += count
        stack.append((depth, count, symbol, [0]))
    while current is not None and stack:
        _close(current, stack.pop())
    return threads


def _close(thread, frame):
    _depth, count, symbol, children = frame
    own = count - children[0]
    if own <= 0 or symbol == "thread" or symbol.startswith(WAIT_LEAVES) or symbol.startswith("???"):
        return
    thread["busy"] += own
    thread["self"][symbol] = thread["self"].get(symbol, 0) + own


def sample_role(thread):
    if "main-thread" in thread["label"] or "Message Thread" in thread["label"]:
        return "main"
    return "audio" if thread["audio"] or "audio" in thread["label"].lower() else "other"


def sample_split(threads):
    """Busy % of a core per role from one call tree (the longest-lived thread's sample count is the whole window)."""
    window = max([t["total"] for t in threads] or [0])
    split = {"audio": 0.0, "main": 0.0, "other": 0.0}
    for thread in threads:
        split[sample_role(thread)] += thread["busy"]
    return {role: round(100.0 * value / window, 1) if window else 0.0 for role, value in split.items()}


def top_self(threads, role, count=12):
    """The functions a role's threads burn their own time in, most first, as (symbol, % of that role's busy time)."""
    totals = {}
    for thread in threads:
        if sample_role(thread) == role:
            for symbol, n in thread["self"].items():
                totals[symbol] = totals.get(symbol, 0) + n
    busy = sum(totals.values()) or 1
    ranked = sorted(totals.items(), key=lambda item: -item[1])[:count]
    return [(symbol, round(100.0 * n / busy, 1)) for symbol, n in ranked]
