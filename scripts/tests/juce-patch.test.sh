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

# A git tree (FetchContent's JUCE clone) still carrying an OLDER revision of the patch -- what a CI cache restored by
# key prefix hands over after the patch was edited: the new revision replaces it instead of failing.
cat >"$WORK/p2.patch" <<'PATCH'
diff --git a/f.txt b/f.txt
--- a/f.txt
+++ b/f.txt
@@ -1 +1 @@
-unpatched
+patched again
PATCH
mkdir -p "$WORK/gtree"
(cd "$WORK/gtree" && git init -q && echo "unpatched" >f.txt && echo "other patch" >g.txt && git add . &&
    git -c user.email=t@t -c user.name=t commit -qm pin && echo "patched" >f.txt && echo "kept" >g.txt)
out="$(apply "$WORK/gtree" "$WORK/p2.patch")"
check "an older revision of the patch in a git tree is replaced" content_is "$WORK/gtree/f.txt" "patched again"
check "it says it reset and re-applied" grep -q "re-applied" <<<"$out"
check "files the patch doesn't touch are left alone" content_is "$WORK/gtree/g.txt" "kept"
apply "$WORK/gtree" "$WORK/p2.patch" >/dev/null
check "the replaced patch is then idempotent" [ $? -eq 0 ]

# In a git tree whose pinned file itself no longer matches (a new JUCE tag), it is still a hard error.
(cd "$WORK/gtree" && echo "moved on" >f.txt && git -c user.email=t@t -c user.name=t commit -qam tag)
apply "$WORK/gtree" "$WORK/p2.patch" >/dev/null
check "a git tree whose pinned file drifted still fails" [ $? -ne 0 ]

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

# --- cmake/CheckJuceSourceDir.cmake: a source dir must already carry every patch -----------------
source_dir_check() { cmake -DJUCE_SRC="$1" -DPATCH_DIR="$2" -P "$ROOT/cmake/CheckJuceSourceDir.cmake" 2>&1; }
mkdir -p "$WORK/patches" "$WORK/src-old" "$WORK/src-new"
cp "$WORK/p.patch" "$WORK/patches/"
echo "unpatched" >"$WORK/src-old/f.txt"
echo "patched" >"$WORK/src-new/f.txt"
out="$(source_dir_check "$WORK/src-old" "$WORK/patches")"
check "a source dir without the patch fails the check" [ $? -ne 0 ]
check "the failure names the missing patch" grep -q "p.patch" <<<"$out"
source_dir_check "$WORK/src-new" "$WORK/patches" >/dev/null
check "a source dir with the patch passes the check" [ $? -eq 0 ]

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
