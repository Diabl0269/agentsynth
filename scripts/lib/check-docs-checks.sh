#!/usr/bin/env bash
#
# scripts/lib/check-docs-checks.sh -- sourced by scripts/check-docs.sh, not executed directly.
#
# Holds the content-correctness check implementations (B through G) and the awk helpers they
# share (build_slugs_file/slug_exists/build_headings_file for heading and slug tables, awk_scan
# for the generic per-line regex extractor). check-docs.sh itself grew past this repo's 1,000-line
# cap once FRO217 added check G -- rather than raise the cap or leave the file over it, this is the
# per-concern split root CLAUDE.md's "Code structure" section calls for: check-docs.sh keeps the
# CLI (usage/argument parsing), the file-tree scan (list_scanned_paths/build_pairs_file), the
# naming ratchet (check A) and its baseline I/O, and the three run modes (check/update/list); this
# file keeps every check that validates docs CONTENT rather than the naming convention. See
# check-docs.sh's own header comment for the full mechanism and the meaning of each check --
# comments here describe only how each one is implemented, not why the guard exists at all.
#
# Same portability rules as check-docs.sh: bash 3.2 + grep/sed/awk/find only, no python, no
# GNU-only flags, no lazy quantifiers, every bash array access guarded by an explicit length check.
# --- shared awk helpers -------------------------------------------------------------------------

# CHECK_DOCS_AWK_LIB -- awk function definitions prepended to the program of every check that needs
# them (`awk ... "$CHECK_DOCS_AWK_LIB"'BEGIN { ... }'`), so each rule exists exactly ONCE instead of
# being re-typed inside every check's own awk program:
#   docs_boundary_ok(line, abs_idx) -- may a `docs/` at 1-based position abs_idx of <line> start OUR
#     docs path? The character before it must be neither alphanumeric nor `/` (so a sibling-repo
#     path like `synth-platform/docs/<x>.md` is never read as ours) -- with ONE exception: a `/` that
#     directly follows a source/doc FILENAME (a component ending `.h`, `.cpp`, `.md`, ... e.g. the
#     typo `ADSRModule.h/docs/modules/modules.md`). No sibling repo directory name ends in a file
#     extension, so the exception cannot re-admit one, and the rule needs no list of repo names.
#   load_headings(file) -- fills the global `headings` (relpath -> SOH-joined section numbers) from
#     a table built by build_headings_file.
#   section_in_doc(doc, secnum) -- the ONE section-resolution rule: true when a heading number equals
#     secnum or extends it (`## 5.3 Foo` satisfies both `5.3` and the coarser `5`). Used by check D,
#     H and G, so they can never disagree about what sections a doc has.
# No apostrophes anywhere in here: the text is spliced between shell single quotes.
CHECK_DOCS_AWK_LIB='
function docs_boundary_ok(line, abs_idx,   prevchar, j, comp) {
    if (abs_idx <= 1) return 1
    prevchar = substr(line, abs_idx - 1, 1)
    if (prevchar !~ /[A-Za-z0-9\/]/) return 1
    if (prevchar != "/") return 0
    comp = ""
    j = abs_idx - 2
    while (j >= 1 && substr(line, j, 1) ~ /[A-Za-z0-9_.-]/) { comp = substr(line, j, 1) comp; j-- }
    if (comp ~ /\.(h|hpp|cpp|md|sh|yml|json|txt|py|cmake)$/) return 1
    return 0
}
function load_headings(hfile,   hline, hcols) {
    while ((getline hline < hfile) > 0) {
        split(hline, hcols, "\t")
        headings[hcols[1]] = (hcols[1] in headings) ? headings[hcols[1]] "\x01" hcols[2] : hcols[2]
    }
    close(hfile)
}
function section_in_doc(doc, secnum,   ns, nums, qq, i) {
    if (!(doc in headings)) return 0
    ns = split(headings[doc], nums, "\x01")
    qq = secnum "."
    for (i = 1; i <= ns; i++) {
        if (nums[i] == secnum || index(nums[i], qq) == 1) return 1
    }
    return 0
}
'

# --- heading helpers --------------------------------------------------------------------------

