# scripts/lib/trial-merge.sh -- sourced by scripts/ci-local.sh, not executed directly.
#
# CI checks the branch merged into the newest main, so a whole-tree ratchet (header reach, file sizes,
# function sizes, header comments) can fail there while the branch alone passes: main grew since the
# branch was cut. trial_merge_tree builds that merged tree in a scratch git repo, without touching the
# working tree, the index or the real object store, so the gate can run those guards on it.
#
# trial_merge_tree <repo_root> <dest_dir> [<base_ref>]   (base_ref default: origin/main)
#   0   <dest_dir> holds the merged tree as a one-commit git repo (the guards need `git ls-files`)
#   10  nothing to do: the branch already contains <base_ref> (or HEAD is it)
#   11  <base_ref> does not exist here (no remote, shallow CI clone): the trial is skipped
#   12  the branch conflicts with <base_ref>: CI could not merge it either; rebase first
#   13  the merge could not be built (git failure): skipped with a warning
# Set TRIAL_MERGE_FETCH=1 to refresh the base ref first (best effort, bounded to ~15 s of network stall).

trial_merge_tree() {
    local repo="$1" dest="$2" base="${3:-origin/main}" objdir tree
    git -C "$repo" rev-parse --verify -q "$base^{commit}" >/dev/null 2>&1 || {
        # An unfetched ref is the same as no ref: the caller prints why.
        return 11
    }
    if [ "${TRIAL_MERGE_FETCH:-0}" = 1 ]; then
        local remote="${base%%/*}" branch="${base#*/}"
        GIT_HTTP_LOW_SPEED_LIMIT=1000 GIT_HTTP_LOW_SPEED_TIME=15 \
            git -C "$repo" fetch -q "$remote" "$branch" 2>/dev/null || echo "ci-local: could not fetch $base; using the copy already here." >&2
    fi
    if git -C "$repo" merge-base --is-ancestor "$base" HEAD 2>/dev/null; then
        return 10
    fi
    # The merge writes its tree objects into a scratch object folder that borrows the real one, so a
    # sandboxed run (which cannot write under the shared .git) works and nothing is left behind.
    objdir="$dest.objects"
    mkdir -p "$objdir" "$dest" || return 13
    local real_objects
    real_objects="$(git -C "$repo" rev-parse --path-format=absolute --git-path objects 2>/dev/null)" || return 13
    local rc=0
    tree="$(GIT_OBJECT_DIRECTORY="$objdir" GIT_ALTERNATE_OBJECT_DIRECTORIES="$real_objects" \
        git -C "$repo" merge-tree --write-tree --no-messages HEAD "$base" 2>/dev/null)" || rc=$?
    if [ "$rc" -eq 1 ]; then
        rm -rf "$objdir"
        return 12
    fi
    if [ "$rc" -ne 0 ] || [ -z "$tree" ]; then
        rm -rf "$objdir"
        return 13
    fi
    tree="$(printf '%s\n' "$tree" | head -1)"
    GIT_OBJECT_DIRECTORY="$objdir" GIT_ALTERNATE_OBJECT_DIRECTORIES="$real_objects" \
        git -C "$repo" archive --format=tar "$tree" | tar -x -C "$dest" || {
        rm -rf "$objdir"
        return 13
    }
    rm -rf "$objdir"
    (
        cd "$dest" || exit 1
        git init -q . &&
            git add -A &&
            git -c user.name=trial -c user.email=trial@invalid commit -q -m "trial merge of HEAD and $base"
    ) >/dev/null 2>&1 || return 13
    return 0
}
