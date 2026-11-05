#!/usr/bin/env python3
"""Finds names two sources of one Windows unity group both declare in an anonymous namespace.

Used by scripts/check-unity-clashes.sh. Usage: unity-clashes.py <repo-root>
Prints one "<key>\t<fileA>\t<fileB>\t<group>" line per clashing pair (files sorted), then exits 0.
A second kind of line has a header as fileB: a source's anonymous-namespace name that a header another
source of the group #includes (directly, one level) declares at named-namespace scope.

Grouping mirrors synth_enable_unity in cmake/Pch.cmake: per target (Core, AppUI, Tests), the .cpp
files not in SYNTH_UNITY_SKIP are counted per folder; a folder splits into count/SIZE+1 groups and a
file lands in group md5(relative path)[0:4] % groups. Source lists are read from the CMake files
(add_library / add_executable / target_sources / APPUI_SOURCES).
"""
import hashlib
import os
import re
import sys

TARGETS = ("Core", "AppUI", "Tests")
KEYWORDS_NOT_NAMES = {"const", "constexpr", "static", "inline", "volatile", "mutable", "extern", "auto"}


def read(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read()


def cmake_commands(text):
    text = re.sub(r"#[^\n]*", "", text)
    return re.finditer(r"([A-Za-z_]+)\s*\(([^()]*(?:\([^()]*\)[^()]*)*)\)", text)


def collect_sources(root):
    """Returns {target: set(repo-relative .cpp paths)} as each target holds them when its
    synth_enable_unity() call runs: sources added by files included after that call are not merged."""
    snapshots = {}
    for top, base in ((os.path.join(root, "CMakeLists.txt"), ""), (os.path.join(root, "Tests", "CMakeLists.txt"), "Tests")):
        state = {"sources": {t: set() for t in TARGETS}, "appui": [], "done": False}
        if os.path.isfile(top):
            walk(root, top, base, state, snapshots)
    return snapshots


def cpp_tokens(args, base, root):
    out = []
    for token in args:
        token = token.replace("${CMAKE_CURRENT_SOURCE_DIR}/", "")
        if token.endswith(".cpp") and "$" not in token:
            rel = os.path.normpath(os.path.join(base, token))
            if os.path.isfile(os.path.join(root, rel)):
                out.append(rel)
    return out


def walk(root, path, base, state, snapshots):
    for match in cmake_commands(read(path)):
        command, args = match.group(1).lower(), match.group(2).split()
        if command == "include" and args:
            name = args[0].replace("${CMAKE_CURRENT_LIST_DIR}", os.path.dirname(path))
            name = name.replace("${CMAKE_CURRENT_SOURCE_DIR}", os.path.join(root, base))
            name = name.replace("${CMAKE_SOURCE_DIR}", root)
            candidates = [name] if os.path.isabs(name) else [os.path.join(root, base, name), os.path.join(root, name)]
            found = next((c for c in candidates if os.path.isfile(c)), None)
            if found and found.endswith(".cmake"):
                walk(root, found, base, state, snapshots)
        elif command in ("add_library", "add_executable", "target_sources") and args and args[0] in TARGETS:
            target = args[0]
            state["sources"][target].update(cpp_tokens(args[1:], "Tests" if target == "Tests" else "", root))
            if "${APPUI_SOURCES}" in args:
                state["sources"][target].update(cpp_tokens(state["appui"], "", root))
        elif command == "set" and args[:1] == ["APPUI_SOURCES"]:
            state["appui"] = list(args[1:])
        elif command == "list" and args[:2] == ["APPEND", "APPUI_SOURCES"]:
            state["appui"] += args[2:]
        elif command == "synth_enable_unity" and args and args[0] in TARGETS:
            snapshots[args[0]] = set(state["sources"][args[0]])


def group_size(root):
    match = re.search(r"AGENTSYNTH_UNITY_GROUP_SIZE\s+(\d+)", read(os.path.join(root, "cmake", "Pch.cmake")))
    return int(match.group(1)) if match else 12


def skipped(root):
    path = os.path.join(root, "cmake", "UnityExclusions.cmake")
    if not os.path.isfile(path):
        return set()
    match = re.search(r"SYNTH_UNITY_SKIP\s*(.*?)\)", re.sub(r"#[^\n]*", "", read(path)), re.S)
    return set(match.group(1).split()) if match else set()


def unity_groups(root):
    """Returns {group-name: [repo-relative .cpp, ...]}, as Pch.cmake assigns them."""
    size, skip = group_size(root), skipped(root)
    groups = {}
    for target, paths in collect_sources(root).items():
        counts = {}
        entries = []
        for rel in sorted(paths):
            if rel in skip:
                continue
            folder = re.sub(r"[^A-Za-z0-9_]", "_", os.path.dirname(rel))
            counts[folder] = counts.get(folder, 0) + 1
            entries.append((rel, folder))
        for rel, folder in entries:
            bucket = int(hashlib.md5(rel.encode()).hexdigest()[:4], 16) % (counts[folder] // size + 1)
            groups.setdefault("%s:%s_%d" % (target, folder, bucket), []).append(rel)
    return groups


TOKEN = re.compile(
    r'//[^\n]*|/\*.*?\*/|(?<![\w])R"([^(\s]*)\(.*?\)\1"|"(?:\\.|[^"\\\n])*"|(?<![\w])\'(?:\\.|[^\'\\\n])*\'',
    re.S)


def blank_out(text):
    """Drops comments, string and char literals (keeping newlines) and preprocessor lines."""
    def repl(m):
        t = m.group(0)
        if t.startswith("//"):
            return ""
        if t.startswith("/*"):
            return "\n" * t.count("\n")
        return ('""' if t[-1] == '"' else "0") + "\n" * t.count("\n")
    text = TOKEN.sub(repl, text)
    return re.sub(r"(?m)^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", "", text)


def strip_template_prefix(stmt):
    stmt = stmt.strip()
    while stmt.startswith("template"):
        rest = stmt[len("template"):].lstrip()
        if not rest.startswith("<"):
            break
        depth = 0
        for k, ch in enumerate(rest):
            depth += ch == "<"
            depth -= ch == ">"
            if depth == 0:
                stmt = rest[k + 1:].strip()
                break
        else:
            break
    return stmt


def param_key(params):
    """Parameter types without names, so overloads differ and a renamed copy still matches."""
    parts, depth, cur = [], 0, ""
    for ch in params:
        depth += ch in "(<[{"
        depth -= ch in ")>]}"
        if ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    parts.append(cur)
    out = []
    for part in parts:
        part = re.sub(r"\s*=.*$", "", part.strip(), flags=re.S)
        part = re.sub(r"\s+", " ", part)
        part = re.sub(r"\s*([&*])\s*", r"\1", part)
        if re.search(r"[\w>&*\]] \w+$|[&*]\w+$", part) and not part.endswith(("unsigned", "long", "short")):
            part = re.sub(r"\s*\w+$", "", part)
        out.append(part)
    return ",".join(out)


def declared_name(stmt, ended_by):
    """Classifies one namespace-scope statement. Returns (kind, name, params) or None."""
    stmt = strip_template_prefix(re.sub(r"\[\[[^\]]*\]\]", " ", stmt))
    stmt = re.sub(r"\s+", " ", stmt).strip()
    if not stmt:
        return None
    first = stmt.split(" ")[0]
    if first in ("using", "typedef"):
        if first == "using" and re.match(r"using\s+namespace\b", stmt):
            return None
        m = re.match(r"using\s+(\w+)\s*=\s*(.*)$", stmt)
        if m:
            target = re.sub(r"\b[a-z_]\w*::", "", m.group(2))
            return ("alias", m.group(1), re.sub(r"\s+", "", target))
        m = re.search(r"(\w+)\s*(?:\[[^\]]*\])?$", stmt) if first == "typedef" else None
        return ("type", m.group(1), None) if m else None
    if first in ("friend", "static_assert", "namespace", "extern") or stmt.startswith("extern "):
        return None
    td = re.match(r"(?:struct|class|union|enum(?: class| struct)?)\s+(?:\w+_API\s+)?([A-Za-z_]\w*)\s*(<.*>)?\s*(?:final\s*)?(?::(?!:)[^{]*)?$", stmt)
    if td and ended_by == "{":
        return ("type", td.group(1) + re.sub(r"\s+", "", td.group(2) or ""), None)
    if re.match(r"(?:struct|class|union|enum)\b", stmt) and ended_by == ";" and "=" not in stmt and "(" not in stmt:
        return None  # forward declaration, or `struct S x;`
    assign = re.search(r"(?<![=!<>])=(?!=)", stmt)
    head = stmt[:assign.start()] if assign else stmt
    if ended_by == "{" and not assign and "(" in head:
        depth, start = 0, None
        for k, ch in enumerate(head):
            if ch == "(":
                if depth == 0 and start is None:
                    start = k
                depth += 1
            elif ch == ")":
                depth -= 1
        m = re.search(r"([~\w]+)\s*$", head[:start])
        if not m or "::" in head[:start].split(" ")[-1] or "operator" in head[:start] or "~" in m.group(1):
            return None
        params = head[start + 1:head.rfind(")")]
        return ("func", m.group(1), param_key(params)) if m.group(1) not in KEYWORDS_NOT_NAMES else None
    if ended_by == ";" and not assign and "(" in head:
        if not re.search(r"\bconst(?:expr)?\b", head):
            return None  # a prototype
        head = head[:head.index("(")]
    head = re.sub(r"\[[^\]]*\]\s*$", "", head.strip())
    m = re.search(r"([A-Za-z_]\w*)\s*$", head)
    tokens = head.split(" ")
    if not m or len(tokens) < 2 or m.group(1) in KEYWORDS_NOT_NAMES or tokens[0] in ("return", "using"):
        return None
    return ("var", m.group(1), None)


def enumerators(body, scoped):
    if scoped:
        return []
    names = []
    for item in body.split(","):
        m = re.match(r"\s*([A-Za-z_]\w*)", item)
        if m:
            names.append(m.group(1))
    return names


def declarations(text, named=False):
    """Returns {qualified key: alias target or None} for the names declared at anonymous-namespace scope,
    or, with named=True (a header), at the scope of a named namespace (never inside an anonymous one)."""
    text = blank_out(text)
    found = {}
    stack = []  # frames: ("ns", name) or ("body", None)
    stmt = ""
    scope = None
    i, n = 0, len(text)

    def in_anon_scope():
        if not all(f[0] == "ns" for f in stack):
            return False
        anon = any(f[1] == "(anon)" or f[1].endswith("::(anon)") for f in stack)
        return (not anon and any(f[1] for f in stack)) if named else anon

    def prefix():
        return "::".join(f[1] for f in stack if f[0] == "ns") + "::"

    while i < n:
        c = text[i]
        if scope is not None and not scope[0] and c not in "{}":
            j = i + 1
            while j < n and text[j] not in "{}":
                j += 1
            i = j
            continue
        if c in "{};" or scope is None:
            scope = (all(f[0] == "ns" for f in stack), None)
        top_level = scope[0]
        if c == "{":
            if top_level:
                head = re.sub(r"\s+", " ", stmt).strip()
                if re.match(r"(?:inline )?namespace\b", head):
                    names = re.sub(r"^(?:inline )?namespace\s*", "", head).strip()
                    stack.append(("ns", names.replace(" ", "") or "(anon)"))
                    stmt = ""
                    i += 1
                    scope = None
                    continue
                if head.startswith("extern"):
                    stack.append(("ns", ""))
                    stmt = ""
                    i += 1
                    scope = None
                    continue
                if in_anon_scope() and stack:
                    decl = declared_name(head, "{")
                    initializer = re.search(r"(?<![=!<>])=(?!=)", head) is not None
                    brace_init = decl is not None and decl[0] == "var"
                    depth, j = 1, i + 1
                    while j < n and depth:
                        depth += text[j] == "{"
                        depth -= text[j] == "}"
                        j += 1
                    body = text[i + 1:j - 1]
                    if decl and decl[0] in ("type", "func") and not initializer:
                        found[prefix() + decl[1] + ("(" + decl[2] + ")" if decl[0] == "func" else "")] = None
                        if decl[0] == "type" and re.match(r"enum\b", head):
                            for e in enumerators(body, bool(re.match(r"enum (class|struct)\b", head))):
                                found[prefix() + e] = None
                        stmt = "" if decl[0] == "func" else "struct_tail"
                    else:
                        stmt = head + " {} " if (initializer or brace_init) else ""
                    i = j
                    scope = None
                    continue
                stack.append(("body", None))
            else:
                stack.append(("body", None))
            i += 1
            scope = None
            continue
        if c == "}":
            if stack:
                stack.pop()
            stmt = ""
            i += 1
            scope = None
            continue
        if top_level and stack and in_anon_scope():
            if c == ";":
                if stmt != "struct_tail":
                    decl = declared_name(stmt, ";")
                    if decl:
                        key = prefix() + decl[1] + ("(" + decl[2] + ")" if decl[0] == "func" else "")
                        found[key] = decl[2] if decl[0] == "alias" else None
                stmt = ""
            else:
                stmt += c
        elif top_level:
            stmt += c if c != ";" else ""
            if c == ";":
                stmt = ""
        i += 1
    return found


USING_NAMESPACE = re.compile(r"\busing\s+namespace\s+(?:::)?([\w:]+)\s*;")
INCLUDE = re.compile(r'(?m)^[ \t]*#[ \t]*include[ \t]+"([^"]+)"')


def included_headers(root, rel):
    """Project headers a source #includes directly (one level), resolved against Source/, the source's
    folder and the repo root. Returns repo-relative paths."""
    found = []
    for name in INCLUDE.findall(read(os.path.join(root, rel))):
        for base in (os.path.join("Source"), os.path.dirname(rel), ""):
            cand = os.path.normpath(os.path.join(base, name))
            if os.path.isfile(os.path.join(root, cand)):
                found.append(cand)
                break
    return found


def split_key(key):
    """Returns (namespace segments without anonymous ones, bare name) of a qualified key."""
    k = key.replace("(anon)", "@A@")
    head = k.split("(", 1)[0]
    cut = head.rfind("::")
    ns = k[:cut] if cut >= 0 else ""
    return [seg for seg in ns.split("::") if seg and seg != "@A@"], k[cut + 2:] if cut >= 0 else k


def header_clashes(root, paths, anon_decls, header_cache):
    """Yields (anonymous key, source, header) where a source of the group declares, in an anonymous
    namespace, a name a header included by another source of the group declares at named-namespace scope
    in the same namespace (an unqualified use is ambiguous), or in a namespace below it that some source of
    the group opens with a namespace-scope `using namespace` (the directive makes the header's name appear
    in the common ancestor, beside the anonymous one, and in a unity build it leaks into every later source)."""
    directives = set()
    for rel in paths:
        directives.update(USING_NAMESPACE.findall(blank_out(read(os.path.join(root, rel)))))
    users = {}
    for rel in sorted(paths):
        for header in included_headers(root, rel):
            users.setdefault(header, set()).add(rel)
    for header, includers in sorted(users.items()):
        if header not in header_cache:
            source = read(os.path.join(root, header))
            header_cache[header] = [split_key(k) + (alias,) for k, alias in declarations(source, named=True).items()]
        if not header_cache[header]:
            continue
        for rel in sorted(paths):
            if includers == {rel}:
                continue  # that one file would already fail on every platform
            for key, alias_a in anon_decls[rel].items():
                ns_a, name_a = split_key(key)
                for ns_h, name_h, alias_h in header_cache[header]:
                    if name_a != name_h or ns_h[:len(ns_a)] != ns_a:
                        continue
                    if alias_a is not None and alias_a == alias_h:
                        continue  # the same alias twice denotes one type: legal
                    joined = "::".join(ns_h)
                    if ns_h == ns_a or any(joined == t or joined.endswith("::" + t) for t in directives):
                        yield key, rel, header


def main():
    root = os.path.abspath(sys.argv[1])
    cache = {}
    header_cache = {}
    for group, paths in sorted(unity_groups(root).items()):
        by_key = {}
        for rel in sorted(paths):
            if rel not in cache:
                source = read(os.path.join(root, rel))
                cache[rel] = declarations(source) if re.search(r"namespace\s*\{", source) else {}
            for key, alias in cache[rel].items():
                by_key.setdefault(key, []).append((rel, alias))
        for key, entries in sorted(by_key.items()):
            owners = [rel for rel, _ in entries]
            for a in range(len(owners)):
                for b in range(a + 1, len(owners)):
                    # Two identical using-aliases are legal C++; anything else is a redefinition.
                    if entries[a][1] is not None and entries[a][1] == entries[b][1]:
                        continue
                    print("%s\t%s\t%s\t%s" % (key, owners[a], owners[b], group))
        for key, rel, header in sorted(set(header_clashes(root, sorted(paths), cache, header_cache))):
            print("%s\t%s\t%s\t%s" % (key, rel, header, group))


if __name__ == "__main__":
    main()
