#!/usr/bin/env python3
"""scripts/lib/compile-changed.py -- syntax-check the changed translation units with the build's own flags.

ci-local.sh runs this before the long build so a missing include, a wrong constructor or an unused
variable stops the gate in seconds, not after a 20-40 minute build. Each changed .cpp that has an
entry in <build>/compile_commands.json is compiled with -fsyntax-only (no object, no depfile); the
first failure prints the compiler output and exits 1. Headers and files the build does not compile
(not in the database, e.g. other platforms' sources) are skipped.

Usage: compile-changed.py --build <dir> <changed-file>...
"""
import argparse
import json
import os
import shlex
import subprocess
import sys


def syntax_only(command: str) -> list:
    """The build's compile command with its output and dependency-file flags swapped for -fsyntax-only."""
    out, skip = [], False
    for arg in shlex.split(command):
        if skip:
            skip = False
        elif arg in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
        elif arg in ("-MD", "-MMD", "-MP"):
            continue
        else:
            out.append(arg)
    return out + ["-fsyntax-only"]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True)
    ap.add_argument("files", nargs="*")
    args = ap.parse_args()
    db_path = os.path.join(args.build, "compile_commands.json")
    if not os.path.isfile(db_path):
        print(f"compile-changed: no {db_path}; skipping the early compile.")
        return 0
    with open(db_path) as f:
        db = {os.path.realpath(e["file"]): e for e in json.load(f)}
    todo = [(p, db[os.path.realpath(p)]) for p in args.files if p.endswith(".cpp") and os.path.realpath(p) in db]
    if not todo:
        print("compile-changed: no changed .cpp is part of the build; nothing to compile early.")
        return 0
    for path, entry in todo:
        cmd = entry.get("command") or shlex.join(entry["arguments"])
        r = subprocess.run(syntax_only(cmd), cwd=entry["directory"], capture_output=True, text=True)
        if r.returncode != 0:
            sys.stdout.write(r.stdout + r.stderr)
            print(f"compile-changed: {path} does not compile (stopping before the full build).")
            return 1
        if r.stderr.strip():
            sys.stdout.write(r.stderr)
        print(f"compile-changed: ok {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
