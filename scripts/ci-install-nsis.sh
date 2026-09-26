#!/usr/bin/env bash
#
# ci-install-nsis.sh -- install NSIS on the Windows release runner, surviving a Chocolatey outage.
#
# WHY: build-artifacts.yml's "Install NSIS" step was a bare `choco install nsis -y` with no retry,
# no fallback and no ceiling, so one upstream blip failed the whole Windows release. On 2026-09-16
# community.chocolatey.org answered 503 for the nsis package; choco "installed 0/0 packages" and
# EXITED 0, the step's own makensis-not-found guard then exited 1, and that skipped Tag and
# Release plus both appcast jobs on a main-branch release build (FRO104). A re-run succeeded with
# no code change: purely transient. Same class of failure as the apt mirror
# (scripts/ci-install-linux-deps.sh), so the same shape of fix: bounded retries with a per-attempt
# timeout, then a direct download of the installer pinned by version AND checksum when the feed
# stays down. Success is judged by makensis.exe existing afterwards, never by choco's exit status,
# because that 0/0 run proved the exit status lies.
#
# Runs under `shell: bash` (Git for Windows) on the runner, and under plain bash off-CI for its
# tests -- everything it touches is overridable so the outage path is testable without choco,
# Windows, or a network (see scripts/tests/ci-install-nsis.test.sh):
#   CHOCO                 choco binary (default "choco")
#   CHOCO_ATTEMPTS        how many times to try choco before falling back (default 3)
#   CHOCO_TIMEOUT         seconds one choco attempt may take (default 120: a healthy install is
#                         under a minute, and 3 attempts plus backoff must finish well inside the
#                         step's 15-minute ceiling with time left for the fallback download)
#   CHOCO_BACKOFF         seconds to wait after a failed attempt, doubled each time (default 10)
#   TIMEOUT_BIN           timeout binary (default "timeout"; "" disables the per-attempt cap)
#   CURL                  curl binary (default "curl")
#   NSIS_VERSION          pinned fallback version (default below)
#   NSIS_URL              fallback installer URL (default: that version on SourceForge)
#   NSIS_SHA256           expected SHA-256 of the fallback installer -- a mismatch is fatal, the
#                         installer is never run unverified
#   NSIS_INSTALLER_RUN    command that runs the downloaded installer silently, given its path
#                         (default: PowerShell Start-Process -Wait when available, else direct)
#   NSIS_SEARCH_ROOTS     ';'-separated directories searched for makensis.exe
#   GITHUB_PATH           where the resolved makensis directory is appended (printed if unset)
#
# The pin below is NSIS 3.12, the release Chocolatey served when this was written. Its checksum
# was taken from two independent SourceForge mirrors and matched against the SHA-1/MD5 published
# on the project's file listing. Bump both lines together.

set -euo pipefail

CHOCO="${CHOCO:-choco}"
CHOCO_ATTEMPTS="${CHOCO_ATTEMPTS:-3}"
CHOCO_TIMEOUT="${CHOCO_TIMEOUT:-120}"
CHOCO_BACKOFF="${CHOCO_BACKOFF:-10}"
TIMEOUT_BIN="${TIMEOUT_BIN-timeout}"
CURL="${CURL:-curl}"
NSIS_VERSION="${NSIS_VERSION:-3.12}"
NSIS_URL="${NSIS_URL:-https://downloads.sourceforge.net/project/nsis/NSIS%203/${NSIS_VERSION}/nsis-${NSIS_VERSION}-setup.exe}"
NSIS_SHA256="${NSIS_SHA256:-3bc2b06253a7e4957111be152ac6a536e0c7478a706e19da814038db5d706495}"
NSIS_INSTALLER_RUN="${NSIS_INSTALLER_RUN:-}"
NSIS_SEARCH_ROOTS="${NSIS_SEARCH_ROOTS:-/c/Program Files (x86)/NSIS;/c/Program Files/NSIS}"

annotate() { printf '::%s::%s\n' "$1" "$2"; }

# find_makensis -- prints the directory holding makensis.exe under the search roots, or nothing.
find_makensis() {
    local root hit
    local IFS=';'
    for root in $NSIS_SEARCH_ROOTS; do
        [ -d "$root" ] || continue
        hit="$(find "$root" -name makensis.exe -type f 2>/dev/null | head -n 1)"
        if [ -n "$hit" ]; then
            dirname "$hit"
            return 0
        fi
    done
    return 1
}

