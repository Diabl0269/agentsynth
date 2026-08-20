# Distribution & Auto-Update

Direct-download distribution (no app stores — see [P5·2](../CLAUDE.md) / the roadmap). This doc
covers how release builds get their version identity and how Sparkle (macOS) checks for updates.
WinSparkle (Windows) is a separate, not-yet-started roadmap task — see "What's not built yet" below.

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

## Generating the EdDSA signing key (one-time, you run this — not CI)

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

**Not automated**: nothing on the appcast-publishing path anymore (P5·7, done 2026-08-20). "Check
for Updates" now round-trips against a real, live feed once a key exists; the 404-then-silent-no-op
behavior described in earlier drafts of this doc no longer applies to that step. What's still
missing is WinSparkle (P5·6, see below) — the feed above only ever contains a macOS item.

## Promoting a build to stable

Every push to `main` ships a GitHub **prerelease** (`build-artifacts.yml`) — that's continuous
build/QA output, not something real users should auto-update onto. `promote-release.yml` is the
separate, manual step that turns one specific already-built prerelease into the actual release
Sparkle/WinSparkle and the download page serve (P5·10):

1. Pick a tag that's been running/tested and looks good — `gh release list --repo Diabl0269/agentsynth`.
2. Actions ▸ Promote Release ▸ Run workflow, with that tag (e.g. `v0.112.0`) as the input. (Only the
   repo owner can run it — the job checks `github.actor`.)
3. The job re-publishes the existing release as `prerelease: false` — it does **not** rebuild
   anything. It first asserts the tag isn't a draft, is currently a prerelease, and carries both
   `appcast.xml` and `SHA256SUMS.txt` assets, and fails loudly rather than promoting a release that
   would leave auto-update or the download page's checksum link 404ing.
4. GitHub's `/releases/latest` (and `/releases/latest/download/<asset>`) now resolves to this tag.
   Trigger `synth-platform`'s `deploy-web` workflow by hand (`workflow_dispatch`) so
   `agentsynth.app/updates/appcast.xml` picks up the promoted build immediately — otherwise its
   daily schedule catches it within 24h regardless.

**How often**: whenever a batch of merged commits is worth shipping to real users — there's no
fixed cadence. There's deliberately no scheduled/automatic promotion: every previous release stays
a permanent prerelease, so skipping a promotion costs nothing, and auto-update only ever moves
forward on a promotion you chose.

## What's not built yet

- **Windows (WinSparkle)** — tracked as a separate roadmap task (macOS-first was a deliberate scope
  cut: P5·2 defers the Windows code-signing cert past first revenue, and someone testing on Windows
  recently reported no audio, so a full Windows pass belongs together regardless).
- **Notarization** — CI's existing `codesign --force --deep -s -` is ad-hoc signing, not a real
  Developer ID + notarization (that's P5·2). Sparkle's own update-signature check (the EdDSA key
  above) doesn't require notarization to function, but Gatekeeper may still warn on the *initial*
  install until P5·2 lands — that's an existing, separate problem this task doesn't change.

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

**`promote-release.yml`**: no automated coverage — it's ~20 lines of `gh` CLI calls against GitHub's
own Releases API, which has no local/offline equivalent to test against (mirrors why the
`ci-cache-check.test.sh` shell-test pattern doesn't apply here: there's no repo state to assert on,
only a live API call). Verify by running it against a real prerelease tag and confirming: the guard
rejects a run from any actor but the owner, rejects a tag missing `appcast.xml`/`SHA256SUMS.txt`,
and `gh release view <tag>` shows `isPrerelease: false` after a successful run.
