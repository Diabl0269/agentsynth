# CI Pipeline

`.github/workflows/ci.yml` runs on pull requests to `main` **and on pushes to `main`**,
path-filtered to changes under `Source/**`, `Tests/**`, `Tools/**`, `CMakeLists.txt`,
`Tests/CMakeLists.txt`, `cmake/**`, `scripts/**`, `.clang-format`, `.clang-format-version`, or
either workflow file (`ci.yml` / `build-artifacts.yml`).

The caches these jobs live on are in [`ci-caching.md`](ci-caching.md); the local reproduction is
[`local-ci.md`](local-ci.md); the post-merge release build is [`releases.md`](releases.md).

## Triggers

**The push-to-`main` trigger exists to seed the build caches.** Removing it silently doubles every
PR's build time, so it is load-bearing, not redundant with the artifact workflow — see
[`ci-caching.md`](ci-caching.md#six-rules-each-learned-from-an-outage), rule 1.

`ci.yml` also has a `workflow_dispatch` trigger for exactly one purpose: re-seeding a cache by hand.
PR runs restore only and never save, so if main's cache for an OS is ever evicted or 7-day-GC'd, no
PR can fix that itself — run "Agent Synth CI" via workflow_dispatch on `main` (Actions tab → select
the workflow → Run workflow, branch `main`) to seed it without waiting for a real commit. It is
build-only on any ref: `lint`, `Run Tests` and `Check Coverage` all gate on `pull_request`, and
`CACHE_WARM_EXPECTED` reads false for `workflow_dispatch`, so a cold cache during the run itself is
reported as expected rather than a failure. It only *saves* when run on `main`.

**Push runs build only.** Linting, test execution and the coverage check are gated to
`pull_request`.

**Why.** They already ran on the PR against this same tree, so repeating them on the merge result
costs runner minutes without adding signal. It does not weaken the cache, because `cmake --build
build` still compiles the `Tests` target — only *running* the binary is skipped, so every test
translation unit still lands in ccache. It trims a push run from about 9.4 to about 6 runner-minutes;
the macOS job was the worst offender, an 8-second cached build carrying 118 seconds of tests. What
it gives up: a post-merge run no longer catches two PRs that are each green alone but conflict once
both land. The exposure is small — the next PR's CI runs against the merged result, and the release
build still fails if the merge does not compile.

A `concurrency` group cancels superseded runs on the same PR. Main is never cancelled, since those
runs are what seed the cache.

## Jobs

| Job | Runner | Config | Notes |
|-----|--------|--------|-------|
| **Lint** | `ubuntu-latest` | — | Installs the pinned `clang-format` PyPI wheel (version from `.clang-format-version`) via `pip install "clang-format==$(cat .clang-format-version)"` after `actions/setup-python`; runs `--dry-run --Werror` over `Source/` and `Tests/`, then every content guard: [ASCII literals](ascii-literal-guard.md), [file size](file-size-guard.md), [function size](function-size-guard.md), [header comment placement](header-comment-guard.md), [comment provenance](comment-provenance-guard.md) and [docs integrity](docs-guard.md). Fast (~30 s) — gives formatting feedback without waiting for a full build. |
| **Build, Test, and Coverage** | `ubuntu-latest` | Debug + clang + `ENABLE_COVERAGE=ON` | Runs tests, then `bash scripts/coverage.sh --report-only` (skips the re-build; only merges profdata and checks the 85% line-coverage threshold). |
| **Build and Test (ASAN)** | `ubuntu-latest` | `RelWithDebInfo` + `-fsanitize=address` | **Label-gated** — only runs when the PR carries the `run-asan` label. `ASAN_OPTIONS=detect_leaks=0`. Has its own PR-scoped ccache (the one exception to "PR runs never save", see [`ci-caching.md`](ci-caching.md#six-rules-each-learned-from-an-outage) rule 6): the first labelled run of a PR is cold (~48 min), later pushes to it are warm. There is no ThreadSanitizer job; see [`test-patterns.md`](test-patterns.md#sanitizers). |
| **Build and Test (macOS)** | `macos-latest` | Release | Catches UB, segfaults and cross-platform issues. Its "Run Tests" step (FRO305) runs the suite as 3 concurrent `GTEST_TOTAL_SHARDS`/`GTEST_SHARD_INDEX` shards — one per the runner's vCPU — cutting ~8m43s serial to about 3 minutes; each shard gets its own `AGENTSYNTH_SETTINGS_DIR` (see [`testing.md`](testing.md#ci-sharding-macos)) so concurrent shards don't collide through the one real on-disk settings file. |
| **Build and Test (Windows)** | `windows-latest` | Release | Catches UB, segfaults and cross-platform issues. Its test step runs `Tests.exe` directly and fails on a test failure or a missing binary (FRO242; it used to run it through `find -exec`, which hid both). |

## Required status checks

> **Do not rename the `Lint`, `Build, Test, and Coverage`, `Build and Test (macOS)`, `Build and Test
> (Windows)`, `Docs`, or `PR Title` jobs.** Those six strings are configured as required status
> checks in `main`'s branch protection; renaming one leaves a required check permanently pending and
> blocks every merge. Skipping a job on `push` is safe — protection only gates pull requests.
> Renaming it is not.

`Docs` comes from `.github/workflows/docs.yml` and `PR Title` from
`.github/workflows/pr-title.yml`; the other four are `ci.yml`'s own.
`.github/CLAUDE.md` carries the same list beside the workflows themselves.

## Docs-only pull requests

`ci.yml`'s `paths:` filter deliberately excludes `docs/**` and `*.md`.

**Why.** Including them would trigger the full three-platform build matrix for a docs-only change,
which is exactly what the filter exists to avoid. The consequence is that `ci.yml`'s four jobs —
including Lint, and every guard that rides in it — never run for a docs-only PR, and four required
checks would sit permanently pending.

Two workflows close that:

- **`.github/workflows/docs.yml`'s "Docs" job** has no `paths:` filter of its own, so it fires on
  every pull request. It runs `scripts/check-docs.sh` and `scripts/check-file-sizes.sh`
  unconditionally, so a docs-only PR is gated in CI, not only by the pre-commit hook and
  `scripts/ci-local.sh` (which still run both guards against the whole tree, catching a violation
  before it is ever pushed).
- **`.github/workflows/ci-passthrough.yml`** posts a synthetic success under `ci.yml`'s four
  required job names whenever `ci.yml`'s own `paths:` filter does not match, so a docs-only PR's
  required checks all post a real status and it merges normally — no `gh pr merge --admin` override,
  and a red Docs job actually blocks the merge it should.

A mixed code-and-docs PR is gated by both. This repo's convention requires updating docs in the same
PR as the behaviour change, so that is the common case, not the rare one — it produces a duplicate
check run under each of the four shared job names. See `ci-passthrough.yml`'s own header for why
that duplication cannot be removed with a path-filter tweak, and why it is harmless:
`mergeStateStatus` was confirmed live to stay non-`CLEAN` while any run under a required context
name is still non-terminal, so the real job's result is never shadowed by an earlier synthetic
success. Only a raw `gh pr checks`-style listing, or tooling that reads it the same naive way, can
look momentarily misleading during that window.

## Optimizations

- **`JUCE_WEB_BROWSER=0`** — drops the unused `WebBrowserComponent` and removes the WebKit/libsoup
  dependencies on Linux.
- **A separate lint job** — instant formatting feedback without waiting for a full build.
- **`coverage.sh --report-only`** — in CI, skips redundant configure/build/test steps and only merges
  profdata and generates the report.
- **Precompiled JUCE headers on the macOS and Linux jobs** (`-DAGENTSYNTH_PCH=ON`,
  `cmake/Pch.cmake`) — a source file spends most of its compile time parsing the JUCE module
  headers (one heavy UI file measured 3.5 s, 94% of it front end, about 1.6 s of that JUCE), so
  Core, AppUI and Tests each parse them once instead of once per file. Measured in CI as build
  time per file that missed the cache:

  | Job | Without | With | One build's cache | Second build, same job |
  |---|---|---|---|---|
  | macOS | about 2.1 s (median of 14 runs) | 1.0 to 1.2 s | 238 MB (156 MB without) | 100% hits |
  | Linux | 2.0 to 2.5 s | 0.9 to 1.1 s | 578 MB | 100% hits, 63 s |
  | Windows | 2.7 to 4.3 s | 1.2 to 1.6 s | 199 MB | **11% hits** |

  Off everywhere else:
  - **Windows job**: MSVC compiles faster with the header, but the files that use it never hit
    ccache: the second build in the same job recompiled 1,101 of 1,242 files, even with an empty
    4 GB cache. The likely cause is that MSVC writes a different `.pch` each time it builds one
    (three files of about 360 MB each) and ccache hashes that file. The job also catches a file
    that compiles only because the header supplied a missing include.
  - **Local builds**: clang writes the build directory's absolute path into the header, so it is
    only valid at the path that built it. The local ccache is shared between checkouts through
    `base_dir` ([local-ci.md](local-ci.md)), which hands a second checkout the first one's header
    and fails every compile with "malformed or corrupted precompiled file". Neither
    `-fmodule-file-home-is-cwd` nor `-relocatable-pch` removes the path. Configuring with the
    option on while `base_dir` is set is refused.

  What makes it cacheable: C++ only (`$<COMPILE_LANGUAGE:CXX>`), so the `-fobjc-arc` `.mm` files
  never see it; the app and plugin targets are left out; `-Xclang -fno-pch-timestamp`; and ccache
  `sloppiness=pch_defines,time_macros`, set through the compiler launcher's environment.
  `juce_dsp` is left out of the header: pre-included everywhere, its `jmin`/`jmax` overloads for
  `SIMDRegister` make `juce::jmin<juce::int64>` a hard error on Linux.

## Dependency install: the apt mirror is not reliable

The Linux job here, the label-gated ASAN job, and the Linux leg of the release workflow's build
matrix all install their build dependencies through `scripts/ci-install-linux-deps.sh`, not a bare
`apt-get`.

**Why.** GitHub's ubuntu runners resolve the archive through `/etc/apt/apt-mirrors.txt` —
`azure.archive.ubuntu.com` first, `archive.ubuntu.com` as fallback — and when the Azure mirror is
degraded **apt does not fail fast**. Its default `Acquire` timeout is 120 s with retries, applied per
index and per package, so around 30 packages become a multi-minute or multi-hour stall that still
ends in a green build. The log signature is a run of `Ign: http://azure.archive.ubuntu.com/…
InRelease` lines followed by a `Hit:` on `archive.ubuntu.com`: apt burning its whole retry budget
before failing over to the mirror that works. One observed degradation went 19 s, then 2m12s, 3m36s,
4m38s, 18 min+ across a single morning, and one job sat in that step for **six hours** — invisible,
because a slow success looks like a healthy build.

The release workflow used to install its Linux packages through `awalsh128/cache-apt-pkgs-action`
instead. That action resolves and pins the *exact* currently-available package versions up front,
for its cache key, then installs those exact `.deb`s with no retry — so when the Azure mirror's
index had already rotated past one pinned version (a routine point-release bump), the fetch 404'd
and the whole release build failed outright. Same class of mirror flakiness, with no failover at all
instead of a slow one. The ASAN job used the same action until FRO275: its cached restore left out
`libfontconfig1-dev`'s `.pc` file and `.so` symlink, so every labelled run died configuring JUCE's
`juceaide`. All three now share this one script.

The script therefore caps each apt call (15 s per attempt, 2 retries, instead of apt's 120 s
default, and 60 s total for `update` / 300 s for `install`); on a stall, rewrites the mirror list to
`https://archive.ubuntu.com` and retries — note the scheme, since the runner lists Azure over
cleartext while the fallback already serves TLS; and treats a second failed `update` as non-fatal,
letting the *install* decide the exit status, since the image ships usable indexes. The healthy path
is unchanged: a working Azure mirror is genuinely faster, being in the same datacenter, so this
switches on failure rather than hard-coding the fallback.

Those caps come from measurement. Healthy: `update` 5-15 s, `install` around 40 s. During a live
outage the first `update` burned its whole cap, the rewrite took milliseconds, and the retry fetched
10.7 MB in 2 s with `install` fetching 31.4 MB in 2 s — so on a sick mirror the cap *is* the cost,
and it wants to be the smallest value that cannot fire on a slow-but-alive mirror.

`timeout-minutes: 15` on the step is the backstop, not the mechanism. Every package-manager step has
one — Linux apt, macOS brew (here and in the release workflow), the ASAN job's install, and the
release workflow's Windows NSIS install — because a package manager with no ceiling stalls until the
6-hour job limit instead of failing. Failover itself is covered by
`scripts/tests/ci-install-linux-deps.test.sh` against a fake `apt-get`: a path that only runs during
an outage otherwise gets tested by the outage. The Windows leg's Chocolatey install has the same
shape of protection — retries, then a checksum-pinned direct download — in `scripts/ci-install-nsis.sh`,
tested by `scripts/tests/ci-install-nsis.test.sh` against a fake `choco`; see
[`releases.md`](releases.md). One of those cases exists because `sed -i` takes a
mandatory backup suffix on BSD sed and none on GNU sed, so the original rewrite edited the file in
CI and silently did nothing on macOS.

## What did not work

- **Unity builds** (`CMAKE_UNITY_BUILD`) — incompatible with JUCE: Objective-C++ `.mm` files cannot
  be merged into C++ unity translation units.
- **Precompiled headers for local builds and on Windows** — see the precompiled-headers entry
  under Optimizations for why they are limited to the macOS and Linux jobs. Two earlier attempts
  were rejected outright: the first on the belief that JUCE's module `.cpp` and `.mm` files could not
  be handled (they can be skipped), the second because the macOS ccache had no room for the
  extra 171 MB that five per-target headers added, before the Actions cache kept one generation.
