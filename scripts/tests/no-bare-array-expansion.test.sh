#!/usr/bin/env bash
# Guard: no script that runs under `set -u` expands a whole array as a bare "${NAME[@]}".
# macOS's /bin/bash is 3.2, where that expansion on an EMPTY array is an unbound-variable error under
# nounset; it kills the script (even inside `|| true`) after every check has already passed. Write
# ${NAME[@]+"${NAME[@]}"} instead: identical for a non-empty array, expands to nothing for an empty one.
# "$@", "${#NAME[@]}" and "${NAME[i]}" are safe and not flagged; comment lines are skipped.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

passed=0
failed=0

# Self-check: the guarded form must survive an empty array under /bin/bash -u on this machine.
if out="$(/bin/bash -u -c 'arr=(); n=0; for x in ${arr[@]+"${arr[@]}"}; do n=$((n+1)); done; printf "%s" "[$n]"; set -- ${arr[@]+"${arr[@]}"}; printf "%s" "[$#]"' 2>&1)" && [ "$out" = "[0][0]" ]; then
    echo "PASS: the guarded form expands an empty array under /bin/bash -u"
    passed=$((passed + 1))
else
    echo "FAIL: \${arr[@]+\"\${arr[@]}\"} failed on an empty array under /bin/bash -u: $out"
    failed=$((failed + 1))
fi

hits=""
while IFS= read -r f; do
    grep -qE '^[[:space:]]*set[[:space:]]+(-[a-zA-Z]*u|-o[[:space:]]+nounset)' "$f" || continue
    found="$(grep -nE '"\$\{[A-Za-z_][A-Za-z_0-9]*\[@\]\}"' "$f" | grep -vE '^[0-9]+:[[:space:]]*#' |
        grep -E '(^|[^+])"\$\{[A-Za-z_][A-Za-z_0-9]*\[@\]\}"' | sed "s|^|$f:|" || true)"
    [ -n "$found" ] && hits="${hits}${found}"$'\n'
done < <(find scripts -name '*.sh' | sort)

if [ -n "$hits" ]; then
    echo "FAIL: bare \"\${NAME[@]}\" in a set -u script (bash 3.2 treats an empty array as unbound; write \${NAME[@]+\"\${NAME[@]}\"}):"
    printf '%s' "$hits"
    failed=$((failed + 1))
else
    echo "PASS: no bare \"\${NAME[@]}\" expansion in set -u scripts under scripts/"
    passed=$((passed + 1))
fi

echo
echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