# build_slugs_file <pairs-file (md-only)> <out-file> -- "relpath\tslug" (GitHub-style: lowercase,
# strip everything not alnum/space/hyphen, spaces->hyphens) for every heading line in every *.md
# file in <pairs-file>. One awk process for every file's headings, computed ONCE regardless of how
# many links reference them -- check_b_violations used to call a 5-process grep|sed|tr|sed|tr
# pipeline PER anchored link (this repo has ~95 of them), which dominated this script's runtime
# under this environment's per-process overhead; a single upfront table plus a plain lookup fixed
# it. Same getline-per-file technique as awk_scan/build_headings_file.
build_slugs_file() {
    local pairs="$1" out="$2"
    awk -v filelist="$pairs" '
        BEGIN {
            while ((getline pairline < filelist) > 0) {
                n = split(pairline, cols, "\t")
                if (n < 2) continue
                relpath = cols[1]
                abspath = cols[2]
                while ((getline line < abspath) > 0) {
                    if (line !~ /^#{1,6}[ \t]/) continue
                    text = line
                    sub(/^#{1,6}[ \t]+/, "", text)
                    text = tolower(text)
                    gsub(/[^a-z0-9 \t-]/, "", text)
                    gsub(/[ \t]/, "-", text)
                    print relpath "\t" text
                }
                close(abspath)
            }
            close(filelist)
            exit
        }
    ' >"$out"
}

# slug_exists <relpath> <anchor> <slugs-file> -- true if <relpath> has a heading whose slug is
# exactly <anchor>.
slug_exists() {
    local doc="$1" anchor="$2" slugs_file="$3"
    awk -F'\t' -v d="$doc" -v a="$anchor" '$1 == d && $2 == a { found = 1 } END { exit !found }' "$slugs_file"
}

# build_headings_file <pairs-file (all)> <out-file> -- "relpath\tnum" for every h2-h6 heading
# NUMBER (`## 5.3 Foo` -> `5.3`) in every docs/**/*.md file, one line per heading. Same
# getline-per-file technique as awk_scan/build_slugs_file above -- one awk process, computed ONCE
# and shared by every consumer instead of re-scanning every doc's headings per caller. Check D used
# to build this table inline inside its own single awk invocation; FRO217 pulled it out into this
# shared function so check G (bare basename references, which also need to resolve a trailing `§N`
# against a doc's real section numbers) can use the EXACT SAME table rather than a second
# implementation -- check D and check G can therefore never disagree about what section numbers a
# doc actually has, the same guarantee build_slugs_file already gives check B and check F for slugs.
build_headings_file() {
    local pairs="$1" out="$2"
    awk -v filelist="$pairs" '
        BEGIN {
            while ((getline pairline < filelist) > 0) {
                n = split(pairline, cols, "\t")
                if (n < 2) continue
                relpath = cols[1]
                if (relpath !~ /^docs\/.*\.md$/) continue
                abspath = cols[2]
                while ((getline line < abspath) > 0) {
                    if (line !~ /^#{2,6}[ \t]/) continue
                    rest = line
                    sub(/^#{2,6}[ \t]+/, "", rest)
                    if (match(rest, /^[0-9]+(\.[0-9]+)*/)) {
                        num = substr(rest, RSTART, RLENGTH)
                        after = substr(rest, RLENGTH + 1)
                        if (after == "" || after ~ /^\.?[ \t]/ || after ~ /^\.$/) {
                            print relpath "\t" num
                        }
                    }
                }
                close(abspath)
            }
            close(filelist)
            exit
        }
    ' >"$out"
}

awk_scan() {
    local pairs="$1" pattern="$2"
    # awk applies escape-sequence processing to a `-v var=value` assignment (POSIX: same rules as
    # a string constant in the program text) -- an UNESCAPED `\]`/`\(`/`\.` etc in <pattern> can
    # silently lose its backslash (implementation-defined for an unrecognized escape), turning
    # `\]\([^)]*\)` into the unanchored `][^)]*` and matching from the wrong `]` entirely. Doubling
    # every backslash here survives that processing and restores the caller's literal pattern.
    local escaped_pattern="${pattern//\\/\\\\}"
    awk -v filelist="$pairs" -v pat="$escaped_pattern" '
        BEGIN {
            while ((getline pairline < filelist) > 0) {
                n = split(pairline, cols, "\t")
                if (n < 2) continue
                relpath = cols[1]
                abspath = cols[2]
                fnr = 0
                while ((getline line < abspath) > 0) {
                    fnr++
                    remaining = line
                    while ((idx = match(remaining, pat)) > 0) {
                        text = substr(remaining, idx, RLENGTH)
                        print relpath "\t" fnr "\t" text
                        if (RLENGTH == 0) { remaining = substr(remaining, idx + 1) } else { remaining = substr(remaining, idx + RLENGTH) }
                    }
                }
                close(abspath)
            }
            close(filelist)
            exit
        }
    '
}

# --- check B: markdown link targets ------------------------------------------------------------

# check_b_violations <pairs-file (md-only)> <slugs-file (from build_slugs_file)> --
# "relpath:line: message" per broken link. <slugs-file> is built once by the caller (run_check /
# run_list) and shared with check_f_violations, so B and F can never disagree about what counts as
# a valid slug -- there is exactly one slug implementation in this file.
check_b_violations() {
    local md_pairs="$1" slugs_file="$2"
    awk_scan "$md_pairs" '\]\([^)]*\)' | while IFS="$(printf '\t')" read -r path line rest; do
        [ -n "$path" ] || continue
        # Pure bash `dirname` equivalent -- the real /usr/bin/dirname is an external process, and
        # this loop runs it once per markdown link (500+ across the repo); at this environment's
        # per-process overhead that alone was a meaningful chunk of check B's runtime.
        dir="${path%/*}"
        [ "$dir" = "$path" ] && dir="."
        target="${rest#](}"
        target="${target%)}"
        case "$target" in
            http:* | https:* | mailto:* | '#'*) continue ;;
        esac
        case "$target" in
            *.md | *.md'#'*) ;;
            *) continue ;;
        esac
        anchor=""
        tpath="$target"
        case "$target" in
            *'#'*)
                tpath="${target%%#*}"
                anchor="${target#*#}"
                ;;
        esac
        if [ "$dir" = "." ]; then
            resolved="$(normalize_rel_path "$tpath")"
        else
            resolved="$(normalize_rel_path "$dir/$tpath")"
        fi
        # A link that still starts with ".." after normalization points above this repo's root
        # (e.g. the workspace-root map link) -- out of scope for a repo-scoped checker; see the
        # header comment.
        case "$resolved" in
            '..' | '../'*) continue ;;
        esac
        if [ ! -f "$ROOT/$resolved" ]; then
            printf '%s:%s: link target '\''%s'\'' resolves to '\''%s'\'', which does not exist\n' "$path" "$line" "$target" "$resolved"
            continue
        fi
        if [ -n "$anchor" ]; then
            if ! slug_exists "$resolved" "$anchor" "$slugs_file"; then
                printf '%s:%s: link target '\''%s'\'' has no heading matching anchor '\''#%s'\''\n' "$path" "$line" "$target" "$anchor"
            fi
        fi
    done
}

