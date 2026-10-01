# CI Build Caching

Two caches carry work between runs of [`ci-pipeline.md`](ci-pipeline.md)'s jobs. Both are easy to
break in ways that look exactly like a healthy build, just slower — which is why
`scripts/ci-cache-check.sh` gates them.

- **ccache** — the compiler cache. `CMAKE_C/CXX_COMPILER_LAUNCHER=ccache` plus
  `CMAKE_OBJC/OBJCXX_COMPILER_LAUNCHER=ccache` on macOS (rule 5 below — the launcher is per
  language), with `CCACHE_DIR` pinned explicitly to `${{ github.workspace }}/.ccache` on every
  platform. Max size is set per OS, measured from what a warm main build actually uses: **2 GB on
  Linux** (the Debug+coverage+AI-harness job — a seeded `Linux-ccache-main` entry measured 1.96 GB,
  already saturating a smaller cap), **1 GB on macOS** (one Release+tests build measured 238 MB with
  the precompiled headers that job turns on) and **512 MB on Windows** (160 MB measured). An entry
  fills to its cap over time as merges add objects, so the cap is a few builds' worth rather than
  the measured size — every configured byte is a byte a save could write toward the 10 GB repo
  budget). Keyed `<os>-ccache-<ref>-<sha>`;
  the restore-keys fall back this ref → `main` → anything. It also sets `compiler_check=content` and
  `sloppiness=time_macros,include_file_mtime,include_file_ctime`, because `actions/checkout`
  rewrites every file's mtime each run and the compiler binary's mtime changes whenever GitHub
  rebuilds the runner image — under ccache's mtime-based defaults both produce false misses.
- **FetchContent** — `build/_deps` (JUCE and GoogleTest sources), keyed on
  `hashFiles('cmake/DependencyVersions.cmake')`.

## Six rules, each learned from an outage

1. **`ci.yml` must keep its `push: main` trigger.** GitHub scopes a cache to the ref that wrote it;
   a PR run can read its own ref and the **base branch**, nothing else. While this workflow ran on
   `pull_request` alone it never wrote a cache into `main`'s scope, and **no PR ever restored one** —
   every run compiled the entire tree cold for months. The logs said `Cache not found for input
   keys: …` and CI passed regardless.
2. **Key `build/_deps` on the dependency pins alone**, never on `CMakeLists.txt`. `build/_deps`
   holds nothing but fetched sources, so adding a module must not invalidate it. A
   `hashFiles('CMakeLists.txt', 'Tests/CMakeLists.txt')` key minted a fresh ~350 MB entry per
   platform per workflow on every module PR; ten `CMakeLists.txt` edits in nine days pushed the repo
   past **GitHub's 10 GB per-repo cache limit**, which LRU-evicted the ccache entries too. All pins
   therefore live in `cmake/DependencyVersions.cmake` and the `FetchContent_Declare()` calls read
   them from there, so key and pin cannot drift.
3. **`CCACHE_DIR` must be set explicitly.** ccache's default directory varies by platform *and*
   version. The Linux job cached `~/.ccache` while ccache 4.x on `ubuntu-24.04` writes to
   `~/.cache/ccache`, so the Linux ccache was never even saved — no `Linux-ccache-*` entry ever
   existed. In `build-artifacts.yml` the path is a **matrix value**, since one job spans three
   runners and a job-level override is unavailable.
4. **Cache steps use `actions/cache/restore` plus `actions/cache/save`, not the combined
   `actions/cache`.** The combined action declares exactly one output, `cache-hit`, true only on an
   *exact* primary-key match — and the ccache primary key embeds `github.sha`, so it never matches
   exactly even when a restore-key fallback works perfectly. Only `actions/cache/restore` exposes
   **`cache-matched-key`**, the one value that reports a fallback hit. Reading it off the combined
   action yields an empty string forever: one run restored every cache and compiled at a **100%
   ccache hit rate** while the check reported `MISS` on all three platforms, so enforcement would
   have failed every build. The split also lets the save step run `if: always()`, so a successful
   compile is not discarded because a later test failed — every save step also adds
   `&& github.ref == 'refs/heads/main'` on top of that, per rule 6.
