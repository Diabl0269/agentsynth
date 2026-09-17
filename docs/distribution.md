# Distribution & Auto-Update

Direct-download distribution (no app stores). This doc
covers how release builds get their version identity and how Sparkle (macOS) / WinSparkle
(Windows) check for updates.

## Version identity

Two numbers are baked into every macOS build, and they mean different things:

- **`CFBundleShortVersionString`** (`SYNTH_MARKETING_VERSION` cache var in `CMakeLists.txt`, e.g.
  `0.13.2`) — the human-facing marketing version, also used as the plugin's own `VERSION` and the
  Windows installer's Add/Remove Programs entry. It defaults to `PROJECT_VERSION` (the version
  declared by `project(AgentSynth VERSION ...)`), which is what a plain local build gets. On CI,
  `build-artifacts.yml` no longer relies on that default: a `version` job runs *before* the build
  matrix and does a `dry_run` computation of the same tag the release job below is about to
  publish (`mathieudutour/github-tag-action`), strips any dry-run prerelease suffix, and passes the
  result to every build leg via `-DSYNTH_MARKETING_VERSION`. The release job then tags with
  `custom_tag` set to that same computed value, so the version embedded in a release build is
  guaranteed to equal the tag it ships under — nobody has to hand-bump `project()` to keep them in
  sync.
- **`CFBundleVersion`** (`SYNTH_BUILD_NUMBER` cache var) — the value Sparkle actually compares
  to decide whether an update is available. This is **not** the same value as
  `SYNTH_MARKETING_VERSION` on purpose: CI passes `-DSYNTH_BUILD_NUMBER=${{ github.run_number }}`
  at configure time — a value that's known before the build starts and increases every workflow
  run, independent of the `version` job's tag computation or of semver formatting. Local/dev
  builds default to `0` and are never distributed, so this doesn't matter for them.

## What's New (build-time, no network)

A deliberately cheap companion to Sparkle/WinSparkle above: a "What's New..." Help-menu item (macOS, `AppCommands::whatsNew`) and a link on the welcome screen's footer (see `docs/architecture.md`'s "Welcome screen" subsection, T114/P8-10) that show recent changes with **no HTTP client and no offline-handling complexity** — everything is sourced from this repo's own local git history at CMake **configure** time, so it works even in an airgapped build.

- **Generation.** The root `CMakeLists.txt`, right after the `SYNTH_BUILD_NUMBER` block, runs two `execute_process` calls against `CMAKE_SOURCE_DIR`: `git describe --tags --abbrev=0` for a release tag (falling back to the literal `"development build"` if it fails or the repo has no tags — a shallow clone must not break the configure) and `git log --pretty=format:%s -n 15` for the last 15 commit **subject lines** (falling back to `"No release history available."` for a source tarball with no `.git` at all). Both captures are sanitized — escaped for a C string literal, then reduced to the printable-ASCII range plus newline (CMake's regex engine has no `\xNN` hex-escape syntax, so the bracket expression spells the range literally: `[^ -~\n]`) — before being written into `${CMAKE_BINARY_DIR}/generated/WhatsNewData.h` as `synth::whatsnew::kReleaseTag` / `kHighlights[]` / `kHighlightsCount`. `AppUI` (and `Tests`, which compiles the `Source/MainComponent/` units directly rather than linking `AppUI` — see its `CMakeLists.txt` comment) both get `${CMAKE_BINARY_DIR}/generated` on their private include path.
- **Configure-time, not build-time.** This is inline `CMakeLists.txt` code, not a custom build-time command, so it captures git state once per `cmake -S . -B build` — a plain incremental `cmake --build` shows whatever the last configure captured. That's an intentional trade for simplicity: the alternative (a real build-time command) would re-run `git` on every invocation for a feature whose whole point is being cheap.
- **Sensitive-info boundary: commit subject lines only.** Never the commit body, never author name or email — exactly the line the founder-approved scope drew ("as long as it doesn't have any sensitive information"). This project's own commit history is public, so subject lines alone are safe to ship in every build.
- **Rendering.** `MainComponent::showWhatsNewDialog()` is a synchronous `juce::AlertWindow` listing `kReleaseTag` and each `kHighlights[]` entry as a bullet line — no async state machine, nothing for a test to accidentally trigger (tests assert the generated data's mechanism only, never specific commit text, since that's machine-dependent).

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
  artifact instead of the raw exe.
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
is separate from Apple code signing — it's Sparkle's own update-integrity mechanism.

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

**Must run on a real Windows machine you control, not this repo's CI.** `agentsynth` is a public
repo — a GitHub Actions artifact or log is downloadable by any signed-in GitHub user for as long as
it exists, so a CI job is not a safe place for the private key to ever pass through, even briefly.
Generate locally and push the secret from your own authenticated `gh` session instead.

1. Download WinSparkle's prebuilt release (the same `SYNTH_WINSPARKLE_URL` pin as
   `cmake/DependencyVersions.cmake`) and find `bin/winsparkle-tool.exe`.
