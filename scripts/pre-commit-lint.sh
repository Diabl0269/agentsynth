#!/bin/bash
# Pre-commit hook: clang-format lint on staged C/C++ sources.
#
# Mirrors the CI "Lint" job (clang-format --dry-run --Werror over Source/ and
# Tests/) but scoped to the files staged for THIS commit, so it runs fast and
# catches formatting before it ever reaches CI. Installed by
# scripts/install-hooks.sh. Bypass a single commit with: git commit --no-verify
#
# NOTE: clang-format is PINNED — CI and this hook must run the SAME version.
# The pinned version lives in .clang-format-version (repo root). Install the
# exact binary with:  pip install "clang-format==$(cat .clang-format-version)"
# A version mismatch only WARNS below (the hook still runs) so you know why CI
# might disagree.

# Staged (added/copied/modified) C/C++ files under Source/ or Tests/.
files=$(git diff --cached --name-only --diff-filter=ACM | grep -E '^(Source|Tests)/.*\.(h|cpp)$' || true)

if [ -z "$files" ]; then
    exit 0
fi

if ! command -v clang-format >/dev/null 2>&1; then
    echo "pre-commit: clang-format not found on PATH." >&2
    echo "            Install the pinned version:  pip install \"clang-format==\$(cat .clang-format-version)\"" >&2
    echo "            Or bypass with: git commit --no-verify" >&2
    exit 1
fi

# Warn (don't fail) when the local clang-format differs from the pinned version
# so a developer knows why CI might disagree with a locally-clean tree.
repo_root="$(git rev-parse --show-toplevel)"
pinned="$(cat "$repo_root/.clang-format-version" 2>/dev/null || true)"
have="$(clang-format --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
if [ -n "$pinned" ] && [ "$have" != "$pinned" ]; then
    echo "pre-commit: clang-format $have differs from pinned $pinned (CI uses $pinned)." >&2
    echo "            Match it:  pip install \"clang-format==$pinned\"" >&2
fi

if ! echo "$files" | xargs clang-format --dry-run --Werror; then
    echo "" >&2
    echo "pre-commit: clang-format violations in staged files (see above)." >&2
    echo "  Fix all:  find Source Tests -name '*.h' -o -name '*.cpp' | xargs clang-format -i" >&2
    echo "  Bypass:   git commit --no-verify" >&2
    exit 1
fi
