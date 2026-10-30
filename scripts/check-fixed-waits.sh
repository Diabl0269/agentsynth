#!/usr/bin/env bash
#
# check-fixed-waits.sh -- a test-QUALITY guard with a strict ratchet baseline, sibling to
# check-comment-provenance.sh / check-file-sizes.sh. A test that pumps the message loop for a FIXED
# number of milliseconds and then asserts is a race against the machine: it passes when the work
# finishes inside the pump and fails (or, worse, passes for the wrong reason) on a loaded runner.
# The reliable shape is to poll for the condition with a timeout: `waitUntil([&] { return done(); })`
# from Tests/TestWait.h.
#
# WHAT IS FLAGGED. In every git-tracked Tests/** C/C++/ObjC file, a call that pumps for a fixed
# time -- `runDispatchLoopUntil(<integer literal | k-constant | constant expression>)`, or a call to
# a file-local (or Tests/-header) one-line wrapper around it such as `pump()` / `settle()` -- whose
# following few lines (up to 8 non-blank code lines, within the same block) contain an
# EXPECT_*/ASSERT_* assertion. It is NOT flagged when the call sits inside a loop whose condition
# checks state (the poll pattern `for (int i = 0; i < 50 && !done; ++i) pump(20);`, a `while (!x)`
# or a `do { ... } while`), because that loop is the polling.
#
# ALSO FLAGGED: a short helper (a block of at most 4 lines) that calls keyPressed / triggerClick /
# invokeDirectly and ends with a fixed pump, e.g. `auto press = [&](auto& b) { b.triggerClick(); runDispatchLoopUntil(30); };`.
# Those calls dispatch asynchronously, so the pump is a bet on the machine; use flushMessageQueue() from
# Tests/TestWait.h, which runs exactly the messages already posted.
#
# This is a deliberate HEURISTIC scanner, not a C++ parser: comments, string literals and char
# literals are stripped, braces and parentheses are tracked, wrappers are recognised only in the
# one-line form `name(...) { ...runDispatchLoopUntil(N); }` (or a lambda of that shape, or the same
# body spread over three lines).
#
# BASELINE. A site that truly needs a fixed pump (a "nothing may happen within N ms" negative check,
# or delivering a posted click that has no observable state to poll) is listed in
# scripts/fixed-waits-baseline.txt, one line per site:
#     <path> | <the call's code line, trimmed> | <one-line reason>
# Keyed by path + code text (not line number) so unrelated edits do not churn it; N identical lines
# in a file need N entries. STRICT ratchet: a flagged site without an entry fails, an entry that
# matches nothing is stale and fails (--update drops them, keeping every reason), and --update never
# adds an entry -- a new site is fixed with waitUntil(), or its entry is written by hand with a reason.
#
# Usage:
#   bash scripts/check-fixed-waits.sh               # check the tree against the baseline
#   bash scripts/check-fixed-waits.sh --update      # drop stale baseline entries (never adds)
#   bash scripts/check-fixed-waits.sh --list        # every flagged site, ignoring the baseline
#   bash scripts/check-fixed-waits.sh --root <dir>  # scan a different repo root (tests)
#   bash scripts/check-fixed-waits.sh -h|--help
#
# Environment:
#   FIXED_WAITS_BASELINE  baseline file path (default: <root>/scripts/fixed-waits-baseline.txt)
#
# Exit status (check mode): 0 no violation, 1 a new site / stale entry.
#
# Portable: bash 3.2 (macOS) + POSIX awk (mawk, gawk and BSD awk -- no \b, no {n}, no gensub).

set -uo pipefail

# A git hook exports GIT_DIR and friends; strip them so every git call below does normal discovery.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-fixed-waits.sh [--update] [--list] [--root <dir>] [-h|--help]

Flags every test that pumps the message loop for a fixed time (runDispatchLoopUntil(<literal>) or a
one-line wrapper of it) and then asserts, instead of polling for the condition with waitUntil()
(Tests/TestWait.h). See docs/development/testing.md "Waiting for async work".

  (no flags)   Check the tree against scripts/fixed-waits-baseline.txt. Exit 1 on a new site or a
               stale baseline entry.
  --update     Drop stale baseline entries (keeps every remaining reason). Never adds an entry.
  --list       Print every flagged site as "path:line: code", ignoring the baseline.
  --root <dir> Repo root to scan (default: the git top level of this script). Lets tests point it at
               a throwaway fixture repo.
  -h, --help   Show this message.