2. CLI confirmed against the real `.exe` (`winsparkle-tool.exe --help` / `<subcommand> --help`):
   ```
   winsparkle-tool.exe generate-key --file private.key
   winsparkle-tool.exe public-key --private-key-file private.key   # prints the public key — safe to share/log
   winsparkle-tool.exe sign --private-key-file private.key <file>  # matches the CI signing step below
   ```
   `generate-key` only writes `private.key`; run `public-key` against that file to get the public
   key to hand off.
3. Set the public key as a **repository variable** (not a secret — it's public by design):
   `gh variable set WINSPARKLE_PUBLIC_KEY --body "<key>"`, or Settings ▸ Secrets and variables ▸
   Actions ▸ Variables. Also pass it locally to test the real flow:
   `-DSYNTH_WINSPARKLE_PUBLIC_KEY=<key>`.
4. Add `private.key`'s contents as the **repository secret** `WINSPARKLE_PRIVATE_KEY` directly from
   the Windows machine (or copy it to wherever your `gh` session already has repo admin access):
   `gh secret set WINSPARKLE_PRIVATE_KEY < private.key`. Then delete the local file. Never commit
   it, and never paste it into a chat/CI log/artifact.

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
  workflow lives in the private backend repo, not here. It fetches
  `https://github.com/Diabl0269/agentsynth/releases/latest/download/appcast.xml` and
  redeploys `apps/web` — with the fetched appcast dropped at `dist/updates/appcast.xml` — only when
  it changed. That alias only ever resolves to the newest **non-prerelease** release, which is
  exactly the point (see "Promoting a build to stable" below): a per-push prerelease never reaches
  real users' auto-update feed. A push to that repo's `apps/web` always redeploys regardless of the
  appcast; a daily schedule in that same workflow is a safety net in case a promotion's manual
  `deploy-web` trigger (see below) gets forgotten. The zip itself still stays on GitHub Releases
  (`--download-url-prefix` points there directly, so only the small `appcast.xml` file needed a
  second home).

- The Windows side mirrors this: the build job also bakes in `SYNTH_WINSPARKLE_PUBLIC_KEY`,
  builds an NSIS installer (`AgentSynthSetup.exe`) via `makensis`, and after `release`,
  `publish-appcast-windows` (gated on `vars.WINSPARKLE_PUBLIC_KEY != ''`, `runs-on: windows-latest`)
  signs that installer with `winsparkle-tool.exe`, hand-templates `appcast-windows.xml` (WinSparkle
  has no `generate_appcast`-equivalent directory scanner), and uploads it to the release. Publishing
  `appcast-windows.xml` to `https://agentsynth.app/updates/appcast-windows.xml` reuses the exact
  same hosted-backend deploy mechanism as the macOS `appcast.xml` — a small, additive
  extension of the existing infrastructure rather than a new pipeline.

**Not automated**: nothing on the appcast-publishing path for either platform (macOS shipped
2026-08-20; Windows extends the same infrastructure). "Check for Updates" now
round-trips against a real, live feed once a key exists for that platform; the 404-then-silent-no-op
behavior described in earlier drafts of this doc no longer applies to that step.

## Release asset upload reliability (FRO151/FRO160, 2026-09-17/18)

Two of four `build-artifacts.yml` runs on 2026-09-17 built every platform successfully but still
published a **half-populated** prerelease: `softprops/action-gh-release@v1`'s asset uploader is a
single, unwrapped `fetch()` per file with no retry, so a single transient GitHub error (`received
status code 502` for v0.240.0's `SHA256SUMS.txt`; an HTML error page instead of JSON — `invalid
json response body ... Unexpected token '<', "<!DOCTYPE "` — for v0.239.0's
`AgentSynth-Windows-x64-Plugins.zip`) aborted the step immediately, stranding whatever had already
uploaded and skipping `publish-appcast`/`publish-appcast-windows` below (both `needs: [release]`,
and the `release` job itself came back `failure`).

- **Fix attempt 1 (#407, 2026-09-17) — bump to `action-gh-release@v3` for retries.** Based on the
  theory that v3's Octokit client pulls in `@octokit/plugin-retry` and transparently retries
  5xx/network failures. **This did not hold up** (found while diagnosing v0.242.0's failure below):
  v3's `dist/index.js` contains no `@octokit/plugin-retry`; the only retry layer in the bundle is an
  undici-level `RetryHandler` whose default retryable methods are `GET/HEAD/OPTIONS/PUT/DELETE/
  TRACE` — POST, what an asset upload is, is excluded by default.
- **Verify, don't trust the exit code.** A `Verify published release assets` step runs right after
  the upload (`if: always() && steps.tag_version.outcome == 'success'`, so it still prints its diff
  even if the upload step itself failed outright). It computes the *expected* asset set from
  `artifacts/` — the same directory `Download All Artifacts` + `Generate checksums` just
  populated, so it tracks the build matrix automatically instead of a hardcoded platform list — and
  compares it against `gh release view "$TAG" --json assets`, printing a missing/unexpected diff and
  failing the job on any mismatch. `appcast.xml`/`appcast-windows.xml` are deliberately excluded
  from the comparison: `publish-appcast`/`publish-appcast-windows` upload those *after* this job
  finishes, so they're legitimately absent from the release at this point. This step worked exactly
  as designed on v0.242.0 (below) — it caught the loss that the upload step's own reporting missed.

**v0.242.0 (run 35272699011, 2026-09-17): 4 of 7 assets lost, root-caused.** `Create Release`
(v3, `files: artifacts/*`) logged all 7 "Uploading ..." lines within 1ms of each other, then
`##[error]Error creating asset temp dir` ~300ms later, then 3 more "Uploaded" lines over the next
second; the other 4 assets (including the Linux standalone) never got an "Uploaded" line. Cause,
confirmed by reading v3's compiled bundle (`dist/index.js` — the string doesn't appear in `src/`):
v3's `files:` path fires every asset **concurrently** via `Promise.all` (sequential only if the
action's own `preserve_order` input is set, which this workflow didn't set); `Promise.all` rejects
on the *first* rejected promise without cancelling the rest, so uploads already in flight kept
racing to finish (the 3 that landed) while the remainder were abandoned mid-request. The
`"Error creating asset temp dir"` string itself is not in v3's bundle at all — no code path in it
creates a temp directory for an asset — so it is GitHub's own upload-API error message, surfaced
raw through Octokit's `RequestError` (not through v3's own `Failed to upload release asset X...`
wrapping, which only wraps a *completed* non-201 response). Because `Promise.all` only surfaces the
first rejection, **the log does not attribute the error to a specific asset** — it is plausible but
not confirmed that it was the Linux standalone's upload (the only asset in this batch with a space
in its filename, and the last one logged as "Uploading"); it could equally have been one of the
other three that never got an "Uploaded" line.

**Fix (this task, FRO151/FRO160).** Two independent changes:

1. **Stop sending a filename with a space.** `build-artifacts.yml`'s "Package Linux Artifact" step
   now copies the raw Linux executable straight to `artifact/Agent.Synth` (renamed at the copy
   point only — the JUCE product name / build output stays `Agent Synth`, `SYNTH_PRODUCT_NAME` in
   `CMakeLists.txt`, unchanged). `Agent.Synth` was already the name every past release published
   under (GitHub silently rewrote the space to a period on upload), so this is a no-op for
   `synth-platform/apps/web/src/lib/releases.ts`'s hardcoded `linuxStandalone: "Agent.Synth"` and
   for `apps/web/src/pages/download.astro`'s existing user-facing instructions. This is not proven
   to be *the* cause of the v0.242.0 error above, but it removes the one variable that was ever
   anomalous about that asset, and it closes a real, separate bug (FRO160): `SHA256SUMS.txt` is
   generated from the *local* `artifacts/` directory (`sha256sum *`), so before this change its
   `Agent Synth` line never matched the `Agent.Synth` name the asset actually published under —
   `sha256sum -c SHA256SUMS.txt` against the downloaded file always failed. Now that the local
   artifact is already named `Agent.Synth`, the checksum line matches the published name.
2. **Upload assets ourselves, sequentially, with our own retries — not via v3's `files:`.**
   `Create Release` (v3) now only creates/updates the release object (tag, name, body,
   draft/prerelease); a separate `Upload release assets` step loops over `artifacts/*` one file at
   a time, calling `gh release upload "$TAG" "$f" --clobber` up to 5 times with linear backoff per
   file, and fails the step (`exit 1`) if any file exhausts its retries. Sequential removes the
   in-flight-abandonment failure mode above (nothing else is racing when one upload fails); reusing
   `gh release upload` for the retry means each attempt reads the file itself rather than depending
   on whether a previously-consumed request body/stream can be replayed; the exit code is entirely
   ours, so there is no separate action-internal retry/swallow behavior left to audit.

`Verify published release assets` no longer normalizes spaces to periods before comparing — no
filename this workflow publishes contains a space any more, so a mismatch there is a real
missing/renamed asset, not an expected GitHub rewrite.

## Promoting a build to stable

Every push to `main` ships a GitHub **prerelease** (`build-artifacts.yml`) — that's continuous
build/QA output, not something real users should auto-update onto. `promote-release.yml` is the
separate, manual step that turns one specific already-built prerelease into the actual release
Sparkle/WinSparkle and the download page serve:

1. Pick a tag that's been running/tested and looks good — `gh release list --repo Diabl0269/agentsynth`.
2. Actions ▸ Promote Release ▸ Run workflow, with that tag (e.g. `v0.112.0`) as the input. (Only the
   repo owner can run it — the job checks `github.actor`.)
3. The job re-publishes the existing release as `prerelease: false` — it does **not** rebuild
   anything. It first asserts the tag isn't a draft, is currently a prerelease, and carries
   `appcast.xml` and `SHA256SUMS.txt` assets (plus `appcast-windows.xml`, but only once
   `WINSPARKLE_PUBLIC_KEY` exists — see "Windows (WinSparkle)" above), and fails loudly rather than
   promoting a release that would leave auto-update or the download page's checksum link 404ing.
4. GitHub's `/releases/latest` (and `/releases/latest/download/<asset>`) now resolves to this tag.
   Trigger the private backend repo's deploy-web workflow by hand (`workflow_dispatch`) so
   `agentsynth.app/updates/appcast.xml` picks up the promoted build immediately — otherwise its
   daily schedule catches it within 24h regardless.

**How often**: whenever a batch of merged commits is worth shipping to real users — there's no
fixed cadence. There's deliberately no scheduled/automatic promotion: every previous release stays
a permanent prerelease, so skipping a promotion costs nothing, and auto-update only ever moves
forward on a promotion you chose.

**A promotion can silently do nothing if you only check the workflow's overall run status.**
`promote-release.yml`'s `if: github.actor == 'Diabl0269'` guard means a run triggered by anyone
else *skips* the job entirely — the run still shows green. Check the job's own conclusion
(`skipped` vs `success`), not just that the workflow finished.

**`DOWNLOAD_CHANNEL` (and any other build-time env var for the site) has to be set in
`deploy-web.yml`, never in the Cloudflare Pages dashboard.** The `agentsynth-legal` Pages project
has no Git integration configured — it's a direct-upload target (`wrangler pages deploy dist`),
so Cloudflare never runs a build itself; a "build environment variable" set in its dashboard would
just sit there unused. `astro build` only ever runs on the GitHub Actions runner.

**If the live appcast still shows the wrong build after step 4, don't assume the fetch is broken —
check whether the value even reached the build.** This bit the first time FRO50 shipped a stable
release (2026-09-14): the site's `deploy-web.yml` build step sets `DOWNLOAD_CHANNEL=stable`
correctly, but its Turborepo `build` task had no `env` allowlist — Turborepo's strict env mode
silently hides any variable not listed there (same class of bug the private repo's `turbo.json`
already had a comment about, for a *different* variable, on the `test` task). The channel var never
reached `astro build`, so the download page kept resolving the newest prerelease instead of the
promoted one, and no error appeared anywhere — it just silently picked the wrong release. A build
that runs fine locally via `pnpm --filter @platform/web build` can still fail this way in CI, since
that direct invocation bypasses turbo (and its strict env mode) entirely — the only faithful local
repro is the actual CI invocation, `pnpm exec turbo run build --filter=@platform/web`.

**Also, a promoted release doesn't stay the newest release for long.** Every push to `main` keeps
minting a new prerelease (`build-artifacts.yml`) regardless of what was just promoted — so
"stable channel" only works correctly if the code actually filters for `isPrerelease === false`
(which `apps/web/src/lib/releases.ts` does); anything that accidentally falls back to "the newest
release, prerelease or not" will start serving the next prerelease within hours of a promotion,
not months.

### First real macOS auto-update test (2026-09-14, FRO50)

The first genuine end-to-end run of this whole pipeline — an old key-baked prerelease (v0.94.0,
built the same afternoon `SPARKLE_PUBLIC_KEY` was set) auto-updating to a freshly promoted release
(v0.202.0) — passed, but not on the first attempt. What actually happened, in order, since each
step is a real trap for a tester and not just a one-off:

1. **A `.zip` downloaded via `gh release download` or `curl` carries no `com.apple.quarantine`
   attribute** — only browsers (and a few other apps that opt into `LSQuarantine`) set it. Testing
   Gatekeeper behavior against a CLI-fetched build is testing nothing; the flag has to be added
   back (`xattr -w com.apple.quarantine "0081;...;Google Chrome;<uuid>"`) to reproduce what a real
   tester's browser download actually looks like to the OS.
2. **A quarantined, ad-hoc-signed (not Developer ID) app gets a hard Gatekeeper block with no
   "Open Anyway" button** in the first dialog — only "Move to Trash" / "Done". The bypass is:
   Done → System Settings ▸ Privacy & Security → click "Open Anyway" next to the blocked-app
   message → confirm the follow-up dialog (which *does* have a real Open button). The
   right-click-▸-Open workaround this doc's download-page copy already tells users about is a
   separate, also-valid path for the same underlying problem.
3. **A quarantined app launched without first being moved to `/Applications` runs under App
   Translocation** — a randomized, read-only `/private/var/folders/.../AppTranslocation/...` copy,
   silently, no dialog. Sparkle detects this itself and refuses to self-update from there: "Agent
   Synth can't be updated if it's running from the location it was downloaded to. Quit Agent
   Synth, move it into your Applications folder, relaunch it from there, and try again." This is a
   real first-run trap for anyone who runs the app straight from a Downloads folder without moving
   it — worth calling out explicitly in tester-facing instructions, not just implied.
4. Once actually running from `/Applications`, Check for Updates → Install Update → the EdDSA
   signature verified, the update downloaded and installed, and the app relaunched into the
   promoted build (`CFBundleVersion` 257, confirmed via `PlistBuddy` against the installed
   `Info.plist` — there is no About box, so this is the only reliable way to check post-update).
   The relaunched build's UI was also visibly newer (extra Pan knobs, a Show Timeline toggle, a
   Plugins library section) — confirms the binary actually changed, not just the version string.

**Two rough edges found along the way, tracked separately (not blockers for this promotion):**

- The Sparkle update dialog itself showed `0.13.2 (257) is now available — you have 0.13.2 (124)`
  — `CFBundleShortVersionString` had gone stale (hand-bumped, untouched since before the
  Gravisynth→AgentSynth rename) while the build number kept advancing normally, so both sides
  showed the same marketing version and the dialog read as broken even though it wasn't. Hand-bumped
  to `0.202.0` as a one-time fix at the time; `build-artifacts.yml` now reorders the build ahead of
  the tag mint with a `version` job that computes the release tag first and feeds it to every build
  leg as `SYNTH_MARKETING_VERSION` (see "Version identity" above), so this class of drift is no
  longer possible.
- Two short-lived helper processes (both `responsiblePid` = the main app) crashed with a
  `libmalloc` memory-corruption report within ~1s of the update relaunch, mid-`SecTrustVerify`/
  `SecKeyVerifySignature` activity — consistent with Sparkle's own verification subprocess, not
  literally our compiled code, but not yet root-caused. The main app process was unaffected and
  kept running normally throughout. Crash reports saved locally; tracked separately.

## What's not built yet

- **Notarization** — CI's existing `codesign --force --deep -s -` is ad-hoc signing, not a real
  Developer ID + notarization. Sparkle's own update-signature check (the EdDSA key
  above) doesn't require notarization to function, but Gatekeeper may still warn on the *initial*
  install until real notarization is added — that's an existing, separate problem this task doesn't change.
- **Windows code signing** — `AgentSynthSetup.exe` is unsigned (the Windows certificate hasn't
  been purchased yet), so SmartScreen still warns on install, same as the raw exe did
  before the installer replaced it — WinSparkle's EdDSA signature check is a separate, orthogonal
  mechanism (update integrity) and doesn't touch this. Existing, unchanged problem.

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
live; with a real key and a signed release, repeat step 3 against production instead of a
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
is live (a small extension to the hosted backend's existing deploy mechanism) and a real key
+ signed release exist, repeat step 4 against production instead of a local server.

**`promote-release.yml`**: no automated coverage — it's ~20 lines of `gh` CLI calls against GitHub's
own Releases API, which has no local/offline equivalent to test against (mirrors why the
`ci-cache-check.test.sh` shell-test pattern doesn't apply here: there's no repo state to assert on,
only a live API call). Verify by running it against a real prerelease tag and confirming: the guard
rejects a run from any actor but the owner, rejects a tag missing `appcast.xml`/`SHA256SUMS.txt`,
and `gh release view <tag>` shows `isPrerelease: false` after a successful run.