# --- check C: docs/... path mentions -----------------------------------------------------------

# awk_scan_docs_path <pairs-file> -- "relpath\tfnr\tmatch" for every `docs/<path>.md` mention,
# BOUNDARY-CHECKED: the character immediately before "docs/" must be neither `/` nor alphanumeric.
# Without this, a qualified path like `synth-platform/docs/billing.md` would be misread as naming
# OUR docs/ tree (FRO169: synth-platform is a private sibling repo, and even after the cross-repo
# prose in this repo stopped naming its paths outright, the regex itself needed tightening so a
# FUTURE qualified mention can't slip through either). Same offset-tracking technique as
# check_d_violations' pass 3 below -- see that function's comment for why.
awk_scan_docs_path() {
    local pairs="$1"
    awk -v filelist="$pairs" "$CHECK_DOCS_AWK_LIB"'
        BEGIN {
            pat = "docs\\/[A-Za-z0-9_.\\/-]+\\.md"
            while ((getline pairline < filelist) > 0) {
                n = split(pairline, cols, "\t")
                if (n < 2) continue
                relpath = cols[1]
                abspath = cols[2]
                fnr = 0
                while ((getline line < abspath) > 0) {
                    fnr++
                    remaining = line
                    offset = 0
                    while ((idx = match(remaining, pat)) > 0) {
                        abs_idx = offset + idx
                        ok = docs_boundary_ok(line, abs_idx)
                        if (ok) {
                            text = substr(remaining, idx, RLENGTH)
                            print relpath "\t" fnr "\t" text
                        }
                        offset += idx + RLENGTH - 1
                        remaining = substr(remaining, idx + RLENGTH)
                    }
                }
                close(abspath)
            }
            close(filelist)
            exit
        }
    '
}

# check_c_violations <pairs-file> -- "relpath:line: message" per docs/... mention that doesn't
# resolve. Scans every in-scope file, not just *.md -- this is what catches a rename that missed a
# Source/** comment.
check_c_violations() {
    local pairs="$1"
    awk_scan_docs_path "$pairs" | while IFS="$(printf '\t')" read -r path line match; do
        [ -n "$path" ] || continue
        if [ ! -f "$ROOT/$match" ]; then
            printf '%s:%s: '\''%s'\'' does not exist\n' "$path" "$line" "$match"
        fi
    done
}