5. **The compiler launcher is per language — `C`/`CXX` do not cover `OBJC`/`OBJCXX`.** CMake treats
   Objective-C++ as its own language, so for a time every `.mm` compile bypassed ccache: **103
   translation units per macOS build**, because JUCE ships each of its modules as a single ObjC++
   unity file and each target compiles its own copy (Core, AppUI, AgentSynth, the plugin, `Tests`,
   both AI harnesses — since FRO306 only three copies remain: the shared `JuceModules` library, the
   app and the plugin, see `cmake/JuceModules.cmake`). Those units never change, and they were still rebuilt
   cold on every run —
   roughly 12 minutes of a 12 min 45 s macOS job, ending in a near-serial tail of 20–35 s
   `juce_gui_basics` / `juce_audio_processors` compiles while the rest of the build had finished. It
   survived four earlier cache fixes and the health check itself because it does not look like a
   cache failure from the outside: the caches restored, and the hit rate read a plausible **54%**,
   since a compile that never reaches ccache counts as neither a hit nor a miss. Both
   `CMakeLists.txt` (for local builds) and `ci.yml`'s macOS job set all four launchers; the launcher
   audit below is what keeps a sixth language from repeating it.
6. **PR runs restore only; main saves and prunes its previous ccache generation.** Every PR push
   used to both restore AND save — each attempt minted its own `refs/pull/<n>/merge`-scoped deps
   entry (if main's did not hash-match yet) and ccache entry (always, since the ccache primary key
   is per-commit and never exact-matches on restore), and those entries were never cleaned up until
   GitHub's 7-day / 10 GB-budget GC got to them. That reached **9.86 GB of the 10 GB repo limit**,
   with PR-scoped entries alone measured at **6.3 GB** (the Linux ccache family alone 5.49 GB) —
   enough to LRU-evict main's seeded macOS/Windows deps caches, so the next PR restored nothing and
   `scripts/ci-cache-check.sh` failed it by design; only a rerun, which itself saved a PR-scoped
   cache, passed. Every `Save FetchContent Dependencies` / `Save ccache` step's `if:` now adds
   `github.ref == 'refs/heads/main'`; the label-gated ASAN job's *deps* cache is restore-only the
   same way. **The ASAN job's ccache is the one deliberate exception** (2026-09-26): that job never
   runs on `push: main`, so nothing can seed a main-scoped cache for it, and without any cache every
   labelled run compiled the whole tree cold in RelWithDebInfo+ASAN — 48 minutes of a 54-minute
   run against 19 minutes for the same PR unlabelled. It now saves its ccache under the PR's own
   ref, which GitHub lets only later runs of that same pull request read: the first labelled run of
   a PR stays cold, every push after it is warm. Two guards keep that from re-creating the
   accumulation this rule exists for: the job's own prune step keeps a single generation per PR
   (deletes only entries under that PR's ref, only after a successful save, only ones older than
   the key just saved), and **`.github/workflows/cache-cleanup.yml` deletes every cache scoped to a
   PR's ref the moment the PR closes**, merged or not — the FRO209 automation, which also covers
   any other PR-scoped entry a future job might leave (the seven dead `cache-apt-pkgs_*` entries
   FRO209 found by hand were that kind).

   Saving to main does not itself solve accumulation, since a ccache key is per-commit and cache
   entries are **immutable** — every merge mints a new `<os>-ccache-main-<sha>` generation instead of
   updating one in place, which would re-blow the 10 GB budget in three or four merges on its own.
   So each `Save ccache` step is followed by a **`Prune superseded main cache generations`** step
   (`gh cache list` / `gh cache delete`, needing job-level `permissions: actions: write`) that runs
   only after a successful save — never `if: always()`, which would delete every generation
   including main's only good one on a failed or skipped save — and only ever deletes entries
   strictly older than the one this run just saved, never "everything but mine", so a run working
   off a stale cache listing cannot delete a sibling run's fresher save. The release workflow's own
   `<os>-release-ccache-<sha>` family gets the same treatment. The same step also sweeps
   superseded `build/_deps` entries for old `DependencyVersions.cmake` hashes, since a pin bump
   mints a new deps generation the same immutable way; unlike ccache this only fires when a pin
   actually changes, not on every merge.

   **FRO341 (2026-09-28): keeps only the single most recent generation per family, down from 2.**
   GitHub's 10 GB per-repo cache limit was exceeded (10.4 GB) and LRU-evicted a deps cache entry,
   failing an unrelated PR's `scripts/ci-cache-check.sh`. The 2-generations rule above was the
   direct cause: one generation's worth of every ccache `max_size` cap plus the deps entries across
   both `ci.yml` and `build-artifacts.yml` totals roughly 6.8 GB by construction (measured
   2026-09-28: `ccache-main` 2.8 GB, `release-ccache` 1.2 GB, `deps3`/`deps4` 1.1 GB,
   `release-deps-ninja3`/`4` 1.1 GB) — keeping 2 of that is ~10.3 GB before anything else in the
   repo's cache namespace is counted. `scripts/ci-cache-budget.sh` (run from the Lint job) sums
   what the workflows commit to writing for one generation and fails if that total alone would
   exceed its own budget, so a future cap increase is caught at review time. Two runtime checks
   catch what the static sum cannot: each prune step ends by reading
   `repos/:owner/:repo/actions/cache/usage` and emitting a (non-failing) `::warning::` above 8.5 GB
   actual usage, and `scripts/ci-cache-check.sh` warns when a single job's own `ccache --show-stats`
   reports its cache at or above 90% of its configured `max_size` — a cap that's too small hides its
   own growth as extra misses, never extra bytes, so nothing else makes it visible.

   This same pass also fixed `build-artifacts.yml`'s `release-deps-ninja` family having **no prune
   call at all** — every past `DependencyVersions.cmake` pin bump (a googletest/Sparkle/WinSparkle
   version bump) left its superseded entry behind indefinitely, unlike `ci.yml`'s `deps` family,
   which was always pruned. It is still kept in its own key namespace, deliberately not merged with
   `ci.yml`'s (see below for why), just no longer exempt from cleanup.

   **FRO341 also made JUCE's `FetchContent_Declare` a shallow clone** (`GIT_SHALLOW TRUE` in
   `CMakeLists.txt` — valid because `SYNTH_JUCE_GIT_TAG` pins a tag, `8.0.3`, not a bare SHA):
   JUCE's own `.git` directory measured ~294 of each ~376 MB deps entry (~80%), pure history no
   build ever reads. `build/_deps`'s cache key hashes only `cmake/DependencyVersions.cmake`, which
   this change does not touch, so the key had to be bumped by hand for the shallow clone to take
   effect at all: `deps3` → `deps4` throughout `ci.yml`, `release-deps-ninja3` →
   `release-deps-ninja4` in `build-artifacts.yml` (both a second such bump — the first, "3", orphaned
   the entries left by the old CMakeLists-wide key in rule 2 above). A brand-new key prefix
   guarantees a miss for every job in the PR that bumps it, since nothing has ever been saved under
   it — `scripts/ci-cache-check.sh`'s `DEPS_KEY_CHANGED` input (see its own header comment) detects
   that case (a git diff of `ci.yml` against the PR's base ref for an added/removed `-deps<N>-`
   line) and reports it as an expected, notice-level miss for `build/_deps` alone, never relaxing
   the ccache half of the same check.

   **The two workflows' deps caches are deliberately not merged into one key** despite fetching from
   the same `cmake/DependencyVersions.cmake` pins: `ci.yml` builds with `ENABLE_TESTS=ON` (which
   populates GoogleTest) and `build-artifacts.yml` never sets it. Sharing a key would risk whichever
   job's `Build` step finishes first permanently winning the cache-populate race for a new hash — if
   that is the no-tests release job, `ci.yml` would silently lose GoogleTest from its restored cache
   and re-fetch it on every run, with no test failure and nothing for the health check to catch.

