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
    awk -v filelist="$pairs" '
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
                        ok = 1
                        if (abs_idx > 1) {
                            prevchar = substr(line, abs_idx - 1, 1)
                            if (prevchar ~ /[A-Za-z0-9\/]/) { ok = 0 }
                        }
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

# --- check D: §-section references -------------------------------------------------------------

# check_d_violations <pairs-file> <headings-file> -- "relpath:line: message" per stale §-reference
# in the current tree. ZERO TOLERANCE -- unlike check A this is never baselined; see the header
# comment for why.
#
# ONE awk process does everything check D needs: loads every docs/**/*.md heading number (from
# <headings-file>, built once by build_headings_file and shared with check G -- see that function's
# own comment) into an in-memory table, then scans every in-scope file's content for
# `docs/<path>.md ... §N` mentions and checks each one against that table directly -- no
# per-occurrence subshell or external `awk` call. An earlier version built the headings table
# inline in this same awk invocation (correct, but check G needed the identical table and a THIRD
# section-resolution implementation is exactly what FRO217 was told not to add); an even earlier
# version split this into three pieces (a headings-table builder, an occurrence extractor, and a
# `section_exists` helper invoked once per occurrence via its own `awk` subprocess) for clarity,
# correct too, but with 500+ §-references in this repo, spawning a process per occurrence was the
# single largest cost in this script under this environment's per-process overhead. Same boundary
# check as check C (awk_scan_docs_path) -- the character immediately before "docs/" must be neither
# `/` nor alphanumeric, so a qualified path like `synth-platform/docs/foo.md §3` is never misread as
# naming OUR docs/ tree -- and the same greedy-`.{0,12}`-then-first-`§` rule for what one occurrence
# means (see check C's own comment on the GNU-vs-BSD lazy-quantifier mismatch that greedy works
# around).
check_d_violations() {
    local pairs="$1" headings_file="$2"
    awk -v filelist="$pairs" -v headingsfile="$headings_file" '
        BEGIN {
            # Pass 1: which relpaths actually exist (a docpath not in this set is check C''s to
            # report, not ours -- skip it here rather than double-report).
            while ((getline pairline < filelist) > 0) {
                split(pairline, cols, "\t")
                exists[cols[1]] = 1
            }
            close(filelist)

            # Pass 2: every h2-h6 heading number in every docs/**/*.md file, keyed by doc, as a
            # SOH-separated list (awk has no portable 2D array iteration, so a query does its own
            # split()+loop below rather than a hash lookup -- cheap: a doc has a handful of
            # headings, not hundreds). Loaded from the shared table build_headings_file already
            # built -- see this function''s own header comment for why it is no longer built here.
            while ((getline hline < headingsfile) > 0) {
                split(hline, hcols, "\t")
                hrelpath = hcols[1]; hnum = hcols[2]
                headings[hrelpath] = (hrelpath in headings) ? headings[hrelpath] "\x01" hnum : hnum
            }
            close(headingsfile)

            outer = "docs\\/[A-Za-z0-9_.\\/-]+\\.md.{0,12}§[ \t]*[0-9]+(\\.[0-9]+){0,2}"
            docre = "^docs\\/[A-Za-z0-9_.\\/-]+\\.md"
            secre = "§[ \t]*[0-9]+(\\.[0-9]+){0,2}"

            # Pass 3: scan every in-scope file'"'"'s content for occurrences and check each one
            # in-memory against the headings table built above.
            while ((getline pairline < filelist) > 0) {
                split(pairline, cols, "\t")
                relpath = cols[1]; abspath = cols[2]
                fnr = 0
                while ((getline line < abspath) > 0) {
                    fnr++
                    remaining = line
                    offset = 0
                    while ((idx = match(remaining, outer)) > 0) {
                        abs_idx = offset + idx
                        ok = 1
                        if (abs_idx > 1) {
                            prevchar = substr(line, abs_idx - 1, 1)
                            if (prevchar ~ /[A-Za-z0-9\/]/) { ok = 0 }
                        }
                        if (ok) {
                            text = substr(remaining, idx, RLENGTH)
                            match(text, docre)
                            docpath = substr(text, RSTART, RLENGTH)
                            spos = index(text, "§")
                            secpart = substr(text, spos)
                            match(secpart, secre)
                            secnum = substr(secpart, RSTART, RLENGTH)
                            sub(/^§[ \t]*/, "", secnum)
                            if (docpath != "" && secnum != "" && (docpath in exists)) {
                                found = 0
                                if (docpath in headings) {
                                    ns = split(headings[docpath], nums, "\x01")
                                    qq = secnum "."
                                    for (i = 1; i <= ns; i++) {
                                        if (nums[i] == secnum || index(nums[i], qq) == 1) { found = 1; break }
                                    }
                                }
                                if (!found) {
                                    print relpath ":" fnr ": " docpath " §" secnum " -- no such section in " docpath
                                }
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
# like `synth-platform/docs/x.md#y` is never misread as naming OUR docs/ tree either), and checks
# each occurrence in-memory against the two tables built above.
check_f_violations() {
    local pairs="$1" slugs_file="$2"
    awk -v filelist="$pairs" -v slugsfile="$slugs_file" '
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
                        ok = 1
                        if (abs_idx > 1) {
                            prevchar = substr(line, abs_idx - 1, 1)
                            if (prevchar ~ /[A-Za-z0-9\/]/) { ok = 0 }
                        }
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
# roughly "root CLAUDE.md, section 8 of docs/architecture.md": the comma-space gap there must NOT
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
    awk -v filelist="$pairs" -v slugsfile="$slugs_file" -v headingsfile="$headings_file" '
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
            while ((getline hline < headingsfile) > 0) {
                split(hline, hcols, "\t")
                hrelpath = hcols[1]; hnum = hcols[2]
                headings[hrelpath] = (hrelpath in headings) ? headings[hrelpath] "\x01" hnum : hnum
            }
            close(headingsfile)

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
                                    if (resolved in headings) {
                                        ns = split(headings[resolved], nums, "\x01")
                                        qq = markerval "."
                                        for (i = 1; i <= ns; i++) {
                                            if (nums[i] == markerval || index(nums[i], qq) == 1) { found = 1; break }
                                        }
                                    }
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

