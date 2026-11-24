#!/usr/bin/env bash
# Sourced helper for ci-local.sh --fast: say loudly when the built app or AIEvalHarness predates the code.
#
# --fast builds only the Tests target, so an app bundle or AIEvalHarness left by an earlier gate is
# older than the change it sits beside, and an in-app check or eval run against it tests old code.
# warn_stale_binaries only warns; it never fails and never rebuilds.
#
# Usage: source scripts/lib/stale-binaries.sh; warn_stale_binaries <build-dir> <merge-base> [<app-bundle>]
# Run from the repo root.

# _sb_mtime <file> -- epoch seconds of the last modification (BSD stat, then GNU stat).
_sb_mtime() {
    stat -f %m "$1" 2>/dev/null || stat -c %Y "$1" 2>/dev/null || echo 0
}

# _sb_newest_source <merge-base> -- prints "<epoch> <path>" of the newest changed file that goes into a binary.
_sb_newest_source() {
    local f t best=0 best_f=""
    while IFS= read -r f; do
        [ -f "$f" ] || continue
        t="$(_sb_mtime "$f")"
        if [ "$t" -gt "$best" ]; then
            best="$t"
            best_f="$f"
        fi
    done < <({ git diff --name-only "$1" -- Source Tools cmake CMakeLists.txt assets; git ls-files -o --exclude-standard -- Source Tools cmake CMakeLists.txt assets; } 2>/dev/null)
    [ -n "$best_f" ] && echo "$best $best_f"
    return 0
}

warn_stale_binaries() {
    local build_dir="$1" base="$2" app="${3:-}" newest newest_t newest_f exe harness bin t stale=false
    newest="$(_sb_newest_source "$base")"
    if [ -z "$newest" ]; then
        return 0
    fi
    newest_t="${newest%% *}"
    newest_f="${newest#* }"

    exe=""
    if [ -n "$app" ] && [ -d "$app/Contents/MacOS" ]; then
        exe="$(find "$app/Contents/MacOS" -type f -perm -u+x 2>/dev/null | head -n 1 || true)"
    fi
    harness="$( (find "$build_dir" -name AIEvalHarness -type f -perm -u+x 2>/dev/null || true) | head -n 1)"

    if [ -z "$exe" ]; then
        echo "ci-local: --fast built no app bundle (none exists under $build_dir): do not run the app or ui-verify from this worktree until \`bash scripts/ci-local.sh --skip-tests\` has built one."
    fi
    for bin in "$exe" "$harness"; do
        [ -n "$bin" ] || continue
        t="$(_sb_mtime "$bin")"
        if [ "$t" -lt "$newest_t" ]; then
            stale=true
            printf 'ci-local: ################ STALE BINARY ################\n'
            printf 'ci-local: %s\n' "$bin"
            printf 'ci-local:   is older than %s (changed after it was built).\n' "$newest_f"
            printf 'ci-local:   --fast does not rebuild it; an in-app check or eval would run old code.\n'
        fi
    done
    if [ "$stale" = true ]; then
        echo "ci-local: Rebuild before any in-app check or eval:  bash scripts/ci-local.sh --skip-tests"
    fi
    return 0
}