## The cache health check

`scripts/ci-cache-check.sh` runs after the build on Linux, macOS and Windows. It reads each cache
step's `cache-matched-key` output plus `ccache --show-stats`, writes a summary table to the job
summary, and:

- **fails** when a cache that should have restored did not;
- **fails** when any `C`/`CXX`/`OBJC`/`OBJCXX` compile generated under `build/` does not invoke
  ccache — the launcher audit. It reads **both** `build/CMakeFiles/rules.ninja` and
  `build/build.ninja`, because CMake splits the launcher across them: the rule's command holds a
  `${LAUNCHER}` placeholder — the word `ccache` never appears in it — while the real path is a
  per-build-statement `LAUNCHER =` variable. The audited unit is therefore the build **statement**,
  one per translation unit, which is also the number worth reporting (103 `.mm` files, not the ~10
  ObjC++ rules they share). A launcher inlined into the rule command, as older CMake did, still
  counts as wired. Two earlier versions of this audit passed against simplified fixtures and then
  failed in CI, so the fixtures now reproduce the real two-file layout verbatim. It parses the
  generator's own output rather than the workflow, so it catches the fault however it arrives — a
  new language, a dropped `-D` flag, a CMake upgrade. Link rules, the Windows resource compiler and
  the C++20 module-scan rules are out of scope: ccache does not handle them. If `build.ninja` exists
  but no compile rule matches the expected `rule <LANG>_COMPILER__<target>` naming, that **warns** —
  an audit that silently matches nothing is the same blind spot it was added to close;
