# Local CI and Git Hooks

`scripts/ci-local.sh` is the single source of truth for "what CI will check, run locally". It is
what the pre-push hook runs, and what a developer runs by hand to get the same signal without
waiting on a CI round-trip. It reproduces [`ci-pipeline.md`](ci-pipeline.md)'s `Lint` job plus this
machine's platform build-and-test job.

```bash
bash scripts/ci-local.sh                # run every check
bash scripts/ci-local.sh --open         # ...then `open` the built app bundle on macOS
bash scripts/ci-local.sh --skip-tests   # skip the Tests suite for a faster local loop
bash scripts/ci-local.sh --help         # usage
```

## What it does, in order

Fast checks first, so a lint failure does not wait on a full build.

1. `clang-format --dry-run --Werror` over `Source/` `Tests/` `Tools/` — the Lint job's "Check
   Formatting" step, exactly. **Check-only, never `-i`** — a violation fails loudly instead of being
   silently rewritten.
2. `bash scripts/utf8-literal-check.sh` against the real tree — the Lint job's "Check for un-decoded
   UTF-8 escapes" step, run directly rather than only via its unit test.
3. `bash scripts/check-file-sizes.sh` against the real tree — see
   [`file-size-guard.md`](file-size-guard.md).
3b. `bash scripts/ci-cache-budget.sh` against the real `ci.yml`/`build-artifacts.yml` — the Lint
   job's "Check CI cache budget" step (FRO341): sums one cache generation's worth of configured
   `ccache` `max_size` values plus a `build/_deps` allowance and fails over an 8 GiB budget — see
   [`ci-caching.md`](ci-caching.md).
4. `bash scripts/check-function-sizes.sh` against the real tree — see
   [`function-size-guard.md`](function-size-guard.md).
5. `bash scripts/check-header-comments.sh` against the real tree — see
   [`header-comment-guard.md`](header-comment-guard.md).
5b. `bash scripts/check-comment-provenance.sh` against the real tree — see
   [`comment-provenance-guard.md`](comment-provenance-guard.md).