Environment:
  FIXED_WAITS_BASELINE  Baseline file path (default: <root>/scripts/fixed-waits-baseline.txt).
USAGE
}

MODE="check"
ROOT_OVERRIDE=""
while [ $# -gt 0 ]; do
    case "$1" in
        --update) MODE="update"; shift ;;
        --list) MODE="list"; shift ;;
        --root)
            shift
            if [ $# -eq 0 ]; then
                echo "check-fixed-waits: --root requires an argument" >&2
                exit 1
            fi
            ROOT_OVERRIDE="$1"
            shift
            ;;
        -h | --help) usage; exit 0 ;;
        *)
            echo "check-fixed-waits: unknown argument '$1' (see --help)" >&2
            usage >&2
            exit 1
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -n "$ROOT_OVERRIDE" ]; then
    ROOT="$(cd "$ROOT_OVERRIDE" && pwd)"
else
    if ! ROOT="$(cd "$SCRIPT_DIR" && git rev-parse --show-toplevel)"; then
        echo "::error::check-fixed-waits: 'git rev-parse --show-toplevel' failed from $SCRIPT_DIR -- refusing to guess ROOT (pass --root explicitly to override)" >&2
        exit 1
    fi
fi
BASELINE_FILE="${FIXED_WAITS_BASELINE:-$ROOT/scripts/fixed-waits-baseline.txt}"
# Per-file results are cached by content (scripts/lib/guard-cache.sh); the baseline is applied fresh every run.
# shellcheck source=scripts/lib/guard-cache.sh
source "$SCRIPT_DIR/lib/guard-cache.sh"

list_scanned_paths() {
    git -C "$ROOT" ls-files | while IFS= read -r path; do
        case "$path" in
            Tests/*.h | Tests/*.hpp | Tests/*.cpp | Tests/*.mm | Tests/*.c) printf '%s\n' "$path" ;;
        esac
    done
}

# The awk scanner. Run with -v mode=defs over the headers to print "W <name>" per one-line wrapper,
# then with -v mode=scan -v gw=" name name " over every file to print "F<TAB>path<TAB>line<TAB>code".
SCAN_AWK='
function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); gsub(/[ \t]+/, " ", s); return s }

# Strip comments and the contents of string/char literals from one line (block-comment state in
# inblk). A quote glued to an alphanumeric is a digit separator, not a char literal.
function clean(line,   n, i, c, d, out, j) {
    n = length(line); out = ""; i = 1
    while (i <= n) {
        if (inblk) {
            j = index(substr(line, i), "*/")
            if (j == 0) { i = n + 1 } else { i = i + j + 1; inblk = 0 }
            continue
        }
        c = substr(line, i, 1)
        if (c == "\"" || c == "\047") {
            if (c == "\047" && i > 1 && substr(line, i - 1, 1) ~ /[A-Za-z0-9_]/) { out = out c; i++; continue }
            out = out c; i++
            while (i <= n) {
                d = substr(line, i, 1)
                if (d == "\\") i += 2
                else if (d == c) { out = out c; i++; break }
                else i++
            }
            continue
        }
        if (c == "/") {
            d = substr(line, i + 1, 1)
            if (d == "/") break
            if (d == "*") { inblk = 1; i += 2; continue }
        }
        out = out c; i++
    }
    return out
}

# Text between an already-consumed "(" at position start and its matching ")" (or end of string).
function grabparen(s, start,   depth, i, c, n) {
    depth = 1; n = length(s)
    for (i = start; i <= n; i++) {
        c = substr(s, i, 1)
        if (c == "(") depth++
        else if (c == ")") { depth--; if (depth == 0) return substr(s, start, i - start) }
    }
    return substr(s, start)
}