# publish_makensis <dir> -- the one channel Actions re-reads before each step. `choco install` only
# updates the machine registry PATH, which a running job never re-reads (docs/development/releases.md).
publish_makensis() {
    local dir="$1"
    # Later steps are pwsh, which needs a Windows-style path; Git for Windows ships cygpath.
    if command -v cygpath >/dev/null 2>&1; then
        dir="$(cygpath -w "$dir")"
    fi
    if [ -n "${GITHUB_PATH:-}" ]; then
        printf '%s\n' "$dir" >>"$GITHUB_PATH"
    fi
    printf 'makensis found in %s\n' "$dir"
}

# choco_attempt -- one bounded `choco install`. Exit status is only used for logging: whether NSIS
# is actually installed is decided by find_makensis afterwards.
choco_attempt() {
    if [ -n "$TIMEOUT_BIN" ]; then
        "$TIMEOUT_BIN" "$CHOCO_TIMEOUT" "$CHOCO" install nsis -y --no-progress
    else
        "$CHOCO" install nsis -y --no-progress
    fi
}

install_via_choco() {
    local attempt=1 backoff="$CHOCO_BACKOFF"
    while [ "$attempt" -le "$CHOCO_ATTEMPTS" ]; do
        if choco_attempt && find_makensis >/dev/null; then
            return 0
        fi
        annotate warning "choco install nsis attempt ${attempt}/${CHOCO_ATTEMPTS} did not leave makensis.exe installed (feed outage, timeout after ${CHOCO_TIMEOUT}s, or an empty 0/0 install)."
        attempt=$((attempt + 1))
        if [ "$attempt" -le "$CHOCO_ATTEMPTS" ] && [ "$backoff" -gt 0 ]; then
            sleep "$backoff"
            backoff=$((backoff * 2))
        fi
    done
    return 1
}

sha256_of() { # sha256_of <file> -- coreutils on the runner and Linux, shasum on macOS.
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

run_installer() { # run_installer <path> -- silent install, waiting for it to finish.
    local exe="$1"
    if [ -n "$NSIS_INSTALLER_RUN" ]; then
        $NSIS_INSTALLER_RUN "$exe"
    elif command -v powershell.exe >/dev/null 2>&1; then
        local winexe="$exe"
        command -v cygpath >/dev/null 2>&1 && winexe="$(cygpath -w "$exe")"
        # A GUI-subsystem installer returns to a shell immediately; Start-Process -Wait does not.
        powershell.exe -NoProfile -NonInteractive -Command \
            "Start-Process -FilePath '$winexe' -ArgumentList '/S' -Wait -NoNewWindow"
    else
        "$exe" /S
    fi
}

install_via_download() {
    local tmp exe actual
    tmp="$(mktemp -d)"
    exe="$tmp/nsis-${NSIS_VERSION}-setup.exe"
    echo "Falling back to the pinned NSIS ${NSIS_VERSION} installer: $NSIS_URL"
    if ! "$CURL" -fsSL --retry 3 --retry-delay 5 --max-time 300 -o "$exe" "$NSIS_URL"; then
        annotate error "Could not download the fallback NSIS installer from ${NSIS_URL}."
        rm -rf "$tmp"
        return 1
    fi
    actual="$(sha256_of "$exe")"
    if [ "$actual" != "$NSIS_SHA256" ]; then
        annotate error "Fallback NSIS installer checksum mismatch: expected ${NSIS_SHA256}, got ${actual}. Refusing to run it. If NSIS_VERSION was bumped, update NSIS_SHA256 with it."
        rm -rf "$tmp"
        return 1
    fi
    echo "Checksum verified; installing silently."
    run_installer "$exe"
    rm -rf "$tmp"
}

if dir="$(find_makensis)"; then
    echo "NSIS already installed."
    publish_makensis "$dir"
    exit 0
fi

if ! install_via_choco; then
    annotate warning "Chocolatey could not install NSIS after ${CHOCO_ATTEMPTS} attempts; using the pinned direct download instead."
    install_via_download || exit 1
fi

if dir="$(find_makensis)"; then
    publish_makensis "$dir"
else
    annotate error "makensis.exe not found under ${NSIS_SEARCH_ROOTS} after installing NSIS."
    exit 1
fi