- **warns** when the ccache hit rate is under `CACHE_MIN_HIT_RATE` (default 25%), which drops
  legitimately whenever a PR touches a widely-included header. Note what the rate cannot tell you:
  it is a ratio over the calls that *reached* ccache, so it never detects an unwired language — that
  is the audit's job.
- **warns** (FRO341) when ccache's own `--show-stats` reports its cache at or above
  `CACHE_SATURATION_WARN_PCT` (default 90%) of its configured `max_size`. A cap that's too small
  hides its own growth as extra *misses*, never extra bytes — ccache evicts to stay under the cap —
  so nothing about a job's hit rate on its own distinguishes "the cap is too small" from any other
  cause of a lower rate. This is the one signal that does, and it never fails the build: a slow
  build from a tight cap is not a broken one.

`CACHE_WARM_EXPECTED` decides which runs are held to that standard. It is true **only for pull
requests from a branch in this repository.** Two cases are legitimately cold and are reported as a
notice instead:

- the **push-to-`main`** run, which is what seeds the cache in the first place;
- a **pull request from a fork** — GitHub gives forks an isolated cache scope with no read access to
  the base repository's entries, so a miss is guaranteed and is nothing the contributor can fix.
  Without this exemption, enforcement would fail every outside contribution on its first run.

A third, narrower case relaxes only the `build/_deps` half of the check, on an otherwise-normal
same-repo PR run: `DEPS_KEY_CHANGED` (FRO341) is true when the PR itself changed the deps cache key
prefix in `ci.yml` (e.g. `deps3` → `deps4`, as this ticket's own JUCE-shallow change does) — a fresh
prefix has never been saved anywhere, so the miss is guaranteed and not a regression, but only for
`build/_deps`; ccache's key is unaffected by a deps bump and stays held to the normal standard. When
unset, it's computed from a `git diff` of `.github/workflows/ci.yml` against `$GITHUB_BASE_REF`
(set automatically on a same-repo `pull_request` run); a fetch/diff failure leaves it `false` rather
than silently swallowing a real cold-cache regression.

Enforcement is controlled by the repository variable **`CI_CACHE_CHECK_ENFORCE`** (Settings →
Secrets and variables → Actions → Variables), currently `true` — a cold cache fails the build. It is
a variable rather than a hard-coded value so enforcement can be switched off without a code change
if a runner-image or `actions/cache` change ever starts producing false alarms.
`build-artifacts.yml` runs the same check but pins it to report-only: losing a release over a cache
miss would be a worse outcome than a slow release.

The check also **fails safe**: an empty `cache-matched-key` contradicted by a high ccache hit rate
(≥ `CACHE_SELFCHECK_HIT_RATE`, default 50%) is reported as *a misconfigured check*, not a cold
cache, and does not fail the build — a cold build cannot hit 100%, since its only hits are files
compiled into two targets in the same run, around 15%. A genuinely cold cache still fails. The guard
keys on the **ccache** contradiction alone: a high hit rate proves the ccache restored and both keys
share the same plumbing, but it proves nothing about `build/_deps`, so a deps-only miss with a
populated ccache key is still a real failure.

The script has its own fixture tests (`scripts/tests/ci-cache-check.test.sh`, run by the Lint job):
if the thing that detects a broken cache breaks silently, the result is shipping cold builds
unnoticed again. Those tests scrub every input variable before each case — `ci.yml` exports
`CACHE_CHECK_ENFORCE` and `CACHE_WARM_EXPECTED` workflow-wide, the Lint job inherits them, and a
non-hermetic harness silently inherited CI's values and passed cases it should have failed.

To inspect cache state directly:

```bash
gh api "repos/:owner/:repo/actions/caches?per_page=100" \
  -q '.actions_caches[] | "\(.size_in_bytes/1048576|floor)MB\t\(.ref)\t\(.key)"'
# Total size -- evictions start once this approaches GitHub's 10 GB limit:
gh api "repos/:owner/:repo/actions/caches?per_page=100" -q '[.actions_caches[].size_in_bytes]|add/1073741824'
```

## Static budget check and runtime alarms (FRO341)

Everything above bounds the repo to **one** cache generation per family, but nothing stopped that
one generation from growing past the 10 GB limit on its own — which is exactly what happened on
2026-09-28 (see the FRO341 note under rule 6 above). Three independent guards, from cheapest/
earliest to most expensive/latest:

- **`scripts/ci-cache-budget.sh`**, run from the Lint job (no new workflow, no new required check).
  Parses every `ccache --set-config=max_size=` in `ci.yml` (excluding the label-gated ASAN job,
  which never runs on `push: main` and is covered by the runtime alarm below instead, since it is
  not part of "one generation on main") and every `ccache_max_size:` in `build-artifacts.yml`'s
  release matrix, adds a fixed `DEPS_ALLOWANCE_MB` for the `build/_deps` entries (not capped by
  ccache, so it can't be read from a config value — see the constant's own comment in the script
  for how it's derived, both before and after the shallow-JUCE change above), and fails if the
  total exceeds `BUDGET_CAP_MB` (default 8 GiB, leaving headroom under the 10 GB repo limit for
  the runtime alarm's own margin and any transient overlap while a save is in flight). Its own
  tests (`scripts/tests/ci-cache-budget.test.sh`) run it against the real workflow files (must
  pass) and a fixture that exceeds the budget (must fail).
- **The runtime usage alarm**, at the end of every `Prune superseded main cache generations` step
  (`ci.yml` ×3, `build-artifacts.yml` ×1): reads
  `gh api repos/:owner/:repo/actions/cache/usage --jq .active_caches_size_in_bytes` and emits a
  `::warning::` (never fails the build) naming the actual byte count when it is at or above 8.5 GB
  — catching anything the static sum above can't see (an old `deps3`/`release-deps-ninja3`
  generation not yet garbage-collected, an ASAN PR's cache, GitHub-side variance).
- **The per-job saturation warning** in `scripts/ci-cache-check.sh` (see above) — catches a single
  job's own cap being too small, which the repo-wide totals above cannot.