# 1 when the argument is a fixed duration: digits, k-constants, getDoubleClickTimeout(), + and parens.
function fixed_arg(a,   id, ok, rest, q) {
    a = trim(a)
    if (a == "") return 1
    rest = a
    ok = 1
    while (match(rest, /[A-Za-z_][A-Za-z0-9_:]*/)) {
        id = substr(rest, RSTART, RLENGTH)
        rest = substr(rest, RSTART + RLENGTH)
        q = id
        while ((i2 = index(q, "::")) > 0) q = substr(q, i2 + 2)
        if (q ~ /^k[A-Z]/ || q == "getDoubleClickTimeout") continue
        ok = 0
    }
    return ok
}

# 1 when the header text is a for/while loop whose condition checks state, or a do-loop.
function stateful_loop(h,   cond, start, seg, k) {
    if (h ~ /(^|[^A-Za-z0-9_])do$/ || h == "do") return 1
    if (match(h, /(^|[^A-Za-z0-9_])while[ ]*\(/)) {
        start = RSTART + RLENGTH
        cond = grabparen(h, start)
        return (cond ~ /!|&&|\|\||\.|->|\(/) ? 1 : 0
    }
    if (match(h, /(^|[^A-Za-z0-9_])for[ ]*\(/)) {
        start = RSTART + RLENGTH
        cond = grabparen(h, start)
        k = split(cond, seg, ";")
        if (k < 3) return 0
        return (seg[2] ~ /!|&&|\|\||\.|->|\(/) ? 1 : 0
    }
    return 0
}

# 1 when the text calls something that dispatches asynchronously (a key press, a click, a command).
function async_dispatch(t) { return (t ~ /(keyPressed|triggerClick|invokeDirectly)[ ]*\(/) ? 1 : 0 }

function emit_helper(i) { printf "F\t%s\t%d\t%s\n", cur, i, trim(code[i]); seenline[i] = 1 }

# A short helper (a block of at most 4 lines) that dispatches asynchronously and ends with a fixed pump: there is no
# assertion after the pump here, but every caller is racing the same fixed time.
function find_dispatch_helpers(   i, pos, rest, arg, nb, k, t, seen_open, tail) {
    for (i = 1; i <= nl; i++) {
        pos = index(code[i], "runDispatchLoopUntil(")
        if (!pos || seenline[i]) continue
        rest = substr(code[i], pos + length("runDispatchLoopUntil("))
        arg = grabparen(rest, 1)
        if (!fixed_arg(arg)) continue
        tail = substr(rest, length(arg) + 2)
        # The pump must be the last statement of its block: a closing brace follows on this line or the next one.
        if (tail !~ /^[ ]*;[ ]*\}/) {
            nb = i + 1
            while (nb <= nl && trim(code[nb]) == "") nb++
            if (nb > nl || trim(code[nb]) !~ /^\}[;), ]*$/ || tail !~ /^[ ]*;[ ]*$/) continue
        }
        t = substr(code[i], 1, pos - 1)
        if (t ~ /\{/) { sub(/^[^{]*\{/, "", t); if (async_dispatch(t)) emit_helper(i); continue }
        if (async_dispatch(t)) { emit_helper(i); continue }
        k = i - 1; seen_open = 0
        while (k >= 1 && i - k <= 4 && !seen_open) {
            t = code[k]
            if (trim(t) != "") {
                if (t ~ /\{/) { seen_open = 1; sub(/^[^{]*\{/, "", t) }
                if (async_dispatch(t)) { emit_helper(i); break }
                if (t ~ /\}/) break
            }
            k--
        }
    }
}

# Name of the function/lambda declared by a wrapper header ("void pump()", "auto pump = [] ").
function wrapper_name(h,   depth, i, c, e, nm, pre) {
    sub(/[ ]*(const|override|noexcept|mutable)[ ]*$/, "", h)
    sub(/[ ]+$/, "", h)
    if (match(h, /[A-Za-z_][A-Za-z0-9_]*[ ]*=[ ]*\[[^]]*\]([ ]*\([^)]*\))?$/)) {
        nm = substr(h, RSTART, RLENGTH)
        sub(/[ ]*=.*/, "", nm)
        return nm
    }
    if (substr(h, length(h), 1) != ")") return ""
    depth = 0
    for (i = length(h); i >= 1; i--) {
        c = substr(h, i, 1)
        if (c == ")") depth++
        else if (c == "(") { depth--; if (depth == 0) break }
    }
    if (i < 1) return ""
    pre = substr(h, 1, i - 1)
    sub(/[ ]+$/, "", pre)
    if (match(pre, /[A-Za-z_][A-Za-z0-9_]*$/)) return substr(pre, RSTART, RLENGTH)
    return ""
}

function find_wrappers(   i, pos, prefix, rest, close_at, hdr, name, p, nb) {
    for (i = 1; i <= nl; i++) {
        pos = index(code[i], "runDispatchLoopUntil(")
        if (!pos) continue
        prefix = substr(code[i], 1, pos - 1)
        rest = substr(code[i], pos + length("runDispatchLoopUntil("))
        rest = substr(rest, length(grabparen(rest, 1)) + 2)
        hdr = ""
        if (prefix ~ /\{/ && rest ~ /^[ ]*;[ ]*\}/) {
            hdr = prefix
            sub(/\{[^{]*$/, "", hdr)
            name = wrapper_name(hdr)
            if (name != "") { wrapper[name] = 1; defline[i] = 1 }
        } else if (prefix ~ /^[ \t]*[A-Za-z_:.>()-]*[ ]*$/ && trim(prefix) ~ /^(juce::MessageManager::getInstance\(\)->|mm->)?$/ && rest ~ /^[ ]*;[ ]*$/) {
            p = i - 1
            while (p >= 1 && trim(code[p]) == "") p--
            nb = i + 1
            while (nb <= nl && trim(code[nb]) == "") nb++
            if (p >= 1 && trim(code[p]) ~ /\{$/ && nb <= nl && trim(code[nb]) ~ /^\};?$/) {
                hdr = trim(code[p])
                sub(/\{$/, "", hdr)
                name = wrapper_name(hdr)
                if (name != "") { wrapper[name] = 1; defline[p] = 1 }
            }
        }
    }
}

function is_wrapper(id) { return (id in wrapper) || (id in hw) }

function remove_closed(   w) {
    for (w = 1; w <= wn; w++) if (wact[w] && wdepth[w] > sp) wact[w] = 0
}

function flag(w) {
    wact[w] = 0
    seenline[wline[w]] = 1
    printf "F\t%s\t%d\t%s\n", cur, wline[w], wtext[w]
}

function base(path,   p) { p = path; while ((k0 = index(p, "/")) > 0) p = substr(p, k0 + 1); return p }

function process_file(   i, L, n, j, c, id, k, k2, arg, w, tail, hasassert, nonblank, a, poll, s, isdecl) {
    if (cur == "") return
    if (mode == "defs") {
        inblk = 0
        for (i = 1; i <= nl; i++) code[i] = clean(raw[i])
        find_wrappers()
        for (id in wrapper) print "W " base(cur) "|" id
        return
    }
    inblk = 0
    delete wrapper; delete defline; delete hw; delete inc
    for (i = 1; i <= nl; i++) if (match(raw[i], /^[ \t]*#[ \t]*include[ \t]*"[^"]*"/)) {
        s = substr(raw[i], RSTART, RLENGTH); sub(/^[^"]*"/, "", s); sub(/"$/, "", s); inc[base(s)] = 1
    }
    for (k = 1; k <= ngw; k++) if (gwb[k] in inc) hw[gwn[k]] = 1
    for (i = 1; i <= nl; i++) code[i] = clean(raw[i])
    find_wrappers()
    sp = 0; hdr = ""; pdepth = 0; wn = 0
    delete wact; delete stk; delete seenline
    for (i = 1; i <= nl; i++) {
        L = code[i]
        nonblank = (trim(L) != "")
        if (nonblank) {
            for (w = 1; w <= wn; w++) if (wact[w]) { wleft[w]--; if (wleft[w] < 0) wact[w] = 0 }
        }
        if (L ~ /(^|[^A-Za-z0-9_])(EXPECT|ASSERT)_[A-Z0-9_]+[ ]*\(/) {
            for (w = 1; w <= wn; w++) if (wact[w]) flag(w)
        }
        n = length(L); j = 1
        while (j <= n) {
            c = substr(L, j, 1)
            if (c == "(") { pdepth++; hdr = hdr c; j++ }
            else if (c == ")") { pdepth--; hdr = hdr c; j++ }
            else if (c == "{") {
                sp++; stk[sp] = stateful_loop(trim(hdr)); hdr = ""; j++
            }
            else if (c == "}") {
                if (sp > 0) sp--
                hdr = ""; remove_closed(); j++
            }
            else if (c == ";" && pdepth <= 0) { hdr = ""; pdepth = 0; j++ }
            else if (c ~ /[A-Za-z_]/) {
                match(substr(L, j), /^[A-Za-z_][A-Za-z0-9_]*/)
                id = substr(L, j, RLENGTH)
                j += RLENGTH
                hdr = hdr id
                k = j
                while (k <= n && substr(L, k, 1) == " ") k++
                if (substr(L, k, 1) == "(" && (id == "runDispatchLoopUntil" || (is_wrapper(id) && !defline[i]))) {
                    arg = grabparen(L, k + 1)
                    if (fixed_arg(arg)) {
                        poll = stateful_loop(trim(hdr))
                        for (s = 1; s <= sp; s++) if (stk[s]) poll = 1
                        if (!poll) {
                            wn++
                            wact[wn] = 1; wline[wn] = i; wtext[wn] = trim(L)
                            wdepth[wn] = sp; wleft[wn] = 8
                            wpos[wn] = k + length(arg) + 1
                            wnew[wn] = i
                        }
                    }
                }
            }
            else { hdr = hdr c; j++ }
        }
        # An assertion later on the same line as a call it follows.
        for (w = 1; w <= wn; w++) {
            if (wact[w] && wnew[w] == i) {
                tail = substr(L, wpos[w])
                if (tail ~ /(^|[^A-Za-z0-9_])(EXPECT|ASSERT)_[A-Z0-9_]+[ ]*\(/) flag(w)
            }
        }
    }
    find_dispatch_helpers()
}

BEGIN { ngw = split(gw, gwe, " "); for (k = 1; k <= ngw; k++) { split(gwe[k], gwp, "|"); gwb[k] = gwp[1]; gwn[k] = gwp[2] } }
FNR == 1 {
    process_file()
    cur = FILENAME; nl = 0
}
{ nl++; raw[nl] = $0 }
END { process_file() }
'

# scan_paths -- read repo-relative paths on stdin and print the scanner's F lines for the ones that
# exist as files. Wrappers defined in Tests/ headers apply everywhere. A file's lines depend on its own
# text and on the wrapper names every header defines, so the cache key carries those names.
scan_paths() {
    local paths headers names="" gw list
    paths="$(while IFS= read -r path; do [ -f "$ROOT/$path" ] && printf '%s\n' "$path"; done)"
    [ -n "$paths" ] || return 0
    gw=" "
    headers="$(printf '%s\n' "$paths" | grep -E '\.(h|hpp)$' || true)"
    if [ -n "$headers" ]; then
        names="$(cd "$ROOT" && printf '%s\n' "$headers" | xargs awk -v mode=defs "$SCAN_AWK" | sed 's/^W //' | sort -u | tr '\n' ' ')"
    fi
    [ -n "$names" ] && gw=" $names"
    list="$(mktemp)"
    printf '%s\n' "$paths" >"$list"
    SCAN_GW="$gw"
    gc_init fixed-waits "$gw" "${BASH_SOURCE[0]}"
    gc_scan_files 'split($0, a, "\t"); p = a[2]' "$list" scan_files_with_wrappers
    rm -f "$list"
}

# scan_files_with_wrappers -- scans the repo-relative paths on stdin, with the wrapper names in SCAN_GW.
scan_files_with_wrappers() {
    (cd "$ROOT" && xargs awk -v mode=scan -v "gw=$SCAN_GW" "$SCAN_AWK")
}

# flagged_sites -- "path<TAB>line<TAB>code" for every flagged site, sorted by path then line.
flagged_sites() {
    list_scanned_paths | scan_paths | grep '^F' | cut -f2- | sort -t "$(printf '\t')" -k1,1 -k2,2n
}

baseline_data() {
    [ -f "$BASELINE_FILE" ] || return 0
    grep -v '^#' "$BASELINE_FILE" | grep -v '^[[:space:]]*$'
}

FIX_HINT="wait for the condition instead of a fixed time: waitUntil([&] { return <condition>; }) from Tests/TestWait.h -- see docs/development/testing.md \"Waiting for async work\""

run_list() {
    flagged_sites | awk -F '\t' '{ printf "%s:%s: %s\n", $1, $2, $3 }'
}

run_check() {
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT
    flagged_sites >"$workdir/flagged.tsv"
    # baseline -> "path<TAB>code" per entry (reason dropped).
    baseline_data | awk '{
        i = index($0, " | "); path = substr($0, 1, i - 1); rest = substr($0, i + 3)
        j = 0; for (k = length(rest) - 2; k >= 1; k--) if (substr(rest, k, 3) == " | ") { j = k; break }
        if (j == 0) { printf "BAD\t%s\n", $0; next }
        printf "%s\t%s\n", path, substr(rest, 1, j - 1)
    }' >"$workdir/baseline.tsv"

    out="$(awk -F '\t' -v hint="$FIX_HINT" '
        FILENAME == bfile {
            if ($1 == "BAD") { printf "::error::malformed baseline line (want \"<path> | <code> | <reason>\"): %s\n", $2; errors++; next }
            allowed[$1 SUBSEP $2]++; next
        }
        {
            key = $1 SUBSEP $3
            seen[key]++
            total++
            if (seen[key] > allowed[key]) {
                printf "::error file=%s,line=%s::%s:%s: fixed-time wait followed by an assertion: %s -- %s\n", $1, $2, $1, $2, $3, hint
                errors++
            } else baselined++
        }
        END {
            for (key in allowed) if (seen[key] + 0 < allowed[key]) {
                split(key, kp, SUBSEP)
                printf "::error::stale baseline entry (%d listed, %d found): %s | %s -- run --update\n", allowed[key], seen[key] + 0, kp[1], kp[2]
                errors++
            }
            printf "SUMMARY %d %d\n", total + 0, baselined + 0
            exit (errors > 0 ? 1 : 0)
        }' bfile="$workdir/baseline.tsv" "$workdir/baseline.tsv" "$workdir/flagged.tsv")"
    status=$?
    summary=""
    while IFS= read -r line; do
        case "$line" in
            "SUMMARY "*) summary="$line" ;;
            *) [ -n "$line" ] && echo "$line" ;;
        esac
    done <<<"$out"
    total=0
    baselined=0
    [ -n "$summary" ] && read -r _ total baselined <<<"$summary"
    echo "check-fixed-waits: $total fixed-wait site(s) flagged, $baselined baselined"
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
        {
            echo "### Fixed-wait guard"
            echo "- flagged sites: $total ($baselined baselined)"
        } >>"$GITHUB_STEP_SUMMARY"
    fi
    return "$status"
}

run_update() {
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT
    flagged_sites >"$workdir/flagged.tsv"
    [ -f "$BASELINE_FILE" ] || { echo "check-fixed-waits --update: no baseline file at $BASELINE_FILE"; return 0; }
    # Keep every comment line and every entry that still has a matching site (up to the found count).
    awk -F '\t' -v flagged="$workdir/flagged.tsv" '
        BEGIN {
            while ((getline line < flagged) > 0) { split(line, f, "\t"); seen[f[1] SUBSEP f[3]]++ }
        }
        /^#/ || /^[ \t]*$/ { print; next }
        {
            i = index($0, " | "); path = substr($0, 1, i - 1); rest = substr($0, i + 3)
            j = 0; for (k = length(rest) - 2; k >= 1; k--) if (substr(rest, k, 3) == " | ") { j = k; break }
            key = path SUBSEP substr(rest, 1, j - 1)
            if (j > 0 && used[key] < seen[key]) { used[key]++; print; next }
            print "dropped: " $0 > "/dev/stderr"
        }' "$BASELINE_FILE" >"$workdir/new.txt"
    if cmp -s "$workdir/new.txt" "$BASELINE_FILE"; then
        echo "check-fixed-waits --update: baseline unchanged."
    else
        cp "$workdir/new.txt" "$BASELINE_FILE"
        echo "check-fixed-waits --update: stale entries dropped."
    fi
}

case "$MODE" in
    check) run_check ;;
    update) run_update ;;
    list) run_list ;;
esac
