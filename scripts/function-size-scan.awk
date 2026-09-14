# function-size-scan.awk -- the brace-depth C++ scanner behind scripts/check-function-sizes.sh.
#
# Split into its own file (rather than inlined like check-file-sizes.sh's much simpler awk block)
# because a real scanner needs helper functions and this reads far better as one; it is still pure
# awk, no compiler, no python -- the same "awk scanner, not clang-tidy" choice check-function-sizes.sh
# documents, just spread across two files for readability.
#
# INVOCATION: run with cwd == repo root, one call for every scanned file passed positionally, e.g.
#   (cd "$ROOT" && awk -f scripts/function-size-scan.awk -- Source/Foo.cpp Tests/FooTests.cpp)
# FILENAME is then exactly the git-relative path check-function-sizes.sh wants as the baseline key.
#
# OUTPUT: one TAB-separated line per function found (over cap or not -- cap filtering is the
# wrapper's job):
#   <size>\t<path>::<name>\t<start-line>
# TAB, not check-file-sizes.sh's plain space, because `name` can legitimately contain a space of
# its own (a TEST_F/TEST_P name is "TEST_F(Suite, Case)" -- see the spec example below); the
# wrapper converts back to a single space only when it writes the persisted baseline file. `name`
# is the qualified identifier immediately before the function's argument list (e.g.
# "MainComponent::performLocateMaster", "operator==", "TEST_F(Suite, Case)"); a name repeated
# within one file gets "#2", "#3" suffixes in order of appearance.
#
# METHOD: track brace depth. On every `{` not already inside a function, classify the text
# accumulated since the last `;`/`{`/`}` at this level ("the header") as a namespace/extern "C"
# (transparent -- content inside keeps being classified normally), a class/struct/union/enum not
# itself a function-like macro call (transparent, same reason), a TEST/TEST_F/TEST_P macro, a real
# function header (has a top-level `(...)` whose close is followed only by whitespace, `const`,
# `noexcept[(...)]`, `override`, `final`, `-> type`, or `: init-list`, any of those repeatable), or
# "other" (a brace initializer, a bare block, ...). Once inside a function every nested `{` --
# lambdas, if/for/while bodies, blocks -- counts toward that ONE enclosing function and is never
# reclassified, matching the spec: "lambdas and local blocks count toward the enclosing function".
#
# Before any of that, each line is cleaned: `//` line comments drop the rest of the line; `/* */`
# block comments (may span lines) are dropped entirely; preprocessor lines (`#...`, plus a `\`
# continuation chain) are dropped entirely, so a macro body's own braces/semicolons never leak into
# real code's brace tracking; string and char literals keep their text (needed for `extern "C"`,
# see below) but have `{`, `}`, `(`, `)`, `;` inside them blanked so quoted/JSON-ish content can
# never look like code structure -- this matters here because several Tests/*.cpp files embed
# multi-line `R"(...)"` JSON fixtures full of braces; the bare-delimiter raw-string form (`R"(...)"`)
# is tracked the same way, across lines. A `'` immediately after a digit (`1'000'000`) is a C++14
# digit separator, not a char-literal open, and is treated as a plain character.
#
# KNOWN LIMITATIONS (deliberate; not seen on this tree -- see check-function-sizes.test.sh):
#   - Raw strings with an explicit delimiter (`R"DELIM(...)DELIM"`) are not specially handled --
#     only the common delimiter-less `R"(...)"` form the spec calls out. None are used in this repo
#     (verified via `grep -RohE 'R"[A-Za-z_]*\(' Source Tests Tools`).
#   - Code inside an `#if 0` / disabled `#ifdef` block is still scanned as if compiled; preprocessor
#     LINES are dropped, but conditional exclusion is not modeled.
#   - A function-header multi-line split keeps its start line correctly, but a line that closes one
#     statement and opens another (e.g. `}; void x() {`) all on one line is still handled character
#     by character, so this is not a real limitation -- listed only because it's the one place a
#     naive scanner would normally trip.
#   - Explicit template specialization names (`template <> void Foo<int>::bar()`) extract as
#     "Foo<int>::bar" (angle brackets included) rather than being fully parsed; harmless for the
#     cap/baseline (used only as the display name), and no explicit specializations exist on this
#     tree to exercise it.

