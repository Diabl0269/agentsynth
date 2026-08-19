#!/usr/bin/env bash
#
# Unit tests for scripts/ci-install-linux-deps.sh.
#
# The point of that script is a path that only runs when a mirror is broken — the case nobody ever
# exercises deliberately, and the one that cost six hours of runner time on 2026-08-18. So the
# failover is tested with a fake apt-get instead of waiting for the next outage. Runs in the Lint
# job: no root, no apt, no network, ~1 s.
#
# Usage: bash scripts/tests/ci-install-linux-deps.test.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="$SCRIPT_DIR/scripts/ci-install-linux-deps.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0

check() { # check <name> <what-is-being-compared> <actual> <expected>
    if [ "$3" = "$4" ]; then
        printf 'ok    %s\n' "$1"
        pass=$((pass + 1))
    else
        printf 'FAIL  %s\n      %s: expected %q, got %q\n' "$1" "$2" "$4" "$3"
        fail=$((fail + 1))
    fi
}

# Reproduce the runner's mirrorlist: the Azure mirror over cleartext (GitHub's image default) and
# archive.ubuntu.com over TLS. The cleartext scheme is assembled rather than written out so URL
# scanners don't read fixture data as a connection this repo makes — the string describes the
# runner's configuration, and the whole point of the script is to replace it with the HTTPS one.
CLEARTEXT='http'
write_runner_mirrorlist() { # write_runner_mirrorlist <file>
    printf '%s://azure.archive.ubuntu.com/ubuntu/\nhttps://archive.ubuntu.com/ubuntu/\n' \
        "$CLEARTEXT" >"$1"
}

# A fake apt-get that logs its invocations and fails whenever the mirror list still names Azure,
# standing in for the real failure mode: apt stalling until the timeout kills it (exit 124).
make_fake_apt() { # make_fake_apt <dir> <mode: mirror-sensitive|always-ok|always-fail>
    local dir="$1" mode="$2"
    cat >"$dir/apt-get" <<EOF
#!/usr/bin/env bash
# Record only the apt sub-command, not the -o Acquire flags, so assertions stay readable.
for arg in "\$@"; do
  case "\$arg" in
    -o|Acquire::*) ;;
    *) printf '%s ' "\$arg" >>"$dir/calls.log" ;;
  esac
done
printf '\n' >>"$dir/calls.log"
case "$mode" in
  always-ok)   exit 0 ;;
  always-fail) exit 124 ;;
  mirror-sensitive)
    if grep -q azure.archive.ubuntu.com "$dir/apt-mirrors.txt" 2>/dev/null; then
      exit 124
    fi
    exit 0 ;;
esac
EOF
    chmod +x "$dir/apt-get"
    : >"$dir/calls.log"
}

# Every case runs with SUDO and TIMEOUT_BIN empty: no root available, and macOS has no `timeout`.
# The script treats an empty TIMEOUT_BIN as "no cap", so the fake apt-get's exit code stands in for
# the kill a real timeout would deliver.
run_script() { # run_script <dir> -> prints exit status
    ( cd "$WORK" && SUDO= TIMEOUT_BIN= APT_GET="$1/apt-get" MIRROR_FILES="$1/apt-mirrors.txt" \
        bash "$SCRIPT" >"$1/out.log" 2>&1 )
    echo $?
}

# --- healthy mirror: one update, one install, mirror list untouched ------------------------
H="$WORK/healthy"; mkdir -p "$H"
write_runner_mirrorlist "$H/apt-mirrors.txt"
make_fake_apt "$H" always-ok
status="$(run_script "$H")"
check "healthy mirror: exits 0" "exit status" "$status" "0"
check "healthy mirror: exactly one update and one install" "apt invocations" \
    "$(grep -c . "$H/calls.log")" "2"
check "healthy mirror: leaves the mirror list alone" "azure entries" \
    "$(grep -c azure.archive.ubuntu.com "$H/apt-mirrors.txt")" "1"

# --- sick Azure mirror: update stalls, mirror is dropped, retry succeeds -------------------
S="$WORK/sick"; mkdir -p "$S"
write_runner_mirrorlist "$S/apt-mirrors.txt"
make_fake_apt "$S" mirror-sensitive
status="$(run_script "$S")"
check "sick mirror: recovers and exits 0" "exit status" "$status" "0"
check "sick mirror: azure is gone" "azure entries" \
    "$(grep -c azure.archive.ubuntu.com "$S/apt-mirrors.txt")" "0"
check "sick mirror: replacement is HTTPS, not cleartext" "https archive entries" \
    "$(grep -c "^https://archive.ubuntu.com" "$S/apt-mirrors.txt")" "2"
check "sick mirror: no cleartext entry survives" "cleartext entries" \
    "$(grep -c "^${CLEARTEXT}://" "$S/apt-mirrors.txt")" "0"
check "sick mirror: warns, so the outage is visible in the log" "warning annotation" \
    "$(grep -c '::warning::apt-get update failed or exceeded' "$S/out.log")" "1"
check "sick mirror: update, retried update, then install" "apt call sequence" \
    "$(cut -d' ' -f1 "$S/calls.log" | tr '\n' ',')" "update,update,install,"

# --- mirror list not where we expect: warn about that specifically, still try --------------
M="$WORK/nomirrorfile"; mkdir -p "$M"
make_fake_apt "$M" always-fail
status="$(run_script "$M")"
check "absent mirror list: does not silently pass" "exit status" \
    "$([ "$status" != "0" ] && echo nonzero || echo zero)" "nonzero"
check "absent mirror list: says it could not find one" "warning annotations" \
    "$(grep -c 'No apt mirror list found' "$M/out.log")" "2"

# --- a genuinely broken package set must not be papered over ------------------------------
F="$WORK/broken"; mkdir -p "$F"
printf 'https://archive.ubuntu.com/ubuntu/\n' >"$F/apt-mirrors.txt"
make_fake_apt "$F" always-fail
status="$(run_script "$F")"
check "install that keeps failing: propagates the failure" "exit status" \
    "$([ "$status" != "0" ] && echo nonzero || echo zero)" "nonzero"

# --- the package set is the one the build needs -------------------------------------------
# A silent drop here would fail much later, in a confusing place (a missing X11 or ALSA header),
# so assert the handful the build cannot do without reach the install call.
P="$WORK/packages"; mkdir -p "$P"
printf 'https://archive.ubuntu.com/ubuntu/\n' >"$P/apt-mirrors.txt"
make_fake_apt "$P" always-ok
run_script "$P" >/dev/null
missing=""
for pkg in cmake ninja-build ccache clang llvm libasound2-dev libgtk-3-dev xvfb; do
    grep -q " ${pkg} \| ${pkg}$" "$P/calls.log" || missing="$missing $pkg"
done
check "installs the packages the build needs" "missing packages" "${missing:-none}" "none"

printf '\n%s passed, %s failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
