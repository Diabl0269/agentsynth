#!/bin/bash
set -e

# Resolve the hooks directory. `git rev-parse --git-path hooks` returns the
# correct location in plain clones AND in worktrees (hooks are shared via the
# common git dir, which is where git actually executes them from).
HOOK_DIR="$(git rev-parse --git-path hooks)"
mkdir -p "$HOOK_DIR"

# pre-commit: fast clang-format lint on staged C/C++ files (mirrors CI Lint).
cat > "$HOOK_DIR/pre-commit" << 'EOF'
#!/bin/bash
exec "$(git rev-parse --show-toplevel)/scripts/pre-commit-lint.sh"
EOF
chmod +x "$HOOK_DIR/pre-commit"

# pre-push: clang-format lint + Release build + tests.
cat > "$HOOK_DIR/pre-push" << 'EOF'
#!/bin/bash
exec "$(git rev-parse --show-toplevel)/scripts/pre-push-release-test.sh"
EOF
chmod +x "$HOOK_DIR/pre-push"

echo "Installed git hooks:"
echo "  pre-commit -> scripts/pre-commit-lint.sh        (clang-format lint, staged files)"
echo "  pre-push   -> scripts/pre-push-release-test.sh  (lint + Release build + tests)"
