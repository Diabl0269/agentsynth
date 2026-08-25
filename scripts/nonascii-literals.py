#!/usr/bin/env python3
"""Reports non-ASCII bytes that sit INSIDE a C++ double-quoted string literal.

Why this exists: juce::String's `const char*` constructor decodes its input as the system
encoding (Latin-1 in practice), NOT as UTF-8. So a literal carrying raw UTF-8 bytes — whether
typed directly ("Rename…") or spelled with hex escapes ("Rename\\xe2\\x80\\xa6") — reaches the UI
as one mojibake character per byte: "Renameâ€¦". The fix is either plain ASCII or an explicit
juce::CharPointer_UTF8 wrap, which tells JUCE how to decode the bytes.

Comments are exempt (this codebase writes prose em dashes throughout them, and they never reach
juce::String), and so is any line that already mentions CharPointer_UTF8.

Usage: nonascii-literals.py <root> [<root> ...]
Prints one `path:line: text` per offending line; exit status 1 when any were found.
"""

import os
import sys

SUFFIXES = (".cpp", ".h", ".mm")


def scan_line(raw, in_block):
    """Returns (found_non_ascii_in_string, in_block_after). `raw` is a bytes line."""
    i = 0
    in_str = False
    in_chr = False
    esc = False
    found = False
    while i < len(raw):
        byte = raw[i : i + 1]
        if in_block:
            if byte == b"*" and raw[i + 1 : i + 2] == b"/":
                in_block = False
                i += 2
                continue
            i += 1
            continue
        if in_str:
            if esc:
                esc = False
            elif byte == b"\\":
                # An ESCAPED high byte is the same bug spelled differently, and it is the spelling
                # that actually shipped: "Rename\xe2\x80\xa6" is three raw UTF-8 bytes by the time
                # juce::String sees it, and decodes to "Renameâ€¦" exactly like a typed "…" would.
                # A scanner that only looked for literal high bytes would have missed it.
                nxt = raw[i + 1 : i + 2]
                if nxt in (b"x", b"X", b"u", b"U"):
                    j = i + 2
                    digits = b""
                    while j < len(raw) and raw[j : j + 1] in b"0123456789abcdefABCDEF":
                        digits += raw[j : j + 1]
                        j += 1
                    if digits and int(digits, 16) > 0x7F:
                        found = True
                    i = max(j, i + 2)
                    continue
                esc = True
            elif byte == b'"':
                in_str = False
            elif raw[i] > 0x7F:
                found = True
            i += 1
            continue
        if in_chr:
            if esc:
                esc = False
            elif byte == b"\\":
                esc = True
            elif byte == b"'":
                in_chr = False
            i += 1
            continue
        if byte == b"/" and raw[i + 1 : i + 2] == b"/":
            break  # line comment: nothing after this can be a literal
        if byte == b"/" and raw[i + 1 : i + 2] == b"*":
            in_block = True
            i += 2
            continue
        if byte == b'"':
            in_str = True
            i += 1
            continue
        if byte == b"'":
            in_chr = True
            i += 1
            continue
        i += 1
    return found, in_block


def scan_file(path):
    hits = []
    with open(path, "rb") as handle:
        lines = handle.read().split(b"\n")
    in_block = False
    for number, raw in enumerate(lines, 1):
        # Exempt: a line that already declares its encoding — juce::CharPointer_UTF8 or the
        # equivalent juce::String::fromUTF8, both of which tell JUCE the bytes are UTF-8 — and a raw
        # string literal (R"(...)" has its own quoting rules this scanner does not model).
        #
        # static_assert messages are deliberately NOT exempt even though the compiler, not
        # juce::String, prints them. The rule is worth more than the exception: "no raw non-ASCII
        # byte in any string literal" is one sentence a reviewer can hold, and an em dash in a
        # compile-time diagnostic buys nothing that "-" does not.
        exempt = b"CharPointer_UTF8" in raw or b"fromUTF8" in raw or b'R"' in raw
        found, in_block = scan_line(raw, in_block)
        if found and not exempt:
            hits.append((number, raw.decode("utf-8", "replace").strip()))
    return hits


def report(path):
    count = 0
    for number, text in scan_file(path):
        print(f"{path}:{number}: {text[:160]}")
        count += 1
    return count


def main(roots):
    total = 0
    for root in roots:
        # A single file is a legal root (the unit tests point this at one fixture at a time);
        # os.walk yields nothing for one, so it is handled explicitly.
        if os.path.isfile(root):
            total += report(root)
            continue
        for dirpath, _dirs, files in os.walk(root):
            for name in sorted(files):
                if name.endswith(SUFFIXES):
                    total += report(os.path.join(dirpath, name))
    return 1 if total else 0


if __name__ == "__main__":
    # No argument: scan the repo's own source tree, resolved from this script's location so the
    # caller's working directory does not matter.
    default_root = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Source")
    sys.exit(main(sys.argv[1:] or [default_root]))
