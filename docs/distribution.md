# Distribution & Auto-Update

Direct-download distribution (no app stores — see [P5·2](../CLAUDE.md) / the roadmap). This doc
covers how release builds get their version identity and how Sparkle (macOS) / WinSparkle
(Windows) check for updates.

## Version identity

Two numbers are baked into every macOS build, and they mean different things:

- **`CFBundleShortVersionString`** (`PROJECT_VERSION` in `CMakeLists.txt`, e.g. `0.13.2`) — the
  human-facing marketing version. Hand-bumped by editing `project(AgentSynth VERSION ...)`.
- **`CFBundleVersion`** (`SYNTH_BUILD_NUMBER` cache var) — the value Sparkle actually compares
  to decide whether an update is available. This is **not** the same value on purpose:
  `build-artifacts.yml`'s release job (`mathieudutour/github-tag-action`) auto-tags every push to
  `main` with a semver bump, entirely independent of `CMakeLists.txt`. If `CFBundleVersion` only
  advanced when a human remembered to bump `PROJECT_VERSION`, Sparkle would either never detect a
  real update or always think one was available. Instead, CI passes
  `-DSYNTH_BUILD_NUMBER=${{ github.run_number }}` at configure time — a value that's known
  before the build starts and increases every workflow run, regardless of what tag gets minted
  afterwards. Local/dev builds default to `0` and are never distributed, so this doesn't matter for
  them.

## Sparkle integration (macOS)

- Fetched as a prebuilt `.xcframework` distribution via `FetchContent` (`cmake/DependencyVersions.cmake`'s
  `SYNTH_SPARKLE_URL`/`SYNTH_SPARKLE_SHA256`), APPLE-only. It's a binary artifact, not a CMake
  project, so `FetchContent_MakeAvailable` just populates it — no `add_subdirectory`.
- `Source/Update/UpdateManager.h` / `SparkleUpdateManager.mm` wrap `SPUStandardUpdaterController`
  behind a two-method interface (`isAvailable()`, `checkForUpdates()`). The `.mm` file is compiled
  with `-fobjc-arc` explicitly (JUCE's own `.mm` sources are non-ARC — see the `set_source_files_properties`
  call in `CMakeLists.txt`), so this one bridge file manages its own memory without hand-written
  retain/release.
- **Safe-by-default startup**: the constructor reads `SUPublicEDKey`/`SUFeedURL` from the running
  app's `Info.plist` and only starts Sparkle's updater if both are non-empty. Without a real public
  key configured, Sparkle would otherwise show an "app is misconfigured" alert on every launch —
  this keeps dev builds and any build before a signing key exists completely inert. When inert,
  `isAvailable()` returns `false` and `MainComponent::getCommandInfo` marks the Help ▸ Check for
  Updates… menu item inactive (greyed out) rather than hiding it.