function trim(s) {
    sub(/^[ \t]+/, "", s)
    sub(/[ \t]+$/, "", s)
    return s
}

# find_close(s, openpos) -- position of the `)` matching the `(` at s[openpos], scanning only
# s (no cross-line state); 0 if s doesn't balance before it ends.
function find_close(s, openpos,    j, d, ch) {
    d = 0
    for (j = openpos; j <= length(s); j++) {
        ch = substr(s, j, 1)
        if (ch == "(") d++
        else if (ch == ")") {
            d--
            if (d == 0) return j
        }
    }
    return 0
}

# strip_kw(rem, kw) -- if rem starts with kw as a WHOLE word (next char, if any, is not an
# identifier char), returns the trimmed remainder and sets STRIP_OK=1; otherwise returns rem
# unchanged and leaves STRIP_OK as the caller set it (0). No \< \> in this awk's regex engine, so
# the word-boundary check is done by hand.
function strip_kw(rem, kw,    L, nextch) {
    L = length(kw)
    if (substr(rem, 1, L) != kw) return rem
    nextch = substr(rem, L + 1, 1)
    if (nextch != "" && nextch ~ /[A-Za-z0-9_]/) return rem
    STRIP_OK = 1
    return trim(substr(rem, L + 1))
}

# valid_trailer(rem) -- true when the text after a function header's argument-list close-paren is
# empty or some combination of const/noexcept[(...)]/override/final, optionally ending in a
# trailing return type (`-> ...`) or a constructor initializer list (`: ...`), either of which
# consumes everything after it (not further validated -- it can contain almost anything).
function valid_trailer(rem,    cp) {
    rem = trim(rem)
    while (rem != "") {
        if (substr(rem, 1, 2) == "->") return 1
        if (substr(rem, 1, 1) == ":") return 1
        STRIP_OK = 0
        rem = strip_kw(rem, "const")
        if (STRIP_OK) continue
        STRIP_OK = 0
        rem = strip_kw(rem, "override")
        if (STRIP_OK) continue
        STRIP_OK = 0
        rem = strip_kw(rem, "final")
        if (STRIP_OK) continue
        STRIP_OK = 0
        rem = strip_kw(rem, "noexcept")
        if (STRIP_OK) {
            if (substr(rem, 1, 1) == "(") {
                cp = find_close(rem, 1)
                if (cp == 0) return 0
                rem = trim(substr(rem, cp + 1))
            }
            continue
        }
        return 0
    }
    return 1
}

