#!/usr/bin/env bash
#
# ci-install-linux-deps.sh — install the Linux build dependencies, surviving a sick apt mirror.
#
# WHY: GitHub's ubuntu runners resolve the archive through /etc/apt/apt-mirrors.txt, which lists
# azure.archive.ubuntu.com first and archive.ubuntu.com as the fallback. When the Azure mirror is
# degraded, apt does NOT fail fast — its default Acquire timeout is 120 s with retries, applied per
# index and per package, so ~30 packages turn into a multi-minute or multi-hour stall that ends in
# a *successful* build. On 2026-08-19 this step went 19 s → 2m12s → 3m36s → 4m38s → 18 min+ across
# a single morning as the mirror got sicker, and on 2026-08-18 one job sat in it for SIX HOURS
# (job 95777163249, 15:53 → 21:54). The log signature is a run of
# `Ign: http://azure.archive.ubuntu.com/... InRelease` lines followed by a `Hit:` on
# archive.ubuntu.com — apt burning its whole retry budget before falling over to the mirror that
# works.
#
# So: cap how long apt may stall, and if it does, drop the Azure mirror and retry against the
# fallback directly. The healthy path is untouched — a working Azure mirror is genuinely faster
# (same datacenter), which is why this switches on failure instead of hard-coding the fallback.
#
# Everything the script touches is overridable so the failover path is testable off-CI without
# root, apt, or a network (see scripts/tests/ci-install-linux-deps.test.sh):
#   SUDO              command prefix for privileged calls (default "sudo"; "" in tests)
#   APT_GET           apt-get binary (default "apt-get")
#   TIMEOUT_BIN       timeout binary (default "timeout"; "" disables the cap — macOS has none)
#   MIRROR_FILES      space-separated files to rewrite when dropping the mirror
#   UPDATE_TIMEOUT    seconds `apt-get update` may take before it counts as stalled (default 180)
#   INSTALL_TIMEOUT   seconds `apt-get install` may take before it counts as stalled (default 600)

set -euo pipefail

SUDO="${SUDO-sudo}"
APT_GET="${APT_GET:-apt-get}"
TIMEOUT_BIN="${TIMEOUT_BIN-timeout}"
MIRROR_FILES="${MIRROR_FILES:-/etc/apt/apt-mirrors.txt}"
UPDATE_TIMEOUT="${UPDATE_TIMEOUT:-180}"
INSTALL_TIMEOUT="${INSTALL_TIMEOUT:-600}"

# Keep this list in sync with the ASAN job's cache-apt-pkgs-action package list in ci.yml.
PACKAGES=(
    cmake ninja-build libasound2-dev libx11-dev libxinerama-dev libxext-dev libxcomposite-dev
    libxcursor-dev libxrandr-dev libxrender-dev libfontconfig1-dev libfreetype6-dev
    libglu1-mesa-dev libcurl4-openssl-dev libgtk-3-dev libjack-jackd2-dev freeglut3-dev
    pkg-config llvm clang ccache xvfb
)

# 15 s per attempt with 2 retries, instead of apt's 120 s default: enough for a slow-but-alive
# mirror, short enough that a dead one fails over in seconds rather than minutes.
APT_OPTS=(-o Acquire::Retries=2 -o Acquire::http::Timeout=15 -o Acquire::https::Timeout=15)

annotate() { printf '::%s::%s\n' "$1" "$2"; }

# Run apt under the timeout, as root, so the timeout kills apt itself and not an intervening sudo.
apt_run() { # apt_run <seconds> <apt-get args...>
    local secs="$1"
    shift
    if [ -n "$TIMEOUT_BIN" ]; then
        ${SUDO:+$SUDO} "$TIMEOUT_BIN" "$secs" "$APT_GET" "${APT_OPTS[@]}" "$@"
    else
        ${SUDO:+$SUDO} "$APT_GET" "${APT_OPTS[@]}" "$@"
    fi
}

# Point the mirrorlist at https://archive.ubuntu.com. Note the scheme: the runner image lists the
# Azure mirror over plain HTTP, while the fallback it falls back TO is already HTTPS, so the rewrite
# upgrades the transport as well as changing the host. Package integrity does not depend on it (apt
# verifies signatures either way), but there is no reason to fetch over cleartext when the mirror
# serves TLS. The mirror files are image details that have moved before, so a missing one is not an
# error — the retry still gets its chance either way.
#
# Deliberately NOT `sed -i`: that flag takes a mandatory backup suffix on BSD sed and no argument on
# GNU sed, so the same command edits the file on the runner and silently does nothing on a macOS
# dev box — which is exactly what happened, and the `|| continue` here hid it until the tests
# caught it. Read with sed, write back through a temp file, and report a failed rewrite instead of
# swallowing it.
drop_azure_mirror() {
    local f tmp found=0
    for f in $MIRROR_FILES; do
        [ -f "$f" ] || continue
        tmp="$(mktemp)"
        if sed -E 's|(https?://)?azure\.archive\.ubuntu\.com|https://archive.ubuntu.com|g' \
            "$f" >"$tmp" && ${SUDO:+$SUDO} cp "$tmp" "$f"; then
            printf 'rewrote %s to https://archive.ubuntu.com\n' "$f"
            found=1
        else
            annotate warning "Could not rewrite ${f} to drop the failing mirror."
        fi
        rm -f "$tmp"
    done
    [ "$found" -eq 1 ] || annotate warning "No apt mirror list found in: ${MIRROR_FILES}. \
Cannot drop the failing mirror; retrying against the existing configuration."
}

if ! apt_run "$UPDATE_TIMEOUT" update; then
    annotate warning "apt-get update failed or exceeded ${UPDATE_TIMEOUT}s — the usual cause is a \
degraded azure.archive.ubuntu.com on the runner. Dropping that mirror and retrying."
    drop_azure_mirror
    # A second failure is NOT fatal on its own: the runner image ships usable indexes, so the
    # install below may well succeed without a refresh. Install is the step that has to work, so
    # let it be the one that decides the exit status.
    apt_run "$UPDATE_TIMEOUT" update ||
        annotate warning "apt-get update failed again; continuing to install with the indexes \
already on the image."
fi

if ! apt_run "$INSTALL_TIMEOUT" install -y "${PACKAGES[@]}"; then
    annotate warning "apt-get install failed or exceeded ${INSTALL_TIMEOUT}s. Dropping the Azure \
mirror (if still configured), refreshing indexes and retrying once."
    drop_azure_mirror
    apt_run "$UPDATE_TIMEOUT" update || true
    apt_run "$INSTALL_TIMEOUT" install -y "${PACKAGES[@]}"
fi