- Framework embedding: this project builds with Ninja (not Xcode), which has no automatic "Embed
  Frameworks" build phase. `CMakeLists.txt` copies `Sparkle.framework` into
  `Contents/Frameworks/` via a `POST_BUILD` custom command (using `ditto`, which preserves the
  framework's `Versions/Current` symlink structure) and sets `INSTALL_RPATH` to
  `@executable_path/../Frameworks` so the binary's `@rpath/Sparkle.framework/...` load command
  resolves. CI's existing `codesign --force --deep -s -` step re-signs the embedded framework along
  with the rest of the bundle — no separate signing step needed.
- No App Sandbox / XPC services: the app isn't sandboxed today, so the simpler non-sandboxed Sparkle
  integration applies (see Sparkle's [sandboxing docs](https://sparkle-project.org/documentation/sandboxing/)
  if that ever changes).
- **Plugin targets (VST3/AU) link Sparkle weakly, not the app's hard `-framework`.** `MainComponent`
  (shared via `AppUI`) owns an `UpdateManager` member unconditionally, so `AgentSynthPlugin_VST3`/`_AU`
  need the same `.mm` source and framework as the app just to satisfy the linker — but
  `juce_vst3_helper` dlopens the freshly-linked bundle immediately after linking (to write
  `moduleinfo.json`), *before* that target's own framework-embed step has run, so a hard dependency
  makes that load fail outright. `-weak_framework Sparkle` (`LC_LOAD_WEAK_DYLIB`) lets the bundle load
  regardless of whether Sparkle is present yet. This also matches actual intent: the plugin's
  Info.plist never gets `SUFeedURL`/`SUPublicEDKey` merged in (only the app's does), so
  `isAvailable()` is always `false` there — Sparkle is genuinely optional for the plugin, not just
  incidentally absent at one point in the build.

## WinSparkle integration (Windows)

- Fetched as a prebuilt binary distribution via `FetchContent` (`cmake/DependencyVersions.cmake`'s
  `SYNTH_WINSPARKLE_URL`/`SYNTH_WINSPARKLE_SHA256`), WIN32-only — same "binary artifact, no
  `add_subdirectory`" shape as Sparkle's `.xcframework`. The distribution ships prebuilt per-arch
  (`Win32`/`x64`/`ARM64`) directories; only `x64` is wired up, matching this project's CI matrix
  and JUCE target architecture.
- `Source/Update/UpdateManager.h` / `WinSparkleUpdateManager.cpp` present the same two-method
  interface (`isAvailable()`, `checkForUpdates()`) as the macOS side, wrapping WinSparkle's C API
  (`win_sparkle_set_appcast_url`, `win_sparkle_set_eddsa_public_key`, `win_sparkle_init`,
  `win_sparkle_check_update_with_ui`).
- **Safe-by-default startup**: same contract as macOS, but there's no Info.plist on Windows to read
  from — the feed URL / public key are baked in at configure time as `target_compile_definitions`
  string literals (`SYNTH_UPDATE_FEED_URL_STR` / `SYNTH_WINSPARKLE_PUBLIC_KEY_STR`) rather than
  merged into a bundle resource. Empty either one and the updater never calls `win_sparkle_init()`.
- **Graceful shutdown, not a kill**: WinSparkle asks the host to close (via
  `win_sparkle_set_can_shutdown_callback` / `win_sparkle_set_shutdown_request_callback`) right
  after launching the downloaded installer, from a background thread — it does not terminate the
  process itself. `WinSparkleUpdateManager.cpp`'s callback calls `JUCEApplicationBase::quit()`,
  which is documented safe to call from any thread.
- **A real installer, not a raw exe**: WinSparkle's default behavior (no
  `win_sparkle_set_user_run_installer_callback` override) is to execute whatever file the appcast
  enclosure points at. Running the bare portable `Agent Synth.exe` as "the update" would just
  launch a second instance rather than install anything, so this task also added an NSIS installer
  (`installer/windows/AgentSynth.nsi`) — CI now ships `AgentSynthSetup.exe` as the Windows release
  artifact instead of the raw exe (see the SmartScreen download-page note in P5·5's doc for what
  that filename change means for the checksum-verification copy).
  - **Per-user install** (`$LOCALAPPDATA\AgentSynth`, HKCU registry, `RequestExecutionLevel user`)
    — deliberately not Program Files. Not strictly required for correctness (WinSparkle already
    shows its own "update available" dialog before running the installer, so a UAC prompt mid-flow
    wouldn't break anything), but avoids the prompt entirely, matching "no code-signing cert yet,
    no admin story" positioning.
  - Upgrade-in-place overwrites files in the existing install dir — safe because WinSparkle has
    already asked the running app to quit (previous bullet) by the time the installer runs, so
    there's no file-lock conflict.
- **Plugin target links WinSparkle *delay-loaded*, not the app's hard import-lib link.**
  `MainComponent` owns an `UpdateManager` member unconditionally, so `AgentSynthPlugin_VST3` needs
  the same source/library as the app just to satisfy the linker — mirroring the exact reason the
  macOS plugin links Sparkle *weakly*: `juce_vst3_helper` loads the freshly-linked plugin DLL right
  after linking (to write `moduleinfo.json`), before this target's own `WinSparkle.dll`-copy step
  has run. Unlike macOS's weak-framework option, Windows import-lib linking resolves ALL imports at
  load time regardless of whether a function is ever called, so a hard link would hit the same
  failure the moment `WinSparkle.dll` isn't next to the plugin DLL yet. `/DELAYLOAD:WinSparkle.dll`
  defers resolution to first *call* of a WinSparkle function, and the plugin's inert (empty
  key/URL) path never calls one. **This has not been verified against a real Windows build in this
  environment** (no Windows toolchain available) — if CI's Windows plugin build fails at the
  `juce_vst3_helper` step with a "WinSparkle.dll not found" error, this is the first thing to
  revisit.

## Generating the EdDSA signing key (one-time, you run this — not CI)

### macOS (Sparkle)

Sparkle signs update archives with an EdDSA (Ed25519) key pair, kept in your macOS Keychain. This
is separate from Apple code signing (P5·2) — it's Sparkle's own update-integrity mechanism.

1. Build once locally on macOS (`cmake -S . -B build && cmake --build build`) so Sparkle's
   distribution is fetched to `build/_deps/sparkle-src/`.
2. Generate (or look up an existing) key pair, stored in your login Keychain:
   ```bash
   ./build/_deps/sparkle-src/bin/generate_keys
   ```
   This prints the public key and the exact `SUPublicEDKey` Info.plist snippet.
3. Set the public key as a **repository variable** (not a secret — it's public by design):
   Settings ▸ Secrets and variables ▸ Actions ▸ Variables ▸ new variable `SPARKLE_PUBLIC_KEY`.
   Also pass it locally when you want to test the real flow: `-DSYNTH_SPARKLE_PUBLIC_KEY=<key>`.
4. Export the private key for CI to use when signing releases:
   ```bash
   ./build/_deps/sparkle-src/bin/generate_keys -x /tmp/sparkle_private_key
   ```
   Add its contents as the **repository secret** `SPARKLE_PRIVATE_KEY`, then delete
   `/tmp/sparkle_private_key`. Never commit it.

Once both exist, `build-artifacts.yml`'s `publish-appcast` job (gated on
`vars.SPARKLE_PUBLIC_KEY != ''`) starts running for real instead of skipping.

### Windows (WinSparkle)

WinSparkle uses the same EdDSA (Ed25519) primitive as Sparkle, but its own, **separate and
independent** key pair — never reuse the Sparkle key here.

1. Download WinSparkle's prebuilt release (the same `SYNTH_WINSPARKLE_URL` pin as
   `cmake/DependencyVersions.cmake`) and find `bin/winsparkle-tool.exe`. This is a Windows binary —
   run it on a Windows machine.
