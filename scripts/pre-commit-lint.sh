#!/bin/bash
# Pre-commit hook: clang-format lint on staged C/C++ sources, plus the file-size and
# function-size guards.
#
# Mirrors the CI "Lint" job's clang-format --dry-run --Werror over Source/ and Tests/, but scoped
# to the files staged for THIS commit, so it runs fast and catches formatting before it ever
# reaches CI. The file-size guard (scripts/check-file-sizes.sh, FRO61) and the function-size guard
# (scripts/check-function-sizes.sh, FRO78) both run unscoped -- they scan the whole git-tracked
# tree either way, and each is fast enough on its own that there's no benefit to staged-file
# scoping there the way there is for clang-format. They run for ANY commit with staged changes,
# not only ones touching C++: a doc or script edit can push a file over its cap just as easily as
# a source change can (the function-size guard only ever scans C++ itself, but a change anywhere
# can still shrink a function below the cap and leave the baseline stale). Installed by
# scripts/install-hooks.sh. Bypass a single commit with:
# git commit --no-verify
#
# NOTE: clang-format is PINNED — CI and this hook must run the SAME version.
# The pinned version lives in .clang-format-version (repo root). Install the
# exact binary with:  pip install "clang-format==$(cat .clang-format-version)"
# A version mismatch only WARNS below (the hook still runs) so you know why CI
# might disagree.

repo_root="$(git rev-parse --show-toplevel)"

# All staged (added/copied/modified) files -- used to decide whether this is a real commit at all
# (an empty/--allow-empty commit stages nothing, and neither check has anything to say about it).
staged_files=$(git diff --cached --name-only --diff-filter=ACM || true)

if [ -z "$staged_files" ]; then
    exit 0
fi

# --- clang-format: only the staged C/C++ files under Source/ or Tests/, and only if there are any.
cpp_files=$(printf '%s\n' "$staged_files" | grep -E '^(Source|Tests)/.*\.(h|cpp)$' || true)

if [ -n "$cpp_files" ]; then
    if ! command -v clang-format >/dev/null 2>&1; then
        echo "pre-commit: clang-format not found on PATH." >&2
        echo "            Install the pinned version:  pip install \"clang-format==\$(cat .clang-format-version)\"" >&2
        echo "            Or bypass with: git commit --no-verify" >&2
        exit 1
    fi

    # Warn (don't fail) when the local clang-format differs from the pinned version
    # so a developer knows why CI might disagree with a locally-clean tree.
    pinned="$(cat "$repo_root/.clang-format-version" 2>/dev/null || true)"
    have="$(clang-format --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
    if [ -n "$pinned" ] && [ "$have" != "$pinned" ]; then
        echo "pre-commit: clang-format $have differs from pinned $pinned (CI uses $pinned)." >&2
        echo "            Match it:  pip install \"clang-format==$pinned\"" >&2
    fi

    if ! echo "$cpp_files" | xargs clang-format --dry-run --Werror; then
        echo "" >&2
        echo "pre-commit: clang-format violations in staged files (see above)." >&2
        echo "  Fix all:  find Source Tests -name '*.h' -o -name '*.cpp' | xargs clang-format -i" >&2
        echo "  Bypass:   git commit --no-verify" >&2
        exit 1
    fi
fi

# --- File-size guard: whole tree, fast, runs regardless of which files are staged (FRO61).
if ! bash "$repo_root/scripts/check-file-sizes.sh"; then
    echo "" >&2
    echo "pre-commit: file-size guard failed (see above)." >&2
    echo "  Split the offending file by concern, or run --update if it legitimately shrank." >&2
    echo "  Bypass:   git commit --no-verify" >&2
    exit 1
fi

# --- Function-size guard: whole tree, same deal as the file-size guard above (FRO78).
if ! bash "$repo_root/scripts/check-function-sizes.sh"; then
    echo "" >&2
    echo "pre-commit: function-size guard failed (see above)." >&2
    echo "  Extract a named step / collaborator, or run --update if it legitimately shrank." >&2
    echo "  Bypass:   git commit --no-verify" >&2
    exit 1
fi

exit 0
