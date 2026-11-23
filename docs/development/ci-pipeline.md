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
| **Lint** | `ubuntu-latest` | — | Installs the pinned `clang-format` PyPI wheel (version from `.clang-format-version`) via `pip install "clang-format==$(cat .clang-format-version)"` after `actions/setup-python`; runs `--dry-run --Werror` over `Source/` and `Tests/`, then every content guard: [ASCII literals](ascii-literal-guard.md), [file size](file-size-guard.md), [function size](function-size-guard.md), [header comment placement](header-comment-guard.md), [comment provenance](comment-provenance-guard.md), [fixed-time waits in tests](testing.md#waiting-for-async-work), [header reach](local-ci.md#header-reach-guard) and [docs integrity](docs-guard.md). Fast (~30 s) — gives formatting feedback without waiting for a full build. |
| **Build, Test, and Coverage** | `ubuntu-latest` | Debug + clang + `ENABLE_COVERAGE=ON` | First runs the keyboard/focus suites listed in `scripts/ci-early-suites.list` on their own (AGE128: they fail on Windows/Linux only, and the Windows job runs ~50 minutes, so a break there now fails within minutes; add a suite after 3+ PR runs failing it off macOS), then runs tests as 4 parallel shards (`scripts/ci-linux-run-tests.sh`), then `bash scripts/coverage.sh --report-only` (skips the re-build; only merges profdata and checks the 85% line-coverage threshold). |
| **Build and Test (ASAN)** | `ubuntu-latest` | `RelWithDebInfo` + `-fsanitize=address` | **Label-gated** — only runs when the PR carries the `run-asan` label. `ASAN_OPTIONS=detect_leaks=0`. Has its own PR-scoped ccache (the one exception to "PR runs never save", see [`ci-caching.md`](ci-caching.md#six-rules-each-learned-from-an-outage) rule 6): the first labelled run of a PR is cold (~48 min), later pushes to it are warm. The race checker is the separate TSAN job below. **Gotcha:** the post-build `gtest_discover_tests` step runs the Tests binary, so anything ASAN flags during test registration fails the *link* with `FAILED: Tests/Tests[1]_tests.cmake` and no message in the log; reproduce by running `Tests --gtest_list_tests`. On ubuntu-24.04 (libstdc++ 14 + clang) `std::stable_sort`'s temporary buffer (`operator new(nothrow)`, freed on another path) raises a false `alloc-dealloc-mismatch` at every call site (35 in Source/Tests), so the job sets `ASAN_OPTIONS=detect_leaks=0:alloc_dealloc_mismatch=0` job-wide, covering the Build step's discovery run as well as Run Tests. Do not fix it per call site. Run Tests goes through `scripts/ci-asan-run-tests.sh`: 6 sequential GoogleTest shards, each a fresh process with its own settings folder, plus a 30 s memory sampler in the log, because one process for all ~8.5k tests stalled the runner after ~8.5k tests (a 41 s then 38 s test, then the runner was lost). It also runs without `xvfb-run`, like the main Linux job: under a bare Xvfb the tests that open real peers (`addToDesktop`) abort the process on an X `BadAtom`. |
| **Build and Test (TSAN)** | `ubuntu-latest` | `RelWithDebInfo` + `-fsanitize=thread` | **Label-gated** (FRO188) — lives in its own workflow, `.github/workflows/ci-tsan.yml` ("Agent Synth TSAN"; `ci.yml` is at the 1000-line cap), and only runs when the PR carries the `run-tsan` label (adding the label starts a run); **not** a required check (never add it to branch protection). Watch it with `scripts/watch-pr-ci.sh <repo> <pr> "Agent Synth TSAN"`, not the main workflow's name. Never runs on `push: main`; `ci-cache-budget.sh` reads `ci.yml` only, so its ccache is not summed. The machine check for lock-free hand-offs between the audio, MIDI and message threads. `TSAN_OPTIONS=halt_on_error=0:second_deadlock_stack=1:suppressions=scripts/tsan.supp` job-wide, so one run lists every report. Same shape as the ASAN job: own PR-scoped ccache (first labelled run cold, later pushes warm), `scripts/ci-asan-run-tests.sh` with `SANITIZER_LABEL=TSAN` (6 sequential shards, no `xvfb-run`). A `sysctl vm.mmap_rnd_bits=28` step first: on kernels with 32 bits of ASLR entropy a TSan binary dies at start with "unexpected memory mapping". `scripts/tsan.supp` holds only JUCE-internal reports, one comment per entry saying why; a race in `Source/` is fixed, never suppressed. Cheap on the lock-free rings: `synth::SpscRing` (release/acquire) runs about 100x faster under TSan than the old `AbstractFifo` rings. It configures `-DCMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE=PRE_TEST` because the post-link `gtest_discover_tests` run of the binary failed the link under TSan with no message (PR #1325); the job never runs ctest, so nothing needs build-time discovery. `Tests/Modules/Filter/AllocationCounter.cpp` compiles its replacement `operator new/delete` out under TSan (TSan's runtime defines them and the link fails with "multiple definition"), so the "no allocation on the audio thread" checks pass vacuously in this job only. The whole suite takes about 2 hours under TSan (6 shards). First full run (PR #1325): one real race, `InMemoryTokenStore` read on the message thread while the account worker cleared it (fixed with a mutex); `die_after_fork=0` is set because the plugin-scan watchdog test forks after threads exist; a few timing-sensitive tests (two `SoundIndex` ones, `DeviceChangeFlowTest.MidiTakeCommittedAtFormatChange`) fail intermittently under TSan's slowdown (tracked, not suppressed). Expected in later runs: tests that apply a patch while a live device thread renders race construction/`prepareToPlay` against `renderNextBlock` (harness, not engine). |
| **Build and Test (RTSAN)** | `ubuntu-latest` | clang 20 (installed from apt.llvm.org), `RelWithDebInfo` + `-DAGENTSYNTH_RTSAN=ON` (`-fsanitize=realtime`) | **Label-gated** (FRO2187) — own workflow, `.github/workflows/ci-rtsan.yml` ("Agent Synth RTSAN"), runs only when the PR carries the `run-rtsan` label; **not** a required check. Watch it with `scripts/watch-pr-ci.sh <repo> <pr> "Agent Synth RTSAN"`. The machine check that the audio thread never allocates, locks or does I/O: `synth::RealtimeScope` in `AudioEngine::renderNextBlock` (the funnel for the device callback and the hosted `processBlock`) arms the sanitizer, and every violation is logged with its call stack (`halt_on_error=false`), a summary groups them by the unsafe call and the first `Source/` frame, and any report fails the job. Same shape as the TSAN job (PR-scoped ccache, `scripts/ci-asan-run-tests.sh` with `SANITIZER_LABEL=RTSAN`, 6 shards, no `xvfb-run`), minus the ASLR step. `scripts/rtsan.supp` holds only JUCE-internal reports, one comment each; an allocation or lock in `Source/` is fixed, never suppressed. `Tests/Engine/RealtimeScopeTests.cpp` has a death test (an allocation inside a scope must abort with "RealtimeSanitizer") that only exists in this build and proves the job can fail. The first labelled run (PR #1513) logged 6,627 reports in about 90 minutes: the JUCE graph's own locking and allocation under `renderPass` (`AudioEngineRenderPass.cpp`, most of them), plus real ones in `Source/`: a lock in `MidiKeyboardModule::processModuleBlock`, a lock and allocation in `ExternalMidiModule::processModuleBlock`, `MidiBuffer` growth in `TimelineMidiSourceModule::emitRange`, an allocation in `VoiceMixerModule::processModuleBlock` and a `sched_yield` in `ModuleBase::renderThroughOutputStage`. Triaging these (fix in `Source/`, or a commented suppression for JUCE internals) is the follow-up |
| **Compile (GCC)** | `ubuntu-latest` | Release + the runner's default GCC, `Core` and `AppUI` targets only | FRO2067. Lives in its own workflow, `.github/workflows/ci-gcc.yml` ("Agent Synth GCC"; `ci.yml` is at the 1000-line cap). Compile-only, no tests: the clang coverage job cannot see GCC-only errors (FRO2066: a `std::unique_ptr` default member initializer of a forward-declared type), which used to surface only after merge in `build-artifacts.yml`'s Linux build. Same compiler, `scripts/ci-install-linux-deps.sh`, and Release flags as that build, and it restores (never saves) that build's deps and ccache family (`Linux-release-ccache-`), so it is fast once a prerelease has seeded main. `pull_request` only, with a `paths:` filter on `Source/`, `Tools/`, CMake files and the two workflow files; 45 minute timeout (the first run took 10 minutes in `apt-get install` alone, then more than 19 minutes compiling). **Not** a required check (never add it to branch protection); `ci-passthrough.yml` posts synthetic successes for the required names only, so it needs no entry there. Watch it with `scripts/watch-pr-ci.sh <repo> <pr> "Agent Synth GCC"`. `ci-cache-budget.sh` reads `ci.yml` only and the job saves nothing, so no ccache is summed. |
| **Build and Test (macOS)** | `macos-latest` | Release | Catches UB, segfaults and cross-platform issues. Its "Run Tests" step (FRO305) runs the suite as 3 concurrent `GTEST_TOTAL_SHARDS`/`GTEST_SHARD_INDEX` shards — one per the runner's vCPU — cutting ~8m43s serial to about 3 minutes; each shard gets its own `AGENTSYNTH_SETTINGS_DIR` (see [`testing.md`](testing.md#ci-sharding-macos)) so concurrent shards don't collide through the one real on-disk settings file. |
| **Build and Test (Windows)** | `windows-latest` | Release | Catches UB, segfaults and cross-platform issues. Its test step first runs the same early suites (`scripts/ci-early-suites.sh`), then `Tests.exe` directly and fails on a test failure or a missing binary (FRO242; it used to run it through `find -exec`, which hid both). |

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
- **Narrow reach for often-edited headers** — an edit to a header recompiles every file that
  includes it, directly or through another header, on every platform. Headers that are edited
  almost daily stay out of other headers. The shortcut table and command mapping live in `.cpp`
  files. `BottomDockComponent.h` forward-declares `MixerPanelComponent` and
  `TimelinePanelComponent` and holds the mixer through a `unique_ptr`;
  `MixerMirrorController.h` forward-declares `MixerPanelComponent` too.
  `FocusRing.h` keeps its body (and its `AppLookAndFeel.h` include) in `FocusRing.cpp`. A file
  that needs the full class includes it itself. To measure a header, run
  `touch <header> && ninja | grep -c 'Building CXX'` in a built Ninja tree (`ninja -n` does not
  work here). To find out which headers spread it, run `ninja -t deps`.

  | Header | Files recompiled before | After |
  |---|---|---|
  | `MixerPanelComponent.h` | 160 | 56 |
  | `AppLookAndFeel.h` | 633 | 562 |

  Not cut, because each needs a class split rather than an include move:
  - `GraphEditor.h` (348): about 190 `.cpp` files and 130 test files include it directly. The
    largest spreader, `PreferencesSettingsTab.h`, adds only 20 because it uses nested enums.
  - `MainComponent.h` (146): every includer includes it directly.
  - `AppLookAndFeel.h`'s remaining 562: `FocusRegion.h`, `MainComponent.h` and the module view
    headers use the theme for real. Forward-declaring it in the headers that hold only a pointer
    would save 27 more.
- **`coverage.sh --report-only`** — in CI, skips redundant configure/build/test steps and only merges
  profdata and generates the report.
- **Precompiled JUCE headers on all three build jobs** (`-DAGENTSYNTH_PCH=ON`,
  `cmake/Pch.cmake`) — a source file spends most of its compile time parsing the JUCE module
  headers (one heavy UI file measured 3.5 s, 94% of it front end, about 1.6 s of that JUCE), so
  Core, AppUI and Tests each parse them once instead of once per file. Measured in CI as build
  time per file that missed the cache:

  | Job | Without | With | One build's cache | Second build, same job |
  |---|---|---|---|---|
  | macOS | about 2.1 s (median of 14 runs) | 1.0 to 1.2 s | 238 MB (156 MB without) | 100% hits |
  | Linux | 2.0 to 2.5 s | 0.9 to 1.1 s | 578 MB | 100% hits, 63 s |
  | Windows | 2.7 to 4.3 s | 1.2 to 1.7 s | 199 MB | 100% hits, 211 s |

  The Windows job builds with `/Z7` (`CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded`, the only debug-info form ccache
  caches for MSVC) and links `Tests.exe` with `/DEBUG` (`AGENTSYNTH_TEST_PDB`), so the crash reporter in
  `Tests/WindowsCrashReport.h` prints function names instead of `Tests.exe+0x...`. ccache `max_size` is 1500M there for the larger objects.

  Windows needs `/Brepro /experimental:deterministic` on the three targets. Without them the
  second build in the same job recompiled 1,101 of 1,242 files (11% hits), with the cache full
  and again with an empty 4 GB cache: MSVC writes a different `.pch` on every build (three files
  of about 360 MB each), ccache hashes it, and every file that uses the header misses. With the
  two flags the second build hit 100% and took 211 s.

  Off for local builds:
  - **Local builds**: clang writes the build directory's absolute path into the header, so it is
    only valid at the path that built it. The local ccache is shared between checkouts through
    `base_dir` ([local-ci.md](local-ci.md)), which hands a second checkout the first one's header
    and fails every compile with "malformed or corrupted precompiled file". Neither
    `-fmodule-file-home-is-cwd` nor `-relocatable-pch` removes the path. Configuring with the
    option on while `base_dir` is set is refused.
  - Because every CI job now uses the header, a local build is what catches a source file that
    compiles in CI only because the header supplied an include it forgot.

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

A third tier covers both Canonical mirrors being down (2026-11-11, for hours): when `update` also
fails against `archive.ubuntu.com` (or a later `install` does), the script rewrites the list to
`FALLBACK_MIRROR` (default `https://mirrors.edge.kernel.org`) and updates again. Switching at the
update keeps the dead 300 s install attempts out of the step's 15 min ceiling. Install still decides
the exit status. apt still verifies signatures, so the mirror is not trusted.

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

## Unity groups on Windows

The Windows job merges the sources of Core, AppUI and Tests (`AGENTSYNTH_UNITY`, `cmake/Pch.cmake`) because MSVC
is by far the slowest compiler. macOS and Linux keep separate files, so a missing include still fails there.
Groups come from a file's folder and a hash of its name, so a new file changes one group and its cache entry
rather than shifting every later one. About 2,230 compiles become about 680.

A clash shows up as a redefinition or ambiguity error on Windows only: usually two files in a group with the
same name in an anonymous namespace. Rename it, or add the file to `cmake/UnityExclusions.cmake` (203 files today).

`scripts/check-unity-clashes.sh` finds these on any machine (pre-commit, `ci-local.sh`, the Lint job; tests in
`scripts/tests/check-unity-clashes.test.sh`). It rebuilds the groups exactly as `synth_enable_unity` does (per target,
folder, md5 of the path, `AGENTSYNTH_UNITY_GROUP_SIZE`, `SYNTH_UNITY_SKIP` left out; only sources added before the
target's `synth_enable_unity` call), then scans each file for names declared in an anonymous namespace (variables,
functions, types, enumerators, aliases) and prints each repeated name with both files. Identical `using` aliases and
overloads are legal and pass. It also reads the project headers the group's sources `#include` directly (one level, not
their own includes) for names at named-namespace scope, and reports a source's anonymous-namespace name that matches one
when the namespaces are the same, or the anonymous one is an ancestor and a source of the group has a `using namespace`
of the header's namespace (that is how `kGap` in `StepGridPolyHint.cpp` met `stepgrid::kGap`, C2872 ambiguous symbol).
It is a text scan, so a clash it cannot see (macro-generated names) still shows on Windows.
There are no clashes today, so the baseline file is absent; rename a new clash rather than baselining it (`*.txt` is git-ignored, so a baseline would have to be force-added).
To try it on any machine: configure with `-DAGENTSYNTH_UNITY=ON` and build Core, AppUI and Tests.

## What did not work

- **Precompiled headers for local builds** — see the precompiled-headers entry under
  Optimizations for why they are limited to the CI jobs. Two earlier attempts were rejected
  outright: the first on the belief that JUCE's module `.cpp` and `.mm` files could not
  be handled (they can be skipped), the second because the macOS ccache had no room for the
  extra 171 MB that five per-target headers added, before the Actions cache kept one generation.