# --- checks D, H, I: section markers -------------------------------------------------------------

# Files that DOCUMENT the section-marker pattern (or fixture it) and so legitimately carry bare
# markers -- exempt from check I only. Checks D/H still apply to them.
BARE_MARKER_EXEMPT="scripts/check-docs.sh|scripts/lib/check-docs-checks.sh|scripts/tests/check-docs.test.sh|docs/development/docs-guard.md"

# marker_scan <pairs-file (all)> <headings-file> <kind: D|H|I> -- "relpath:line: message" for every
# `§N`/`§N.M`/`§N.M.K` section marker of the requested kind, in every in-scope file. ONE awk program
# classifies each marker on a line, then prints only the kind asked for (check D/H/I below are
# three thin callers), so the three checks can never disagree about which path a marker belongs to.
# NOT baselined -- ZERO TOLERANCE for all three; see the header comment for why.
#
# Per line, the program collects every boundary-checked `docs/<path>.md` mention (an optional
# `#anchor` is part of the mention, so a marker after `docs/<x>.md#anchor` is attached to that doc) and
# every marker, then binds each marker, in this order:
#   1. REVERSED (kind H): the next path on the line starts right after the marker, separated only by
#      a preposition (`§8 of docs/<x>.md`, `§2 in the docs/<x>.md`). It belongs to THAT later path. This
#      is decided FIRST so a marker meant for a later mention is never read as a continuation of an
#      earlier path on the same line (FRO217: `docs/<a>.md, §8 of docs/<b>.md` -- §8 belongs to the second path).
#   2. FORWARD (kind D): otherwise it belongs to the nearest path BEFORE it, if within 12 characters
#      of that path's end -- or, for a second/third marker (`docs/<x>.md §1, §2, §3`), within 12
#      characters of the previous marker attached to the same path. Every marker in such a chain is
#      validated, not only the first (the old check D looked at the first one only).
#   3. Anything else is BARE (kind I): no docs path before it on the line, or none close enough to
#      claim it. Skipped for files in BARE_MARKER_EXEMPT.
# D and H resolve against check-D's headings table via section_in_doc (see CHECK_DOCS_AWK_LIB); a
# docs path that does not exist at all is check C's failure to report, so it is skipped here.
marker_scan() {
    local pairs="$1" headings_file="$2" kind="$3"
    awk -v filelist="$pairs" -v headingsfile="$headings_file" -v kind="$kind" -v exempt="$BARE_MARKER_EXEMPT" "$CHECK_DOCS_AWK_LIB"'
        BEGIN {
            while ((getline pairline < filelist) > 0) {
                split(pairline, cols, "\t")
                exists[cols[1]] = 1
            }
            close(filelist)
            load_headings(headingsfile)
            nex = split(exempt, exl, "|")
            for (i = 1; i <= nex; i++) isexempt[exl[i]] = 1

            pathre = "docs\\/[A-Za-z0-9_.\\/-]+\\.md(#[A-Za-z0-9_-]+)?"
            secre = "§[ \t]*[0-9]+(\\.[0-9]+){0,2}"
            revgapre = "^[ \t]+(of|in|from)[ \t]+(the[ \t]+)?$"

            while ((getline pairline < filelist) > 0) {
                n = split(pairline, cols, "\t")
                if (n < 2) continue
                relpath = cols[1]; abspath = cols[2]
                fnr = 0
                while ((getline line < abspath) > 0) {
                    fnr++
                    if (index(line, "§") == 0) continue

                    np = 0; remaining = line; offset = 0
                    while ((idx = match(remaining, pathre)) > 0) {
                        len = RLENGTH
                        abs_idx = offset + idx
                        if (docs_boundary_ok(line, abs_idx)) {
                            np++
                            ps[np] = abs_idx
                            pe[np] = abs_idx + len - 1
                            ptext = substr(remaining, idx, len)
                            hp = index(ptext, "#")
                            pd[np] = (hp > 0) ? substr(ptext, 1, hp - 1) : ptext
                        }
                        offset += idx + len - 1
                        remaining = substr(remaining, idx + len)
                    }

                    nm = 0; remaining = line; offset = 0
                    while ((idx = match(remaining, secre)) > 0) {
                        len = RLENGTH
                        nm++
                        ms[nm] = offset + idx
                        me[nm] = offset + idx + len - 1
                        mv[nm] = substr(remaining, idx, len)
                        sub(/^§[ \t]*/, "", mv[nm])
                        offset += idx + len - 1
                        remaining = substr(remaining, idx + len)
                    }

                    fwd_path = 0; fwd_end = 0
                    for (m = 1; m <= nm; m++) {
                        rk = 0
                        for (k = 1; k <= np; k++) { if (ps[k] > me[m]) { rk = k; break } }
                        if (rk > 0 && substr(line, me[m] + 1, ps[rk] - me[m] - 1) ~ revgapre) {
                            if (kind == "H" && (pd[rk] in exists) && !section_in_doc(pd[rk], mv[m])) {
                                print relpath ":" fnr ": §" mv[m] " of " pd[rk] " (marker written before the path) -- no such section in " pd[rk]
                            }
                            continue
                        }
                        pk = 0
                        for (k = 1; k <= np; k++) { if (pe[k] < ms[m]) pk = k }
                        if (pk > 0) {
                            start = (fwd_path == pk) ? fwd_end : pe[pk]
                            if (ms[m] - start - 1 <= 12) {
                                fwd_path = pk; fwd_end = me[m]
                                if (kind == "D" && (pd[pk] in exists) && !section_in_doc(pd[pk], mv[m])) {
                                    print relpath ":" fnr ": " pd[pk] " §" mv[m] " -- no such section in " pd[pk]
                                }
                                continue
                            }
                        }
                        if (kind == "I" && !(relpath in isexempt)) {
                            print relpath ":" fnr ": section marker §" mv[m] " is not attached to a docs/ path (none before it on this line, or too far from it) -- write the full docs/<path>.md#<anchor> form"
                        }
                    }
                }
                close(abspath)
            }
            close(filelist)
            exit
        }
    '
}

