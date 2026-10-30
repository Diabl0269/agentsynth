#!/usr/bin/env bash
#
# Tests scripts/lib/changed-tests.py against a fixture repository and a fake `ninja -t deps`.

set -uo pipefail
HELPER="$(cd "$(dirname "${BASH_SOURCE[0]}")/../lib" && pwd)/changed-tests.py"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
failures=0
expect() { # expect <name> <wanted> <got>
    if [ "$2" = "$3" ]; then echo "ok: $1"; else echo "FAIL: $1: wanted '$2', got '$3'"; failures=$((failures + 1)); fi
}

mkdir -p "$TMP/bin" "$TMP/repo/Source" "$TMP/repo/Tests" "$TMP/repo/build"
cat >"$TMP/bin/ninja" <<'FAKE'
#!/usr/bin/env bash
cat <<'DEPS'
Tests/CMakeFiles/Tests.dir/A.cpp.o: #deps 2, deps mtime 1 (VALID)
    /Applications/SDKSettings.json
    ../Tests/A.cpp
    ../Source/Wide.h
Tests/CMakeFiles/Tests.dir/B.cpp.o: #deps 2, deps mtime 1 (VALID)
    ../Tests/B.cpp
    ../Source/Other.h
CMakeFiles/Core.dir/Source/Unrelated.cpp.o: #deps 2, deps mtime 1 (VALID)
    ../Source/Unrelated.cpp
    ../Source/Lone.h
DEPS
FAKE
chmod +x "$TMP/bin/ninja"
cd "$TMP/repo" || exit 1
git init -q
printf 'TEST(ASuite, One) {}\nTEST_F(AFixture, Two) {}\n' >Tests/A.cpp
printf 'TEST(BSuite, One) {}\nINSTANTIATE_TEST_SUITE_P(Each, BParam, x);\n' >Tests/B.cpp
for i in 1 2 3 4 5 6; do printf 'TEST(Extra%s, One) {}\n' "$i" >"Tests/E$i.cpp"; done
touch Source/Wide.h Source/Wide.cpp Source/Other.h Source/Lone.h CMakeLists.txt
git add -A
git -c user.name=t -c user.email=t@t commit -qm base
base="$(git rev-parse HEAD)"

run() { PATH="$TMP/bin:$PATH" python3 "$HELPER" --build build --base "$base" "$@"; }
reset() { git checkout -q -- . && git clean -qfd Tests Source; }

expect "no change reaches nothing" NONE "$(run)"

echo x >>Source/Wide.h
expect "a header reaches the suites of the tests including it" "AFixture.*:ASuite.*" "$(run)"
reset

echo x >>Source/Wide.cpp
expect "a source counts as its same-named header" "AFixture.*:ASuite.*" "$(run)"
reset

echo x >>Tests/B.cpp
expect "a changed test reaches its own suites, parameterised ones too" "BSuite.*:Each/BParam/*" "$(run)"
reset

echo x >>Source/Lone.h
expect "a header only production code includes reaches no test" NONE "$(run)"
reset

echo x >>CMakeLists.txt
expect "a build input means everything" ALL "$(run)"
reset

echo x >>Source/Wide.h
echo x >>Source/Other.h
expect "reaching too much of the tests means everything" ALL "$(run --max-share 0.1)"
reset

[ "$failures" -eq 0 ] && echo "changed-tests: all passed" || exit 1
