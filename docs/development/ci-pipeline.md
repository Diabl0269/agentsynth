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
| **Lint** | `ubuntu-latest` | — | Installs the pinned `clang-format` PyPI wheel (version from `.clang-format-version`) via `pip install "clang-format==$(cat .clang-format-version)"` after `actions/setup-python`; runs `--dry-run --Werror` over `Source/` and `Tests/`, then every content guard: [ASCII literals](ascii-literal-guard.md), [file size](file-size-guard.md), [function size](function-size-guard.md), [header comment placement](header-comment-guard.md) and [docs integrity](docs-guard.md). Fast (~30 s) — gives formatting feedback without waiting for a full build. |
| **Build, Test, and Coverage** | `ubuntu-latest` | Debug + clang + `ENABLE_COVERAGE=ON` | Runs tests, then `bash scripts/coverage.sh --report-only` (skips the re-build; only merges profdata and checks the 85% line-coverage threshold). |
| **Build and Test (ASAN)** | `ubuntu-latest` | `RelWithDebInfo` + `-fsanitize=address` | **Label-gated** — only runs when the PR carries the `run-asan` label. `ASAN_OPTIONS=detect_leaks=0`. There is no ThreadSanitizer job; see [`test-patterns.md`](test-patterns.md#sanitizers). |
| **Build and Test (macOS)** | `macos-latest` | Release | Catches UB, segfaults and cross-platform issues. |
| **Build and Test (Windows)** | `windows-latest` | Release | Catches UB, segfaults and cross-platform issues. |

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
one — Linux apt, macOS brew, the ASAN job's cached-apt action — because a package manager with no
ceiling stalls until the 6-hour job limit instead of failing. Failover itself is covered by
`scripts/tests/ci-install-linux-deps.test.sh` against a fake `apt-get`: a path that only runs during
an outage otherwise gets tested by the outage. One of those cases exists because `sed -i` takes a
mandatory backup suffix on BSD sed and none on GNU sed, so the original rewrite edited the file in
CI and silently did nothing on macOS.

## What did not work

- **Unity builds** (`CMAKE_UNITY_BUILD`) — incompatible with JUCE: Objective-C++ `.mm` files cannot
  be merged into C++ unity translation units.
- **Precompiled headers** — JUCE module `.cpp` files guard against being pre-included, and on macOS
  `.mm` files also require Objective-C++ mode, which conflicts with a C++ PCH.
