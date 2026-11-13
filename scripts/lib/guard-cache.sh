# scripts/lib/guard-cache.sh -- sourced by the check-*.sh guards, not executed directly.
#
# A result cache for guards that scan many files. It remembers what a scanner printed for one file,
# keyed by the file's content, so the next run (in any worktree of this repository) only scans the
# files that changed. The cache never stores a verdict for the per-file guards: each one still
# compares the current per-file answers with its baseline and limits on every run, so a changed
# baseline or a lowered limit can never be skipped.
#
# Where: <git common dir>/agentsynth-guard-cache/<guard>-<hash>/. The <hash> covers the guard
# script, the scanner it drives, this file and anything the guard names as extra key material, so
# editing any of them starts a fresh set of entries. Each entry is its own file, written to a
# temporary name and renamed into place, so several gates writing at once cannot tear an entry. An
# entry that is missing, half-written or otherwise unreadable counts as a miss and the file is
# rescanned. An unwritable folder turns the cache off for the run; the guard still works.
#
# GUARD_CACHE=0 turns it off. Unused folders and entries older than a month are pruned about daily.
#
# Callers set ROOT (the repository root the guard scans) before sourcing.

# gc_init <name> <extra-key-text> <file>... -- picks this guard's cache folder; GC_DIR stays empty
# (cache off) when it cannot.
gc_init() {
    GC_DIR=""
    [ "${GUARD_CACHE:-1}" = 0 ] && return 0
    local name="$1" extra="$2" common salt base
    shift 2
    common="$(git -C "$ROOT" rev-parse --git-common-dir 2>/dev/null)" || return 0
    case "$common" in /*) : ;; *) common="$ROOT/$common" ;; esac
    salt="$({ printf '%s\n' "$extra"; cat "$@" 2>/dev/null; } | git -C "$ROOT" hash-object --stdin 2>/dev/null)" || return 0
    [ -n "$salt" ] || return 0
    base="$common/agentsynth-guard-cache"
    GC_DIR="$base/$name-$salt"
    mkdir -p "$GC_DIR" 2>/dev/null || { GC_DIR=""; return 0; }
    touch "$GC_DIR" 2>/dev/null || true
    if [ -z "$(find "$base/.pruned" -mtime -1 2>/dev/null)" ]; then
        touch "$base/.pruned" 2>/dev/null || true
        find "$base" -mindepth 1 -maxdepth 1 -type d -mtime +14 -exec rm -rf {} + 2>/dev/null || true
        find "$base" -type f -mtime +30 -delete 2>/dev/null || true
        find "$base" -type f -name '.tmp.*' -mmin +120 -delete 2>/dev/null || true
    fi
    return 0
}

# gc_rename_pairs <file of "tmp<TAB>final" lines> -- renames each tmp onto its final name, in one process.
gc_rename_pairs() {
    if command -v perl >/dev/null 2>&1; then
        perl -ne 'chomp; my ($a, $b) = split /\t/; rename($a, $b) or unlink($a);' <"$1"
    else
        local a b
        while IFS=$'\t' read -r a b; do mv -f "$a" "$b" 2>/dev/null || rm -f "$a"; done <"$1"
    fi
}

# gc_scan_files <path-awk> <paths-file> <scan-command>... -- prints the scanner's output for every
# path in <paths-file> (repo-relative, one per line, every file must exist), taking each file's share
# from the cache when its content is unchanged. <scan-command> reads the paths it must scan on stdin
# and prints lines; <path-awk> is awk code (fields split on TAB, the line is $0) that sets the
# variable p to the path a scanner line belongs to. A scanner that fails stores nothing.
gc_scan_files() {
    local pexpr="$1" list="$2" tmp rc=0
    shift 2
    if [ -z "${GC_DIR:-}" ]; then
        "$@" <"$list"
        return $?
    fi
    tmp="$(mktemp -d)"
    if ! (cd "$ROOT" && git hash-object --no-filters --stdin-paths <"$list") >"$tmp/blobs" 2>/dev/null ||
        [ "$(wc -l <"$tmp/blobs" | tr -d ' ')" != "$(wc -l <"$list" | tr -d ' ')" ]; then
        rm -rf "$tmp"
        "$@" <"$list"
        return $?
    fi
    paste "$tmp/blobs" "$list" >"$tmp/keyed"
    : >"$tmp/hits"
    : >"$tmp/miss"
    awk -F'\t' -v dir="$GC_DIR" -v hits="$tmp/hits" -v miss="$tmp/miss" '
        {
            e = $2; gsub(/%/, "%25", e); gsub(/\//, "%2F", e)
            f = dir "/" $1 "_" e
            ok = 0; n = 0
            delete buf
            if ((getline line < f) > 0 && line == "#ok") {
                while ((getline line < f) > 0) {
                    if (line == "#end") { ok = 1; break }
                    buf[++n] = line
                }
            }
            close(f)
            if (ok) { for (i = 1; i <= n; i++) print buf[i] > hits }
            else print $1 "\t" $2 > miss
        }' "$tmp/keyed"
    if [ -s "$tmp/miss" ]; then
        cut -f2 "$tmp/miss" >"$tmp/misspaths"
        "$@" <"$tmp/misspaths" >"$tmp/fresh" || rc=$?
        if [ "$rc" -eq 0 ]; then
            awk -F'\t' -v dir="$GC_DIR" -v missfile="$tmp/miss" -v pairs="$tmp/pairs" -v pid="$$" '
                FILENAME == missfile { blob[$2] = $1; next }
                { '"$pexpr"'; if (p in blob) buf[p] = buf[p] $0 "\n" }
                END {
                    for (p in blob) {
                        e = p; gsub(/%/, "%25", e); gsub(/\//, "%2F", e)
                        name = blob[p] "_" e
                        if (length(name) > 200) continue
                        i++
                        t = dir "/.tmp." pid "." i
                        printf "#ok\n%s#end\n", buf[p] > t
                        close(t)
                        print t "\t" dir "/" name > pairs
                    }
                }' "$tmp/miss" "$tmp/fresh" 2>/dev/null || true
            [ -s "$tmp/pairs" ] && gc_rename_pairs "$tmp/pairs"
        fi
    fi
    cat "$tmp/hits"
    [ -f "$tmp/fresh" ] && cat "$tmp/fresh"
    rm -rf "$tmp"
    return "$rc"
}

# gc_whole_key <paths-file> [extra-file]... -- prints a key over the content of every path listed
# (repo-relative, every file must exist) and over each extra file. Prints nothing when the cache is
# off or a hash cannot be computed.
gc_whole_key() {
    [ -n "${GC_DIR:-}" ] || return 0
    local list="$1" x
    shift
    {
        (cd "$ROOT" && git hash-object --no-filters --stdin-paths <"$list") | paste - "$list"
        for x in "$@"; do
            printf 'extra %s\n' "${x##*/}"
            cat "$x" 2>/dev/null
        done
    } | git -C "$ROOT" hash-object --stdin 2>/dev/null
}

# gc_whole_get <key> -- prints the output stored for <key> and succeeds, or fails when there is none.
gc_whole_get() {
    [ -n "${GC_DIR:-}" ] && [ -n "$1" ] || return 1
    awk '
        NR == 1 { if ($0 != "#ok") exit 1; next }
        $0 == "#end" { done = 1; exit }
        { lines[++n] = $0 }
        END { if (!done) exit 1; for (i = 1; i <= n; i++) print lines[i] }
    ' "$GC_DIR/whole_$1" 2>/dev/null
}

# gc_whole_put <key> <output-file> -- stores a passing run's output under <key>.
gc_whole_put() {
    [ -n "${GC_DIR:-}" ] && [ -n "$1" ] || return 0
    local t="$GC_DIR/.tmp.$$.whole"
    { echo "#ok"; cat "$2"; echo "#end"; } >"$t" 2>/dev/null && mv -f "$t" "$GC_DIR/whole_$1" 2>/dev/null || rm -f "$t"
    return 0
}
