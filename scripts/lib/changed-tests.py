#!/usr/bin/env python3
"""Prints the gtest filter for what a change can reach, for `ci-local.sh --fast`.

A test file is reached when it changed, or when its compile included a changed header, read from the
build's own dependency data (`ninja -t deps`). A changed .cpp under Source/ counts as a change to its
same-named header, the usual way tests reach it. The answer is the suites those test files define.

Prints ALL when the change reaches too much to be worth filtering, or anything the dependency data
cannot answer (a build input, or no Ninja data), and NONE when nothing is reached.
"""
import argparse
import os
import re
import subprocess
import sys

BUILD_INPUT = re.compile(r"(^|/)(CMakeLists\.txt|[^/]*\.cmake|TestPaths\.h\.in)$|^cmake/")
SUITE = re.compile(r"^\s*(?:TEST|TEST_F|TEST_P|TYPED_TEST|TYPED_TEST_P)\s*\(\s*(\w+)\s*,", re.M)
INSTANTIATE = re.compile(r"INSTANTIATE_TEST_SUITE_P\s*\(\s*(\w+)\s*,\s*(\w+)\s*,")


def changed_files(base):
    tracked = subprocess.run(["git", "diff", "--name-only", base], capture_output=True, text=True, check=True)
    untracked = subprocess.run(["git", "ls-files", "-o", "--exclude-standard"], capture_output=True, text=True, check=True)
    return sorted(set(tracked.stdout.split("\n") + untracked.stdout.split("\n")) - {""})


def header_users(build, wanted):
    """Maps each test source file to whether its compile read a header in `wanted`."""
    out = subprocess.run(["ninja", "-C", build, "-t", "deps"], capture_output=True, text=True, errors="replace")
    if out.returncode != 0 or not out.stdout:
        return None
    users, source = set(), None
    for line in out.stdout.splitlines():
        if line and not line[0].isspace():
            # The object names its source; the dependency list can start with an SDK file instead.
            match = re.match(r"Tests/CMakeFiles/Tests\.dir/(.+)\.o:", line)
            source = f"Tests/{match.group(1)}" if match else None
        elif source and line.strip():
            path = line.strip()
            while path.startswith("../"):
                path = path[3:]
            if path in wanted:
                users.add(source)
    return users


def suites_in(path):
    try:
        with open(path, errors="replace") as handle:
            text = handle.read()
    except OSError:
        return set()
    names = set(SUITE.findall(text))
    for prefix, suite in INSTANTIATE.findall(text):
        names.add(f"{prefix}/{suite}")
    return names


def filter_for(files, build, max_share):
    if any(BUILD_INPUT.search(f) for f in files):
        return "ALL"
    code = [f for f in files if f.startswith(("Source/", "Tests/")) and f.endswith((".h", ".cpp", ".mm"))]
    if not code:
        return "NONE"
    wanted = {f for f in code if f.endswith(".h")}
    wanted |= {re.sub(r"\.(cpp|mm)$", ".h", f) for f in code if f.startswith("Source/")}
    reached = {f for f in code if f.startswith("Tests/") and not f.endswith(".h")}
    users = header_users(build, wanted)
    if users is None:
        return "ALL"
    reached |= users

    all_tests = subprocess.run(["git", "ls-files", "Tests/*.cpp", "Tests/**/*.cpp"], capture_output=True, text=True)
    total = len(set(all_tests.stdout.split())) or 1
    if len(reached) > total * max_share:
        return "ALL"
    suites = set()
    for path in reached:
        suites |= suites_in(path)
    if not suites:
        return "NONE"
    return ":".join(sorted(f"{s}.*" if "/" not in s else f"{s}/*" for s in suites))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True)
    parser.add_argument("--base", required=True)
    parser.add_argument("--max-share", type=float, default=0.4)
    args = parser.parse_args()
    print(filter_for(changed_files(args.base), args.build, args.max_share))
    return 0


if __name__ == "__main__":
    sys.exit(main())