# extract_name(pre) -- the qualified identifier (or operator name) immediately before a function's
# `(`. `pre` has already had a lone "operator()" mangled to "operatorZZCALLZZ" by the caller, so the
# call operator's own empty parens can't be confused with the real argument list.
function extract_name(pre,    prefixpart, qualifier, tail) {
    pre = trim(pre)
    if (pre == "") return ""
    if (match(pre, /operator[ \t]*(==|!=|<=|>=|<<|>>|&&|\|\||\+\+|--|\+=|-=|\*=|\/=|%=|\^=|&=|\|=|->|\[\]|ZZCALLZZ|=|<|>|\+|-|\*|\/|%|\^|~|!)[ \t]*$/)) {
        tail = substr(pre, RSTART, RLENGTH)
        gsub(/[ \t]/, "", tail)
        sub(/ZZCALLZZ/, "()", tail)
        prefixpart = trim(substr(pre, 1, RSTART - 1))
        if (match(prefixpart, /[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*::$/)) {
            qualifier = substr(prefixpart, RSTART, RLENGTH)
        } else {
            qualifier = ""
        }
        return qualifier tail
    }
    if (match(pre, /[A-Za-z_~][A-Za-z0-9_:~<>]*$/)) {
        return substr(pre, RSTART, RLENGTH)
    }
    return ""
}

# classify(raw) -- sets CLASSIFY_KIND to "namespace" | "type" | "function" | "other", and
# CLASSIFY_NAME (function/TEST only) to the extracted name.
function classify(raw,    t, t2, kw, matchpos, closepos, pre, rem, nm, argpos, argclose, argtext) {
    t = trim(raw)
    CLASSIFY_KIND = "other"
    CLASSIFY_NAME = ""
    if (t == "") return

    if (t ~ /^namespace([ \t]|$)/ || t ~ /^extern[ \t]+"C"([ \t]|$)/ || t ~ /^extern[ \t]+"C\+\+"([ \t]|$)/) {
        CLASSIFY_KIND = "namespace"
        return
    }

    if ((t ~ /^(class|struct|union)([ \t]|$)/ || t ~ /^enum([ \t]|$)/) && index(t, "(") == 0) {
        CLASSIFY_KIND = "type"
        return
    }

    if (match(t, /^(TEST_F|TEST_P|TEST)[ \t]*\(/)) {
        kw = t
        sub(/[ \t]*\(.*$/, "", kw)
        argpos = index(t, "(")
        argclose = find_close(t, argpos)
        if (argclose == 0) return
        argtext = substr(t, argpos + 1, argclose - argpos - 1)
        gsub(/[ \t]+/, " ", argtext)
        gsub(/[ \t]*,[ \t]*/, ", ", argtext)
        argtext = trim(argtext)
        CLASSIFY_KIND = "function"
        CLASSIFY_NAME = kw "(" argtext ")"
        return
    }

    t2 = t
    gsub(/operator[ \t]*\(\)/, "operatorZZCALLZZ", t2)
    matchpos = index(t2, "(")
    if (matchpos == 0) return
    closepos = find_close(t2, matchpos)
    if (closepos == 0) return
    pre = trim(substr(t2, 1, matchpos - 1))
    rem = trim(substr(t2, closepos + 1))
    if (!valid_trailer(rem)) return
    nm = extract_name(pre)
    if (nm == "") return
    CLASSIFY_KIND = "function"
    CLASSIFY_NAME = nm
}

# neutralize_line(line) -- comments/preprocessor dropped, string/char/raw-string content kept but
# with {}();  blanked inside it. NSTATE (0 normal, 1 block comment, 2 raw string) and PREPROC_CONT
# persist across lines within one file (reset at FNR==1 below).
function neutralize_line(line,    n, chars, i, c, c2, out) {
    if (PREPROC_CONT) {
        PREPROC_CONT = (substr(line, length(line), 1) == "\\")
        return ""
    }
    if (NSTATE == 0 && match(line, /^[ \t]*#/)) {
        PREPROC_CONT = (substr(line, length(line), 1) == "\\")
        return ""
    }

    n = split(line, chars, "")
    out = ""
    i = 1
    while (i <= n) {
        c = chars[i]
        if (NSTATE == 1) {
            if (c == "*" && i < n && chars[i + 1] == "/") {
                NSTATE = 0
                out = out " "
                i += 2
                continue
            }
            i++
            continue
        }
        if (NSTATE == 2) {
            if (c == ")" && i < n && chars[i + 1] == "\"") {
                NSTATE = 0
                out = out ")\""
                i += 2
                continue
            }
            out = out (c ~ /[{}();]/ ? " " : c)
            i++
            continue
        }

        if (c == "/" && i < n && chars[i + 1] == "/") break
        if (c == "/" && i < n && chars[i + 1] == "*") {
            NSTATE = 1
            out = out " "
            i += 2
            continue
        }
        if (c == "R" && i + 2 <= n && chars[i + 1] == "\"" && chars[i + 2] == "(" \
            && (i == 1 || chars[i - 1] !~ /[A-Za-z0-9_]/)) {
            out = out "R\"("
            NSTATE = 2
            i += 3
            continue
        }
        if (c == "\"") {
            out = out c
            i++
            while (i <= n) {
                c2 = chars[i]
                if (c2 == "\\" && i < n) {
                    out = out c2 chars[i + 1]
                    i += 2
                    continue
                }
                if (c2 == "\"") {
                    out = out c2
                    i++
                    break
                }
                out = out (c2 ~ /[{}();]/ ? " " : c2)
                i++
            }
            continue
        }
        if (c == "'") {
            if (i > 1 && chars[i - 1] ~ /[0-9]/) {
                out = out c
                i++
                continue
            }
            out = out c
            i++
            while (i <= n) {
                c2 = chars[i]
                if (c2 == "\\" && i < n) {
                    out = out c2 chars[i + 1]
                    i += 2
                    continue
                }
                if (c2 == "'") {
                    out = out c2
                    i++
                    break
                }
                out = out (c2 ~ /[{}();]/ ? " " : c2)
                i++
            }
            continue
        }
        out = out c
        i++
    }
    return out
}

# emit_function(ln) -- called when the `}` closing the function at the current depth is seen.
function emit_function(ln,    nm) {
    nm = scope_name[depth]
    name_count[nm]++
    if (name_count[nm] > 1) nm = nm "#" name_count[nm]
    printf "%d\t%s::%s\t%d\n", ln - scope_start[depth] + 1, FILENAME, nm, scope_start[depth]
}

# scan_clean_line(clean, ln) -- the brace/paren-free structural pass: track depth, classify each
# `{` not already inside a function, emit on the `}` that closes a function.
function scan_clean_line(clean, ln,    n, chars, i, c) {
    n = split(clean, chars, "")
    for (i = 1; i <= n; i++) {
        c = chars[i]
        if (c == "{") {
            depth++
            if (in_function_depth != 0) {
                scope_kind[depth] = "nested"
            } else {
                classify(header_buf)
                if (CLASSIFY_KIND == "function") {
                    scope_kind[depth] = "function"
                    scope_name[depth] = CLASSIFY_NAME
                    scope_start[depth] = (header_start_line != 0 ? header_start_line : ln)
                    in_function_depth = depth
                } else {
                    scope_kind[depth] = CLASSIFY_KIND
                }
            }
            header_buf = ""
            header_start_line = 0
        } else if (c == "}") {
            if (depth == 0) continue   # stray close brace (malformed input) -- ignore, don't underflow
            if (scope_kind[depth] == "function" && depth == in_function_depth) {
                emit_function(ln)
                in_function_depth = 0
            }
            delete scope_kind[depth]
            delete scope_name[depth]
            delete scope_start[depth]
            depth--
            header_buf = ""
            header_start_line = 0
        } else if (c == ";") {
            header_buf = ""
            header_start_line = 0
        } else if (c == ":" && trim(header_buf) ~ /^(public|private|protected)$/) {
            # An access-specifier statement ("public:" etc.) is the one common `;`/`{`/`}`-free
            # class-body statement -- without this it glues onto the NEXT member's header text,
            # pulling that member's reported start line back to the specifier's own line. Every
            # OTHER use of `:` (inheritance -- "class Foo : public Bar {", a constructor
            # initializer list, a ternary) must NOT reset here, so this only fires when the buffer
            # is EXACTLY one of the three keywords -- never a prefix of something larger.
            header_buf = ""
            header_start_line = 0
        } else {
            header_buf = header_buf c
            if (header_start_line == 0 && c !~ /[ \t]/) header_start_line = ln
        }
    }
}

FNR == 1 {
    depth = 0
    in_function_depth = 0
    header_buf = ""
    header_start_line = 0
    NSTATE = 0
    PREPROC_CONT = 0
    delete scope_kind
    delete scope_name
    delete scope_start
    delete name_count
}

{
    scan_clean_line(neutralize_line($0), FNR)
    header_buf = header_buf " "   # line-boundary separator so a multi-line header never fuses tokens
}
