#!/usr/bin/env bash
# Tests cmake/ApplyJucePatch.cmake (FetchContent's PATCH_COMMAND for JUCE) against a tiny fixture tree --
# no network, no compiler -- and checks the real cmake/patches/*.patch still applies to (or is already part
# of) a JUCE checkout when one is on disk. docs/development/juce-patches.md.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SCRIPT="$ROOT/cmake/ApplyJucePatch.cmake"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0
check() { # check <description> <command...>
    local desc="$1"
    shift
    if "$@"; then
        echo "PASS: $desc"
        pass=$((pass + 1))
    else
        echo "FAIL: $desc"
        fail=$((fail + 1))
    fi
}

apply() { (cd "$1" && cmake -DPATCH_FILE="$2" -P "$SCRIPT" 2>&1); }
content_is() { [ "$(tr -d '\r' <"$1")" = "$2" ]; }

# --- fixture: a patch turning f.txt "unpatched" into "patched" ----------------------------------
cat >"$WORK/p.patch" <<'PATCH'
Header text before the first diff is ignored by git apply.

diff --git a/f.txt b/f.txt
--- a/f.txt
+++ b/f.txt
@@ -1 +1 @@
-unpatched
+patched
PATCH

mkdir -p "$WORK/tree"
echo "unpatched" >"$WORK/tree/f.txt"
out="$(apply "$WORK/tree" "$WORK/p.patch")"
check "a fresh tree is patched" content_is "$WORK/tree/f.txt" "patched"
check "the first run says it applied" grep -q "Applied JUCE patch" <<<"$out"

out="$(apply "$WORK/tree" "$WORK/p.patch")"
check "a second run succeeds" [ $? -eq 0 ]
check "a second run leaves the patched file alone" content_is "$WORK/tree/f.txt" "patched"
check "a second run says it is already applied" grep -q "already applied" <<<"$out"

# CRLF checkout of the same file (JUCE's own files use CRLF): still applies, still idempotent.
printf 'unpatched\r\n' >"$WORK/tree/f.txt"
apply "$WORK/tree" "$WORK/p.patch" >/dev/null
check "a CRLF tree is patched" content_is "$WORK/tree/f.txt" "patched"
apply "$WORK/tree" "$WORK/p.patch" >/dev/null
check "a CRLF tree is idempotent too" [ $? -eq 0 ]

# A tree the patch no longer matches is a hard error, not a silent skip.
echo "something else" >"$WORK/tree/f.txt"
apply "$WORK/tree" "$WORK/p.patch" >/dev/null
check "a drifted tree fails" [ $? -ne 0 ]
apply "$WORK/tree" "$WORK/missing.patch" >/dev/null
check "a missing patch file fails" [ $? -ne 0 ]

# --- the real patches against a JUCE checkout, when one is on disk -------------------------------
juce_src=""
for candidate in "${JUCE_SRC:-}" "$ROOT/build-ci-local/_deps/juce-src" "$ROOT/build/_deps/juce-src"; do
    if [ -n "$candidate" ] && [ -d "$candidate/modules/juce_audio_processors" ]; then
        juce_src="$candidate"
        break
    fi
done
if [ -n "$juce_src" ]; then
    for patch in "$ROOT"/cmake/patches/*.patch; do
        copy="$WORK/juce-copy"
        rm -rf "$copy"
        mkdir -p "$copy"
        # Only the files the patch touches: the rest of JUCE is irrelevant to whether it applies.
        for f in $(grep '^+++ b/' "$patch" | sed 's#^+++ b/##'); do
            mkdir -p "$copy/$(dirname "$f")"
            cp "$juce_src/$f" "$copy/$f"
        done
        # Start from the unpatched state whether or not the checkout was already patched.
        (cd "$copy" && git apply --reverse --ignore-whitespace "$patch" >/dev/null 2>&1)
        apply "$copy" "$patch" >/dev/null
        check "$(basename "$patch") applies to the JUCE checkout at $juce_src" [ $? -eq 0 ]
        apply "$copy" "$patch" >/dev/null
        check "$(basename "$patch") is idempotent on that checkout" [ $? -eq 0 ]
    done
else
    echo "SKIP: no JUCE checkout on disk (set JUCE_SRC or build once); real patch not checked here"
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