5c. `bash scripts/check-fixed-waits.sh` against the real tree — tests that pump the message loop for a
   fixed time and then assert; see [`testing.md`](testing.md#waiting-for-async-work).
6. `bash scripts/check-docs.sh` against the real tree — see [`docs-guard.md`](docs-guard.md).
7. Every `scripts/tests/*.test.sh`, globbed, so a newly added one is picked up automatically without
   editing this script. `check-nonascii-literals.test.sh`'s last case scans the real `Source/` tree
   itself, so this also covers the Lint job's
   [ASCII-literal gate](ascii-literal-guard.md) on live code, not just the checker's fixtures.
   `no-pipe-into-grep-q.test.sh` scans `scripts/` for `echo`/`printf` piped into `grep -q` in a script
   that sets `pipefail`: grep exits at its first match, the writer dies of SIGPIPE, and pipefail turns the
   match into a failure only on a busy machine, so script tests pass text to grep as a here-string
   (`grep -qF "text" <<<"$output"`).
   `check-ui-caps.test.sh` likewise scans `Source/UI/` for all-caps UI text
   ([rule](accessibility.md#no-all-caps-ui-text)) and for the default theme's accent and text colours
   written as literals outside `Source/UI/Theme/` ([rule](../layout/theming.md#themed-widgets)).
8. Configure `build-ci-local/` with `-DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON
   -DENABLE_AI_HARNESS=ON` and build with a plain
   `cmake --build` — every target those jobs build (`Core`, `AppUI`, `AgentSynth`,
   `AgentSynthPlugin`, `Tests`), the same way a missing `CMakeLists.txt` entry shows up in CI.
   ccache and Ninja are picked up automatically when installed (the `find_program(CCACHE_PROGRAM
   ccache)` block at the top of `CMakeLists.txt`), so a second run is an incremental rebuild, not a
   cold one. On the **first** configure only, and only when this checkout is a git worktree, it also
   reuses the main checkout's already-fetched dependency sources — see
   [Worktree dependency-source reuse](#worktree-dependency-source-reuse) below.
8b. Configure a scratch `build-ci-local-ci-flags/` with CI's own macOS/Windows flags (`-DAGENTSYNTH_PCH=ON`
   and the explicit ccache compiler launchers, with the ccache `base_dir` cleared because `cmake/Pch.cmake` refuses
   a precompiled header beside one) and build nothing. The build above configures the way a developer does, so a
   configure problem that only CI's flags hit (JUCE 9's `juceaide` bootstrap choking on the list-valued launcher)
   went green here and red on all three CI jobs. It reuses the dependency sources the build above fetched, so it
   adds about 25 s warm. `scripts/tests/ci-local-ci-flags.test.sh` fails when `ci.yml`'s macOS configure gains a
   `-D` flag this step lacks.
9. Dev-sign the built app bundle (macOS only): `bash scripts/dev-sign-app.sh "$APP_PATH"`. Not a CI
   check — it runs only locally, after the build and before the test suite, so a signing failure
   surfaces early rather than after a multi-minute test run.
10. Run the full suite: `build-ci-local/Tests/Tests --gtest_brief=1` (only failures and the summary print), behind the shared test lock described in
   [Running suites in parallel](#running-suites-in-parallel) below. Skippable with `--skip-tests`
   (default off — the pre-push hook and CI both expect the full suite) for a faster local loop;
   every earlier step, including dev-signing, still runs, so a `--skip-tests` rebuild keeps the
   same TCC identity for a live or manual app run. The suite's native windows and dialogs are invisible,
   click-through and never take focus (macOS), so you can keep using the machine; set
   `AGENTSYNTH_TESTS_VISIBLE=1` to see them ([`test-mode.md`](test-mode.md#hidden-windows)).

On success it prints the path to the built `Agent Synth.app` bundle under `build-ci-local/`, found
the same way the release workflow locates it for packaging, so a green terminal is not the only
thing you are left with — you can open and try the real app. `--open` does that automatically
(macOS only; a no-op notice elsewhere, since the flag also has to be safe to leave on in a
headless run).

## Deliberately not reproduced

All covered elsewhere: the Ubuntu coverage gate (`bash scripts/coverage.sh`, a separate opt-in —
see [`testing.md`](testing.md)), the label-gated ASAN job (opt-in per PR, not something to run on
every push), and actual cross-platform compilation — this only exercises the toolchain installed on
the machine it runs on, so Linux and Windows failures still need CI or a VM.


## Git hooks

Hooks are **not** auto-installed. Install once per clone:

```bash
bash scripts/install-hooks.sh
```

Two hooks are registered:

- **pre-commit** (`scripts/pre-commit-lint.sh`): runs `clang-format --dry-run --Werror` on staged
  `Source/` and `Tests/` C/C++ files (skipped entirely when no C++ is staged), then the
  [file-size](file-size-guard.md), [function-size](function-size-guard.md),
  [header-comment](header-comment-guard.md), [comment-provenance](comment-provenance-guard.md), [fixed-wait](testing.md#waiting-for-async-work) and
  [docs](docs-guard.md) guards against the whole tree, unconditionally, for any commit with staged
  changes. Those six scan the whole tree in about a
  second each, so there is no benefit to staged-file scoping them the way there is for
  clang-format. Fast; mirrors the CI Lint job. It also warns if the local `clang-format` version
  differs from the pin in `.clang-format-version`.
- **pre-push** (`scripts/ci-local.sh`): the full local CI reproduction above. The first push
  configures the `build-ci-local/` directory; subsequent pushes are fast incremental rebuilds
  (ccache and Ninja are picked up automatically when installed).

The generated `pre-push` hook is a static file, so a clone whose hooks predate a change to what
pre-push runs keeps pointing at the old script — re-run `bash scripts/install-hooks.sh` after
pulling one.

Bypass a single invocation with `--no-verify`:

```bash
git commit --no-verify
git push --no-verify
```

Run either by hand at any time:

```bash
bash scripts/pre-commit-lint.sh  # lint staged files
bash scripts/ci-local.sh         # everything the pre-push hook runs
```

## clang-format is pinned

clang-format is pinned via the PyPI `clang-format` wheel to the version recorded in
`.clang-format-version`. CI installs that exact version with `pip install "clang-format==$(cat
.clang-format-version)"` after `actions/setup-python`, so CI and the local hooks run the identical
binary. Install or update locally with the same command:

```bash
pip install "clang-format==$(cat .clang-format-version)"
```

**Why pin it.** Different clang-format releases format the same file differently, which produces
"the hook passes locally but CI fails" drift that has nothing to do with the change under review.

## Worktree dependency-source reuse

A freshly created `git worktree add` checkout starts with no `build-ci-local/` at all, so its first
configure has nothing in `build-ci-local/_deps` and FetchContent re-downloads JUCE (~600 MB),
GoogleTest and Sparkle from scratch — even though the main checkout sitting right next to it already
has those exact sources on disk.

**Why this is worth automating.** Measured on the machine that fixed it: **4m 12.7s** for a cold
configure (no ninja installed, so the default Makefiles generator) down to **32.0s** reusing the
main checkout's sources. The gap depends heavily on network conditions to GitHub, which is why the
two numbers differ so much; unfixed, it repeats on every new worktree.

`scripts/ci-local.sh` avoids the re-fetch on the **first** configure of a worktree. Once
`build-ci-local/CMakeCache.txt` exists, later configures are already fast, so this never applies
again. The logic lives in `scripts/lib/deps-reuse.sh` — sourced by `ci-local.sh`, unit-tested
directly by `scripts/tests/ci-local-deps-reuse.test.sh` against fixture directory trees, with no
cmake, no compiler and no real worktree needed:

- Detects a worktree via `git rev-parse --git-common-dir` pointing outside the checkout itself (a
  plain checkout's common dir resolves back to its own `.git`).
- **Only** reuses when `cmake/DependencyVersions.cmake` is byte-identical between the worktree and
  the main checkout — a pin bump made in the worktree (not yet merged to `main`) must never silently
  build against the main checkout's OLD sources.
- Passes `-DFETCHCONTENT_SOURCE_DIR_JUCE` / `_GOOGLETEST` / `_SPARKLE` pointing at `<main
  checkout>/build-ci-local/_deps/{juce,googletest,sparkle}-src` for whichever of those the main
  checkout has already fetched. Partial reuse is fine — a source CMake still fetches on either side
  is fetched normally.
- JUCE is reused only when every `cmake/patches/*.patch` of the worktree is already applied to the main
  checkout's copy (`git apply --check --reverse`): a reused source dir skips FetchContent's patch step, so an
  unpatched copy would build without the patch ([`juce-patches.md`](juce-patches.md)). The other sources are
  still reused and the one-line message says JUCE was skipped.
- `CI_LOCAL_NO_DEPS_REUSE=1` opts out unconditionally, forcing a normal from-scratch fetch.
- Prints exactly one line saying what was reused, or why not (not a worktree, pins differ, nothing
  fetched yet in the main checkout, or the opt-out env is set).

## Running suites in parallel

**Two test binaries running at once on the same machine used to collide through the shared on-disk
"Agent Synth" `ApplicationProperties` file.** FRO192 hit exactly that: a `ProjectLoadStripGainTest`
case reported FAILED by one of two sessions running their suites at the same time, green alone and
green again under a lock. Every run now gets its own private settings folder (see
[`test-patterns.md`](test-patterns.md#shared-settings-file-reset-guard)), so that collision is gone
and a run no longer touches the developer's real settings. Concurrent runs still share the machine's
audio device, which a stuck run can wedge for everyone.

`scripts/ci-local.sh` therefore runs the suite as parallel shards (step 10) —
`scripts/lib/test-shards.sh`, unit-tested by `scripts/tests/ci-local-test-shards.test.sh` in the Lint job:

- **Each shard owns its on-disk state**: its own `AGENTSYNTH_SETTINGS_DIR`, its own `TMPDIR`, and a
  uniquely named hard link of the binary. The link matters on macOS, where JUCE ignores `TMPDIR` and
  puts its temp folder at `~/Library/Caches/<executable name>`.
- **Test slots limit the machine, not the gate.** A shard holds one slot while it runs, an advisory
  lock (`flock` on Linux, `lockf` on macOS) under `git rev-parse --git-common-dir`, which every
  worktree shares. Default is three quarters of the cores (`CI_LOCAL_TEST_SLOTS`), with 8 shards per
  suite (`CI_LOCAL_TEST_SHARDS`). A crashed run frees its slots.
- **Waiting is bounded.** A shard gives up after `CI_LOCAL_TEST_SLOT_TIMEOUT` seconds (default 3600)
  with a distinct message and exit status `75`, never conflated with a red suite.
- **A red shard runs once more, alone, after the others finish.** A test that only fails on a fully
  loaded machine doesn't cost the whole gate. It is printed as `FLAKY` with the test names either way,
  and those tests need fixing. CI doesn't retry.
- **Builds use half the cores** (`CI_LOCAL_JOBS` overrides). Measured with 60 uncached compiles on a busy
  16-core machine: about twice as fast as a quarter of the cores for one gate and no slower for six. Every
  core made six gates 2-3x slower, and a load limit (`-l`) starved a gate whenever other sessions held the
  load above it.

Tests must not depend on the order they run in or on another test having run first: shards split the
order, and a test's neighbours differ from one run to the next. Running the binary directly (a single
filtered suite while iterating) takes no slot, so keep such runs short.

### `--fast`

`bash scripts/ci-local.sh --fast` is for iterating. It checks formatting of changed files only, builds
only `Tests`, and runs only the suites whose test files include a changed header or changed themselves
(`scripts/lib/changed-tests.py`, reading the build's own dependency data). A build-input change, or one
reaching over 40% of the test files, runs the full suite. CI still runs everything, so `--fast` passing
is not a promise that CI passes.

**ccache settings for parallel worktrees.** The dependency-source reuse above only covers the
FetchContent download; the compiler cache is a separate concern and needs its own configuration to
share hits across sibling worktrees. The default `max_size` (5 GiB) is sized for one checkout, not
several building concurrently — measured at 99.95% full with 5,901 cleanups and a 15.9% hit rate
running two worktrees side by side, because evictions were racing the builds. `hash_dir=true`
(ccache's default) additionally bakes the compiling directory's absolute path into the cache key, so
two worktrees compiling the same source at different paths can never share a hit at all. Fix once,
globally — this is local machine config, not a repo setting:

```bash
ccache --set-config max_size=60G
ccache --set-config base_dir=<projects root>
ccache --set-config hash_dir=false
```

`base_dir` plus `hash_dir=false` is what makes this safe: ccache rewrites any path under `base_dir`
to a relative one before hashing, so object code that only differs by which worktree compiled it
still hits, while paths outside `base_dir` (a stray absolute include from a system header, say) are
left alone.

This sharing is also why precompiled headers (`-DAGENTSYNTH_PCH=ON`) stay off locally: a
precompiled header carries its build directory's absolute path, so one served from another
worktree's build fails every compile that uses it. `cmake/Pch.cmake` refuses to configure with
the option on while `base_dir` is set. See [ci-pipeline.md](ci-pipeline.md#optimizations).

## Dev-signing

`scripts/dev-sign-app.sh` re-signs the built bundle with a stable **Apple Development** identity
after every local build: it runs as a `POST_BUILD` step of the `AgentSynth` target on macOS
whenever the `CI` environment variable is unset (so a plain `cmake --build`, not only
`ci-local.sh`, gets it), after Sparkle is embedded.

A sandboxed agent shell cannot see the keychain, so the script finds no identity there and leaves
the bundle ad-hoc signed: re-run it outside the sandbox before launching that build.

**Why.** A local ad-hoc or linker-signed build's designated requirement pins to `cdhash H"..."`,
which changes on every build — any changed byte mints a new hash. macOS TCC (the
microphone/camera permission system) keys its grants on the designated requirement, so every fresh
local build looks like a brand-new app to TCC: it re-prompts for microphone access, and that dialog
is one automation cannot dismiss, stalling live app testing on a human click. The login keychain does the same for the saved sign-in token: an ad-hoc build asks for the keychain password on every launch, while a stable signature keeps the first Always Allow.

```bash
bash scripts/dev-sign-app.sh "$APP_PATH"
```

- On Darwin it picks the first identity from `security find-identity -v -p codesigning` whose name
  starts with `Apple Development:`, then runs `codesign --force --deep --sign "$identity"` followed
  by `codesign --verify --deep --strict`. Signed with such an identity, the app's designated
  requirement becomes `identifier "com.agentsynth.app" and anchor apple generic and certificate
  leaf[subject.CN] = "Apple Development: <name> (<team>)" and ...` — identical across rebuilds, so
  TCC keeps recognising the app. **The first launch after switching to a stable identity still asks
  once; every later rebuild signed with the same identity does not.**
- Override the identity, or opt out, with `AGENTSYNTH_DEV_SIGN_IDENTITY`: set it to an identity name
  to force that one, or to `-`/`none` to deliberately skip signing.
- If no Apple Development identity is installed, including on every CI runner, it prints a note and
  exits `0` rather than failing the build; the app just stays ad-hoc signed and TCC re-prompts as
  before.
- A signature that fails to apply or verify is a real error (non-zero exit), unlike a missing
  identity.
- This is local dev tooling only. It never runs in CI (no Apple Development identity exists on CI
  runners, so it always takes the "no identity found" no-op branch there —
  `scripts/tests/dev-sign-app.test.sh` covers the identity-selection logic itself with fake
  `uname`/`security`/`codesign` shims, on every CI platform including Linux), and it never touches
  the release packaging path, which keeps its own ad-hoc `codesign -s -` for distributed builds. No
  hardened runtime, no entitlements: a plain re-sign for local trust, not a distributable or
  notarizable signature.

**Why a mic prompt can appear at all when audio input is opt-in.** On a fresh launch the app
requests **0** input channels and JUCE builds an output-only device — no input stream object, no
`AudioIODeviceCombiner` (traced through `AudioDeviceManager::insertDefaultDeviceNames` and
`CoreAudioIODeviceType::createDevice`). The prompt has been observed on a dev machine whose default
output device is a combined-I/O USB interface that is also the default *input* device; starting that
device for output appears to be enough for macOS to ask. A mic prompt at launch on such a machine is
therefore not by itself a regression of the opt-in contract — check the default device
(`system_profiler SPAudioDataType`) before debugging the app. Signing makes one Allow persist; it
does not stop the first ask.

**FRO27.** A saved device state can no longer name an input device it did not actually enable —
`synth::stripUnusedInputDevice` (`Source/AudioEngine/DeviceStateInputs.h`) strips one on save and
on load, working around a JUCE quirk where `AudioDeviceManager` can fill in a default microphone
name even for an output-only change. The app also only asks for the mic-permission runtime prompt
when a saved state actually enables input; a plain output-only launch asks for nothing, and a
denied prompt no longer stops audio output — it only means the input side stays silent. If you
still see a prompt on such a launch, it's the combined-I/O-default-device case above, not this bug.

**Launching for computer-use with no device prompt at all.** Dev-signing only makes the prompt
*stable*, not absent — if computer-use is driving the app (see
[`docs/architecture/audio-engine.md#automation-launch-no-audio-device`](../architecture/audio-engine.md#automation-launch-no-audio-device)),
skip the device entirely instead:

**Keychain.** A local (non-CI) Apple build keeps its own sign-in item (`<bundle id>.dev.refreshtoken`,
the `AGENTSYNTH_DEV_KEYCHAIN` define), apart from the release app's, so it never reads an item whose
access list does not trust the dev signature: sign in once in the dev build. Automation launches
(`--no-audio-device`) and `--no-keychain` / `AGENTSYNTH_NO_KEYCHAIN=1` skip the keychain entirely
(in-memory sign-in), so no login-keychain prompt can block the launch. See
[`docs/ai/accounts.md`](../ai/accounts.md#keychain-access-and-permission-prompts).

```bash
open -n "<path>/Agent Synth.app" --args --no-audio-device
# or, running the binary directly:
AGENTSYNTH_NO_AUDIO_DEVICE=1 "<path>/Agent Synth.app/Contents/MacOS/Agent Synth"
```

No audio or MIDI device is opened and no TCC prompt of any kind can fire, so there is nothing for
an agent to dismiss.