# check_d_violations <pairs-file> <headings-file> -- forward references: `docs/<path>.md ... §N`
# (every marker in a chain, see marker_scan) naming a section the doc does not have.
check_d_violations() { marker_scan "$1" "$2" D; }

# check_h_violations <pairs-file> <headings-file> -- reversed references: `§N of docs/<path>.md`.
check_h_violations() { marker_scan "$1" "$2" H; }

# check_i_violations <pairs-file> <headings-file> -- bare markers: a section marker with no docs path
# to attach to. The fix is always the full `docs/<path>.md#<anchor>` form (or plain words).
check_i_violations() { marker_scan "$1" "$2" I; }

# --- check E: docs/README.md map completeness --------------------------------------------------

# readme_linked_docs -- "docs/<name>.md" (sorted, deduped) for every markdown-link target
# docs/README.md points at within docs/ itself (relative targets ending in .md, resolved against
# README.md's own directory -- i.e. docs/). Reuses awk_scan's generic extractor on a one-line
# pairs file built just for docs/README.md.
readme_linked_docs() {
    local readme_pairs
    readme_pairs="$(mktemp)"
    printf 'docs/README.md\t%s/docs/README.md\n' "$ROOT" >"$readme_pairs"
    awk_scan "$readme_pairs" '\]\([^)]*\)' | while IFS="$(printf '\t')" read -r path line rest; do
        [ -n "$path" ] || continue
        target="${rest#](}"
        target="${target%)}"
        case "$target" in
            http:* | https:* | mailto:* | '#'*) continue ;;
        esac
        case "$target" in
            *.md | *.md'#'*) ;;
            *) continue ;;
        esac
        tpath="${target%%#*}"
        resolved="$(normalize_rel_path "docs/$tpath")"
        printf '%s\n' "$resolved"
    done | sort -u
    rm -f "$readme_pairs"
}

