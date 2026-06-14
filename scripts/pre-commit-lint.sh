#!/bin/bash
# Pre-commit hook: clang-format lint on staged C/C++ sources.
#
# Mirrors the CI "Lint" job (clang-format --dry-run --Werror over Source/ and
# Tests/) but scoped to the files staged for THIS commit, so it runs fast and
# catches formatting before it ever reaches CI. Installed by
# scripts/install-hooks.sh. Bypass a single commit with: git commit --no-verify
#
# NOTE: CI runs clang-format 18 (ubuntu-24.04). Prefer that major version
# locally to avoid "hook passes but CI fails" drift. (The tree is currently
# clean under clang-format 18-22, which agree on this codebase's style.)

# Staged (added/copied/modified) C/C++ files under Source/ or Tests/.
files=$(git diff --cached --name-only --diff-filter=ACM | grep -E '^(Source|Tests)/.*\.(h|cpp)$' || true)

if [ -z "$files" ]; then
    exit 0
fi

if ! command -v clang-format >/dev/null 2>&1; then
    echo "pre-commit: clang-format not found on PATH." >&2
    echo "            Install it (CI uses clang-format 18) or bypass with: git commit --no-verify" >&2
    exit 1
fi

if ! echo "$files" | xargs clang-format --dry-run --Werror; then
    echo "" >&2
    echo "pre-commit: clang-format violations in staged files (see above)." >&2
    echo "  Fix all:  find Source Tests -name '*.h' -o -name '*.cpp' | xargs clang-format -i" >&2
    echo "  Bypass:   git commit --no-verify" >&2
    exit 1
fi