2. Generate a key pair:
   ```
   winsparkle-tool.exe generate-key --file private.key
   ```
   This documented command writes the private key to `private.key`. **Not independently verified
   in this environment** (no Windows machine to run the `.exe` on) exactly how the public key is
   printed/obtained from the same invocation — run `winsparkle-tool.exe generate-key --help` first
   and confirm before treating this as gospel; adjust these steps if the real CLI differs.
3. Set the public key as a **repository variable** (not a secret — it's public by design):
   Settings ▸ Secrets and variables ▸ Actions ▸ Variables ▸ new variable `WINSPARKLE_PUBLIC_KEY`.
   Also pass it locally to test the real flow: `-DSYNTH_WINSPARKLE_PUBLIC_KEY=<key>`.
4. Add `private.key`'s contents as the **repository secret** `WINSPARKLE_PRIVATE_KEY`, then delete
   the local file. Never commit it.

Once both exist, `build-artifacts.yml`'s `publish-appcast-windows` job (gated on
`vars.WINSPARKLE_PUBLIC_KEY != ''`) starts running for real instead of skipping, and
`promote-release.yml` starts requiring an `appcast-windows.xml` release asset before it will
promote a tag to stable (see "Promoting a build to stable" below).

## CI: what's automated vs. what isn't

Automated (`build-artifacts.yml`):
- The macOS build job bakes in `CFBundleVersion` = `github.run_number` and (once the variable
  exists) the real `SUPublicEDKey`.