# check_e_violations <scanned-file> -- "relpath: message" per docs/README.md map-completeness
# violation: a real docs/**/*.md file (other than README.md) that the map never links, or a map
# link that resolves to a file that doesn't exist (check B already catches the latter as a broken
# link -- this is belt-and-braces so check E stands on its own without depending on check B having
# run first). <scanned-file> is list_scanned_paths' own output, shared across every check in this
# run -- see the perf note above run_check.
check_e_violations() {
    local scanned="$1"
    local linked_file real_file
    linked_file="$(mktemp)"
    real_file="$(mktemp)"
    readme_linked_docs >"$linked_file"
    while IFS= read -r path; do
        case "$path" in
            docs/*.md) [ "$path" = "docs/README.md" ] || printf '%s\n' "$path" ;;
        esac
    done <"$scanned" | sort >"$real_file"

    while IFS= read -r path; do
        [ -n "$path" ] || continue
        if ! grep -qxF -- "$path" "$linked_file"; then
            printf '%s: not linked from docs/README.md\n' "$path"
        fi
    done <"$real_file"

    while IFS= read -r linked; do
        [ -n "$linked" ] || continue
        if [ ! -f "$ROOT/$linked" ]; then
            printf 'docs/README.md: links to %s, which does not exist\n' "$linked"
        fi
    done <"$linked_file"

    rm -f "$linked_file" "$real_file"
}

# --- check F: docs/...#anchor mentions outside markdown link syntax ---------------------------

# check_f_violations <pairs-file (all)> <slugs-file (from build_slugs_file, md-only)> --
# "relpath:line: message" per `docs/<path>.md#<anchor>` mention, in ANY in-scope file, whose anchor
# doesn't match a real heading slug in the target doc. NOT baselined -- zero tolerance, same as
# B/C/D/E. This is what stops the FRO166 docs restructure (converting ~500 `§N` references, hard-
# gated by check D, into `#anchor` references) from trading gated references for ungated ones: an
# anchor mention written as plain prose or a Source/**/*.cpp comment -- not `](target)` markdown
# link syntax -- is invisible to check B, which only looks inside that syntax in *.md files.
#
# ONE awk process does everything, same reasoning as check_d_violations above it: with ~500 anchor
# references coming in the wake of this ticket, a per-occurrence subshell/subprocess (as an earlier
# version of this script paid for elsewhere) would dominate runtime the same way it used to for
# check B. Pass 1 loads which relpaths actually exist (a docpath that doesn't exist at all is check
# C's failure to report, not ours -- skipped here to avoid double-reporting the same mistake under
# two different checks). Pass 2 loads <slugs-file> -- the EXACT SAME table check_b_violations uses,
# built once by build_slugs_file and shared by both callers in run_check/run_list -- into an
# in-memory set, so check B and check F can never disagree about what counts as a valid slug (no
# second slug implementation exists anywhere in this file). Pass 3 scans every in-scope file for the
# `docs/<path>.md#<anchor>` pattern with the SAME boundary rule as check C's awk_scan_docs_path (the
# character immediately before "docs/" must be neither `/` nor alphanumeric, so a qualified path
# like `synth-platform/docs/<x>.md#y` is never misread as naming OUR docs/ tree either), and checks
# each occurrence in-memory against the two tables built above.
check_f_violations() {
    local pairs="$1" slugs_file="$2"
    awk -v filelist="$pairs" -v slugsfile="$slugs_file" "$CHECK_DOCS_AWK_LIB"'
        BEGIN {
            # Pass 1: which relpaths actually exist (a docpath not in this set is check C'"'"'s to
            # report, not ours -- skip it here rather than double-report).
            while ((getline pairline < filelist) > 0) {
                split(pairline, cols, "\t")
                exists[cols[1]] = 1
            }
            close(filelist)

            # Pass 2: check B'"'"'s own slug table, keyed "relpath\x01slug" for an O(1) lookup.
            while ((getline sline < slugsfile) > 0) {
                split(sline, scols, "\t")
                slugs[scols[1] "\x01" scols[2]] = 1
            }
            close(slugsfile)

            pat = "docs\\/[A-Za-z0-9_.\\/-]+\\.md#[A-Za-z0-9_-]+"

            # Pass 3: scan every in-scope file'"'"'s content for occurrences and check each one
            # in-memory against the tables built above.
            while ((getline pairline < filelist) > 0) {
                n = split(pairline, cols, "\t")
                if (n < 2) continue
                relpath = cols[1]; abspath = cols[2]
                fnr = 0
                while ((getline line < abspath) > 0) {
                    fnr++
                    remaining = line
                    offset = 0
                    while ((idx = match(remaining, pat)) > 0) {
                        abs_idx = offset + idx
                        ok = docs_boundary_ok(line, abs_idx)
                        if (ok) {
                            text = substr(remaining, idx, RLENGTH)
                            hashpos = index(text, "#")
                            docpath = substr(text, 1, hashpos - 1)
                            anchor = substr(text, hashpos + 1)
                            if ((docpath in exists) && !((docpath "\x01" anchor) in slugs)) {
                                printf "%s:%d: anchor mention '"'"'%s'"'"' has no heading matching anchor '"'"'#%s'"'"'\n", relpath, fnr, text, anchor
                            }
                        }
                        offset += idx + RLENGTH - 1
                        remaining = substr(remaining, idx + RLENGTH)
                    }
                }
                close(abspath)
            }
            close(filelist)
            exit
        }
    '
}

# --- check G: bare basename references -----------------------------------------------------------

# check_g_violations <pairs-file (all)> <slugs-file (from build_slugs_file)> <headings-file (from
# build_headings_file)> -- "relpath:line: message" per bare backtick-wrapped `` `<name>.md` ``
# reference (no `docs/` or other path prefix, no markdown link target) whose trailing `§N` or
# `#anchor` marker doesn't resolve, or whose basename doesn't resolve to exactly one real
# docs/**/*.md file. NOT baselined -- zero tolerance, same as B/C/D/E/F.
#
# ONE awk process, same three-table-then-scan shape as check D/F above it. Pass 1 builds a basename
# -> docs/**/*.md relpath(s) index (SOH-joined, so an ambiguous basename is detected directly from
# its own length rather than a second lookup) -- this is a plain index over the same file list every
# other check already has, not a third slug/section implementation. Pass 2 loads check B/F's own
# slug table (<slugs-file>) for a trailing `#anchor`. Pass 3 loads check D's own headings table
# (<headings-file>, from the shared build_headings_file) for a trailing `§N`. Pass 4 scans every
# in-scope file's content: for each LINE, it first masks out every full `[text](target)`
# markdown-link span (the same construct check B parses, broadened to the whole bracket+paren span)
# so a bare name that IS inside proper link syntax is never seen here at all -- that is check B's
# business, and reporting it again here would double-report the same mistake check F's own
# comment warns about. It then finds every backtick-wrapped `` `name.md` `` on its own (never
# requiring a trailing marker to even notice the name -- see the AMBIGUITY note below for why) and
# separately looks ahead, past a whitespace-only gap of up to 3 characters, for an OPTIONAL trailing
# `§N`/`§N.M`/`§N.M.K` marker or `#anchor`. The gap is whitespace-only, narrower than check D's any-
# char-up-to-12 budget: every real bare-name-plus-marker occurrence in this repo is exactly a name
# plus ONE space plus the marker, and a wider gap risks stealing a marker that actually belongs to a
# CLOSER, different mention sitting between this bare name and it -- a real line in this repo reads
# roughly "root CLAUDE.md, section 8 of docs/architecture/app-wiring.md": the comma-space gap there must NOT
# let the bare CLAUDE.md name (which is not even a docs/ file) claim a section marker that check
# C/D already validate against the docs/-prefixed path that actually follows it.
#
# AMBIGUITY is checked for EVERY bare name found, marker or none: a basename matching more than one
# real docs/**/*.md file is unresolvable for a reader regardless of whether prose happened to
# attach a section marker to this particular occurrence, so it is always reported. A basename
# matching *zero* docs files, by contrast, is reported ONLY when a marker follows -- with no marker
# there is no signal this was ever meant as a docs cross-reference at all (a plain `CLAUDE.md`
# mention, say), and FRO217's own measurement of the real tree found every genuine bare-basename
# cross-reference in this repo follows the "name plus marker" shape, so requiring one here costs no
# real coverage. A basename matching exactly one doc has its trailing marker (if any) checked
# against Pass 2/3's tables the same way check F/D already do; with no marker, there is nothing to
# check and it is silently fine.
check_g_violations() {
    local pairs="$1" slugs_file="$2" headings_file="$3"
    awk -v filelist="$pairs" -v slugsfile="$slugs_file" -v headingsfile="$headings_file" "$CHECK_DOCS_AWK_LIB"'
        BEGIN {
            # Pass 1: basename -> SOH-joined list of docs/**/*.md relpaths sharing it, plus a count
            # per basename so ambiguity is a single lookup.
            while ((getline pairline < filelist) > 0) {
                n = split(pairline, cols, "\t")
                if (n < 2) continue
                relpath = cols[1]
                if (relpath !~ /^docs\/.*\.md$/) continue
                base = relpath
                slashpos = 0
                for (i = length(base); i >= 1; i--) {
                    if (substr(base, i, 1) == "/") { slashpos = i; break }
                }
                if (slashpos > 0) base = substr(base, slashpos + 1)
                basecount[base] = basecount[base] + 1
                baselist[base] = (base in baselist) ? baselist[base] "\x01" relpath : relpath
            }
            close(filelist)

            # Pass 2: check B/F'"'"'s own slug table, keyed "relpath\x01slug".
            while ((getline sline < slugsfile) > 0) {
                split(sline, scols, "\t")
                slugs[scols[1] "\x01" scols[2]] = 1
            }
            close(slugsfile)

            # Pass 3: check D'"'"'s own headings table (see build_headings_file), keyed by relpath as
            # a SOH-joined list of section numbers -- same lookup style check D itself uses.
            load_headings(headingsfile)

            # See the header comment above this function for why the gap is whitespace-only and
            # why ambiguity (cnt > 1) is checked independently of whether a marker follows at all.
            namere = "`[A-Za-z0-9_-]+\\.md`"
            gapmarkre = "^[ \t]{0,3}(§[ \t]*[0-9]+(\\.[0-9]+){0,2}|#[A-Za-z0-9_-]+)"
            secre = "§[ \t]*[0-9]+(\\.[0-9]+){0,2}"
            anchre = "#[A-Za-z0-9_-]+"
            linkre = "\\[[^]]*\\]\\([^)]*\\)"

            # Pass 4: scan every in-scope file'"'"'s content for occurrences and check each one
            # in-memory against the tables built above.
            while ((getline pairline < filelist) > 0) {
                n = split(pairline, cols, "\t")
                if (n < 2) continue
                relpath = cols[1]; abspath = cols[2]
                fnr = 0
                while ((getline line < abspath) > 0) {
                    fnr++
                    # Mask out every full markdown-link span first -- a bare name inside proper
                    # `[text](target)` syntax is check B'"'"'s business, not ours.
                    masked = line
                    gsub(linkre, "", masked)

                    remaining = masked
                    while ((idx = match(remaining, namere)) > 0) {
                        # RLENGTH is a GLOBAL awk variable that every later match() call below
                        # (on `after`, then on `token`) overwrites -- including to -1 on a failed
                        # match, which is not merely "wrong", it is BACKWARD, and using it to
                        # advance `remaining` at the end of this loop would rewind onto the same
                        # text forever. Capture THIS match''s length into its own variable before
                        # any nested match() call can clobber it, and advance by that, never by a
                        # bare `RLENGTH` read after this point.
                        namelen = RLENGTH
                        namepart = substr(remaining, idx, namelen)
                        base = substr(namepart, 2, length(namepart) - 2)
                        after = substr(remaining, idx + namelen)

                        markerkind = ""
                        markerval = ""
                        if (match(after, gapmarkre)) {
                            token = substr(after, RSTART, RLENGTH)
                            if (match(token, secre)) {
                                markerkind = "section"
                                markerval = substr(token, RSTART, RLENGTH)
                                sub(/^§[ \t]*/, "", markerval)
                            } else if (match(token, anchre)) {
                                markerkind = "anchor"
                                markerval = substr(token, RSTART + 1, RLENGTH - 1)
                            }
                        }

                        cnt = basecount[base] + 0
                        if (cnt > 1) {
                            # Ambiguous regardless of whether a marker follows THIS occurrence --
                            # a basename a script cannot resolve is one a reader cannot resolve
                            # either, marker or none.
                            list = baselist[base]
                            gsub(/\x01/, ", ", list)
                            printf "%s:%d: bare reference '"'"'%s'"'"' is ambiguous -- matches %d docs (%s); write the full docs/ path instead\n", relpath, fnr, base, cnt, list
                        } else if (markerkind != "") {
                            if (cnt == 0) {
                                printf "%s:%d: bare reference '"'"'%s'"'"' names a doc that does not exist anywhere under docs/\n", relpath, fnr, base
                            } else {
                                resolved = baselist[base]
                                found = 0
                                if (markerkind == "section") {
                                    found = section_in_doc(resolved, markerval)
                                    if (!found) {
                                        printf "%s:%d: bare reference '"'"'%s'"'"' (resolved to %s) §%s -- no such section in %s\n", relpath, fnr, base, resolved, markerval, resolved
                                    }
                                } else {
                                    if ((resolved "\x01" markerval) in slugs) { found = 1 }
                                    if (!found) {
                                        printf "%s:%d: bare reference '"'"'%s'"'"' (resolved to %s) has no heading matching anchor '"'"'#%s'"'"'\n", relpath, fnr, base, resolved, markerval
                                    }
                                }
                            }
                        }

                        remaining = substr(remaining, idx + namelen)
                    }
                }
                close(abspath)
            }
            close(filelist)
            exit
        }
    '
}

