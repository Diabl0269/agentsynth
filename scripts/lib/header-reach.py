#!/usr/bin/env python3
"""Counts, for every header under Source/, how many .cpp files under Source/ and Tests/ reach it.

A .cpp reaches a header when it includes it directly or through other headers. The count comes from
a static scan of #include lines in one pass, no build. An include resolves the way the build does:
next to the including file first, then under Source/ (the only include directory the project adds).
JUCE, system and generated headers resolve to nothing and are ignored.

  header-reach.py <root>                 prints "<count> <header>" lines, largest first
  header-reach.py <root> --includers H   prints "<header-count> <includer>" for each file that includes H directly
"""
import collections
import os
import re
import sys

INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*["<]([^">]+)[">]', re.M)


def build_graph(root):
    files = []
    for top in ("Source", "Tests"):
        for folder, _, names in os.walk(os.path.join(root, top)):
            files += [os.path.relpath(os.path.join(folder, n), root) for n in names if n.endswith((".h", ".cpp"))]
    known = set(files)
    graph = {}
    for path in files:
        try:
            with open(os.path.join(root, path), errors="replace") as handle:
                text = handle.read()
        except OSError:
            continue
        found = []
        for name in INCLUDE.findall(text):
            for cand in (os.path.normpath(os.path.join(os.path.dirname(path), name)), os.path.normpath("Source/" + name)):
                if cand in known:
                    found.append(cand)
                    break
        graph[path] = found
    return files, graph


def reach_counts(files, graph):
    counts = collections.Counter()
    for cpp in (f for f in files if f.endswith(".cpp")):
        seen, todo = set(), list(graph.get(cpp, []))
        while todo:
            header = todo.pop()
            if header not in seen:
                seen.add(header)
                todo.extend(graph.get(header, []))
        counts.update(seen)
    return counts


def main():
    root = sys.argv[1]
    files, graph = build_graph(root)
    counts = reach_counts(files, graph)
    if len(sys.argv) > 3 and sys.argv[2] == "--includers":
        target = sys.argv[3]
        rows = [(counts.get(f, 1 if f.endswith(".cpp") else 0), f) for f in files if target in graph.get(f, [])]
        for n, f in sorted(rows, key=lambda r: (-r[0], r[1])):
            print(n, f)
        return
    for header, n in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0])):
        if header.startswith("Source/") and header.endswith(".h"):
            print(n, header)


if __name__ == "__main__":
    main()
