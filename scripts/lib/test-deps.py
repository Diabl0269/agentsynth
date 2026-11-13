#!/usr/bin/env python3
"""scripts/lib/test-deps.py -- a content key for each scripts/tests/*.test.sh, used by ci-local.sh.

ci-local.sh runs a script test only when its key changed since it last passed. The key hashes the
test, every file it names, and (repeating for each .sh/.awk/.py found) every file those name in turn,
plus a few whole directories when a file mentions them (workflows, patches). Naming is read from the
text: a path or a bare file name that resolves to a file under the repo, the scripts folders,
.github/workflows or cmake. This over-approximates on purpose (a mention in a comment counts), so a
test reruns more often than strictly needed and never less often.

Whenever the mapping is in doubt the key is empty and ci-local.sh runs the test: no file is found
besides the test itself, or the text reaches a folder by glob, or a file cannot be read.

Usage: test-deps.py --root <repo-root> <test-script>...
Prints "<name><TAB><key>" per test (name = file name without .test.sh).
"""

import hashlib
import os
import re
import sys

VERSION = "1"
TOKEN = re.compile(r"[A-Za-z0-9_./${}-]*[A-Za-z0-9_-]\.(?:sh|awk|py|txt|yml|yaml|cmake|patch)\b")
SEARCH_DIRS = ["", "scripts", "scripts/lib", "scripts/tests", ".github/workflows", "cmake"]
RECURSE_EXT = (".sh", ".awk", ".py")
SKIP_DIRS = {"__pycache__", ".git", "node_modules"}
# A mention of these pulls in the whole folder or file, whatever else resolves.
WHOLE = [
    (".github/workflows", [".github/workflows"]),
    ("cmake/patches", ["cmake/patches"]),
    ("Tests/CMakeLists.txt", ["Tests/CMakeLists.txt"]),
    (".clang-format", [".clang-format", ".clang-format-version"]),
]
# A glob into a scripts folder cannot be resolved name by name: take the whole folder.
GLOB = re.compile(r"(scripts(?:/lib|/tests)?)\"?/\*")


def read(root, rel):
    with open(os.path.join(root, rel), "rb") as f:
        return f.read()


def walk(root, rel):
    base = os.path.join(root, rel)
    if os.path.isfile(base):
        return [rel]
    out = []
    for d, dirs, files in os.walk(base):
        dirs[:] = [x for x in dirs if x not in SKIP_DIRS and not x.startswith("build")]
        for name in files:
            out.append(os.path.relpath(os.path.join(d, name), root))
    return out


def resolve(root, token):
    token = re.sub(r"^.*\$\{?[A-Za-z_][A-Za-z0-9_]*\}?/", "", token)
    token = re.sub(r"\$\{?[A-Za-z_][A-Za-z0-9_]*\}?", "", token).lstrip("./")
    if not token or token.startswith("/") or ".." in token:
        return []
    found = []
    for d in SEARCH_DIRS:
        rel = os.path.join(d, token) if d else token
        if os.path.isfile(os.path.join(root, rel)):
            found.append(rel)
    return found


def closure(root, test):
    seen, queue, whole, unsure = set(), [test], set(), False
    while queue:
        rel = queue.pop()
        if rel in seen:
            continue
        seen.add(rel)
        try:
            text = read(root, rel).decode("utf-8", "replace")
            # Whole-line comments name files as prose (a usage note, a pointer to a sibling script); skip them.
            text = "\n".join(l for l in text.split("\n") if not l.lstrip().startswith("#"))
        except OSError:
            unsure = True
            continue
        for needle, paths in WHOLE:
            if needle in text:
                whole.update(paths)
        if GLOB.search(text):
            unsure = True
        if rel.endswith(".py"):
            for m in re.finditer(r"^(?:from|import)\s+([A-Za-z_][A-Za-z0-9_]*)", text, re.M):
                cand = os.path.join(os.path.dirname(rel), m.group(1) + ".py")
                if os.path.isfile(os.path.join(root, cand)):
                    queue.append(cand)
        if not rel.endswith(RECURSE_EXT):
            continue
        for tok in TOKEN.findall(text):
            for dep in resolve(root, tok):
                if dep not in seen:
                    queue.append(dep)
                # A dependency outside the scripts and workflow folders brings its whole folder
                # (a Python tool's other modules, say), except at the repo root.
                top = os.path.dirname(dep)
                if top and not top.startswith(("scripts", ".github")) and "/" in dep:
                    whole.add(top)
    return seen, whole, unsure


def key_for(root, test):
    seen, whole, unsure = closure(root, test)
    if unsure or len(seen) < 2:
        return ""
    files = set(seen)
    for w in whole:
        files.update(walk(root, w))
    h = hashlib.sha1()
    h.update(VERSION.encode())
    with open(__file__, "rb") as me:
        h.update(me.read())
    for rel in sorted(files):
        try:
            h.update(("%s\0" % rel).encode())
            h.update(hashlib.sha1(read(root, rel)).digest())
        except OSError:
            h.update(b"missing\0")
    return h.hexdigest()


def main(argv):
    if len(argv) < 4 or argv[1] != "--root":
        print(__doc__)
        return 2
    root = os.path.abspath(argv[2])
    for test in argv[3:]:
        name = os.path.basename(test)
        if name.endswith(".test.sh"):
            name = name[: -len(".test.sh")]
        try:
            key = key_for(root, test)
        except Exception:
            key = ""
        print("%s\t%s" % (name, key))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