- After the `release` job tags and publishes the GitHub Release, `publish-appcast` downloads that
  release's macOS zip, runs Sparkle's `generate_appcast` tool against it (signing with
  `SPARKLE_PRIVATE_KEY`, `--download-url-prefix` pointing at that same release's GitHub asset URLs),
  and uploads the resulting `appcast.xml` back onto the release as an asset.
- Publishing that `appcast.xml` to `https://agentsynth.app/updates/appcast.xml` — the `SUFeedURL`
  baked into the app (`SYNTH_UPDATE_FEED_URL` in `CMakeLists.txt`) — is automated too, but the
  workflow lives in the **synth-platform** repo, not here: `synth-platform/.github/workflows/deploy-web.yml`
  fetches `https://github.com/Diabl0269/agentsynth/releases/latest/download/appcast.xml` and
  redeploys `apps/web` — with the fetched appcast dropped at `dist/updates/appcast.xml` — only when
  it changed. That alias only ever resolves to the newest **non-prerelease** release, which is
  exactly the point (see "Promoting a build to stable" below): a per-push prerelease never reaches
  real users' auto-update feed. A push to that repo's `apps/web` always redeploys regardless of the
  appcast; a daily schedule in that same workflow is a safety net in case a promotion's manual
  `deploy-web` trigger (see below) gets forgotten. The zip itself still stays on GitHub Releases
  (`--download-url-prefix` points there directly, so only the small `appcast.xml` file needed a
  second home).

- The Windows side (P5·6) mirrors this: the build job also bakes in `SYNTH_WINSPARKLE_PUBLIC_KEY`,
  builds an NSIS installer (`AgentSynthSetup.exe`) via `makensis`, and after `release`,
  `publish-appcast-windows` (gated on `vars.WINSPARKLE_PUBLIC_KEY != ''`, `runs-on: windows-latest`)
  signs that installer with `winsparkle-tool.exe`, hand-templates `appcast-windows.xml` (WinSparkle
  has no `generate_appcast`-equivalent directory scanner), and uploads it to the release. Publishing
  `appcast-windows.xml` to `https://agentsynth.app/updates/appcast-windows.xml` reuses the exact
  same `synth-platform/deploy-web.yml` mechanism as the macOS `appcast.xml` — a small, additive
  extension of P5·7's existing infrastructure rather than a new pipeline.

**Not automated**: nothing on the appcast-publishing path for either platform (P5·7 done
2026-08-20 for macOS; P5·6 extends the same infrastructure for Windows). "Check for Updates" now
round-trips against a real, live feed once a key exists for that platform; the 404-then-silent-no-op
behavior described in earlier drafts of this doc no longer applies to that step.

## Promoting a build to stable

Every push to `main` ships a GitHub **prerelease** (`build-artifacts.yml`) — that's continuous
build/QA output, not something real users should auto-update onto. `promote-release.yml` is the
separate, manual step that turns one specific already-built prerelease into the actual release
Sparkle/WinSparkle and the download page serve (P5·10):

1. Pick a tag that's been running/tested and looks good — `gh release list --repo Diabl0269/agentsynth`.
2. Actions ▸ Promote Release ▸ Run workflow, with that tag (e.g. `v0.112.0`) as the input. (Only the
   repo owner can run it — the job checks `github.actor`.)
3. The job re-publishes the existing release as `prerelease: false` — it does **not** rebuild
   anything. It first asserts the tag isn't a draft, is currently a prerelease, and carries
   `appcast.xml` and `SHA256SUMS.txt` assets (plus `appcast-windows.xml`, but only once
   `WINSPARKLE_PUBLIC_KEY` exists — see "Windows (WinSparkle)" above), and fails loudly rather than
   promoting a release that would leave auto-update or the download page's checksum link 404ing.
4. GitHub's `/releases/latest` (and `/releases/latest/download/<asset>`) now resolves to this tag.
   Trigger `synth-platform`'s `deploy-web` workflow by hand (`workflow_dispatch`) so
   `agentsynth.app/updates/appcast.xml` picks up the promoted build immediately — otherwise its
   daily schedule catches it within 24h regardless.

**How often**: whenever a batch of merged commits is worth shipping to real users — there's no
fixed cadence. There's deliberately no scheduled/automatic promotion: every previous release stays
a permanent prerelease, so skipping a promotion costs nothing, and auto-update only ever moves
forward on a promotion you chose.

## What's not built yet

- **Notarization** — CI's existing `codesign --force --deep -s -` is ad-hoc signing, not a real
  Developer ID + notarization (that's P5·2). Sparkle's own update-signature check (the EdDSA key
  above) doesn't require notarization to function, but Gatekeeper may still warn on the *initial*
  install until P5·2 lands — that's an existing, separate problem this task doesn't change.
- **Windows code signing** — `AgentSynthSetup.exe` is unsigned (P5·2 defers the Windows
  certificate past first revenue), so SmartScreen still warns on install, same as the raw exe did
  before P5·6 — WinSparkle's EdDSA signature check is a separate, orthogonal mechanism (update
  integrity) and doesn't touch this. Existing, unchanged problem — see the marketing site's
  SmartScreen explainer (P5·5).

## Testing

There's no automated (GoogleTest) coverage for this feature. `UpdateManager`'s only logic is two
one-line methods delegating straight to `SPUStandardUpdaterController` — a native macOS GUI
framework with its own (separately maintained, widely-used) test suite upstream — so there's
nothing headless-testable to lock down here; the real risk surface is build/packaging/CI wiring,
which GoogleTest can't exercise either. Verify manually instead:

**1. Compiles and embeds correctly:**
```bash
cmake -S . -B build && cmake --build build
otool -L "build/AgentSynth_artefacts/AgentSynth.app/Contents/MacOS/Agent Synth" | grep Sparkle
# expect: @rpath/Sparkle.framework/Versions/B/Sparkle
ls "build/AgentSynth_artefacts/AgentSynth.app/Contents/Frameworks/Sparkle.framework"
```

**2. Inert without a key** (the default state right now): launch the built app, open Help ▸ Check
for Updates… — it should be greyed out, and no misconfiguration alert should appear.

**3. Full local update flow**, once you've generated a key (see above):
- Configure with your real public key: `cmake -S . -B build -DSYNTH_SPARKLE_PUBLIC_KEY=<your key> -DSYNTH_BUILD_NUMBER=1` and rebuild.
- Build a second copy with a *higher* build number (e.g. `-DSYNTH_BUILD_NUMBER=2`), zip its
  `.app`, and run Sparkle's `generate_appcast` against a directory containing just that zip:
  ```bash
  ./build/_deps/sparkle-src/bin/generate_appcast /path/to/dir-with-the-zip
  ```
- Serve that directory locally: `python3 -m http.server 8000` from inside it.
- Point the *first* (lower build number) app at it for this one test run:
  `-DSYNTH_UPDATE_FEED_URL=http://127.0.0.1:8000/appcast.xml`. Sparkle's App Transport Security
  normally requires HTTPS — `http://127.0.0.1` is exempted by default (loopback), so no ATS
  exception plist entry should be needed; if it is blocked, add a Debug-only
  `NSAppTransportSecurity` / `NSExceptionDomains` entry for `127.0.0.1`, never for the real feed URL.
  Rebuild the first app with this override.
- Launch it, Help ▸ Check for Updates… — Sparkle should offer the higher-numbered build, download,
  verify the EdDSA signature, and install it.

**4. Signature-rejection check** (the actual security-relevant case): re-run `generate_appcast`
against the same zip but with a different, throwaway key (`generate_keys --account test-throwaway`
first), or hand-edit the `<sparkle:edSignature>` in the generated `appcast.xml`. Confirm Sparkle
refuses the update instead of installing it.

**5. End-to-end against the real deployment** — `https://agentsynth.app/updates/appcast.xml` is
live (P5·7); with a real key and a signed release, repeat step 3 against production instead of a
local server.

### Windows (WinSparkle) manual verification

None of this has been run against a real Windows machine or real keys in this environment (no
Windows toolchain, no generated WinSparkle key pair) — treat everything below as the acceptance
test for whoever first runs it on real hardware, not as confirmed-working.

**1. Compiles and the DLL sits beside the exe:**
```
cmake -S . -B build -G Ninja && cmake --build build
dumpbin /DEPENDENTS "build\AgentSynth_artefacts\Release\Agent Synth.exe" | findstr WinSparkle
:: expect: WinSparkle.dll
dir "build\AgentSynth_artefacts\Release\WinSparkle.dll"
```

**2. Inert without a key** (the default state right now): launch the built app, open Help ▸ Check
for Updates… — it should be greyed out, and no misconfiguration alert should appear.

**3. Installer builds and installs per-user:**
```
makensis /DVERSION=0.13.2 /DSTAGE_DIR="build\AgentSynth_artefacts\Release" installer\windows\AgentSynth.nsi
installer\windows\AgentSynthSetup.exe
```
Confirm it installs to `%LOCALAPPDATA%\AgentSynth` with **no UAC prompt**, creates Start Menu
shortcuts, and appears in Add/Remove Programs. Confirm `AgentSynthSetup.exe /S` runs silently.

**4. Full local update flow**, once you've generated a WinSparkle key (see above):
- Configure with the real public key: `cmake -S . -B build -DSYNTH_WINSPARKLE_PUBLIC_KEY=<your key> -DSYNTH_BUILD_NUMBER=1` and rebuild + package a first installer.
- Build/package a second installer with a higher `-DSYNTH_BUILD_NUMBER=2`.
- Sign the second installer: `winsparkle-tool.exe sign -f private.key AgentSynthSetup.exe`.
- Hand-write a local `appcast-windows.xml` (same shape `publish-appcast-windows` generates in CI —
  see `build-artifacts.yml`) pointing at the second installer, and serve it:
  `python -m http.server 8000` from the directory containing both.
- Point the *first* (lower build number) app at it for this one test run:
  `-DSYNTH_UPDATE_FEED_URL=http://127.0.0.1:8000/appcast-windows.xml`. Rebuild the first app with
  this override.
- Launch it, Help ▸ Check for Updates… — WinSparkle should offer the higher-numbered build,
  download, verify the EdDSA signature, ask the app to quit (confirm it actually quits — this is
  the untested `handleShutdownRequest` callback), and run the installer.

**5. Signature-rejection check**: re-sign with a different, throwaway key, or hand-edit the
`sparkle:edSignature` attribute in the local `appcast-windows.xml`. Confirm WinSparkle refuses the
update instead of installing it.

**6. End-to-end against the real deployment** — once `https://agentsynth.app/updates/appcast-windows.xml`
is live (small extension to the synth-platform `deploy-web.yml` P5·7 already built) and a real key
+ signed release exist, repeat step 4 against production instead of a local server.

**`promote-release.yml`**: no automated coverage — it's ~20 lines of `gh` CLI calls against GitHub's
own Releases API, which has no local/offline equivalent to test against (mirrors why the
`ci-cache-check.test.sh` shell-test pattern doesn't apply here: there's no repo state to assert on,
only a live API call). Verify by running it against a real prerelease tag and confirming: the guard
rejects a run from any actor but the owner, rejects a tag missing `appcast.xml`/`SHA256SUMS.txt`,
and `gh release view <tag>` shows `isPrerelease: false` after a successful run.
