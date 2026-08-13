# Distribution & Auto-Update

Direct-download distribution (no app stores — see [P5·2](../CLAUDE.md) / the roadmap). This doc
covers how release builds get their version identity and how Sparkle (macOS) checks for updates.
WinSparkle (Windows) is a separate, not-yet-started roadmap task — see "What's not built yet" below.

## Version identity

Two numbers are baked into every macOS build, and they mean different things:

- **`CFBundleShortVersionString`** (`PROJECT_VERSION` in `CMakeLists.txt`, e.g. `0.13.2`) — the
  human-facing marketing version. Hand-bumped by editing `project(AgentSynth VERSION ...)`.
- **`CFBundleVersion`** (`GRAVISYNTH_BUILD_NUMBER` cache var) — the value Sparkle actually compares
  to decide whether an update is available. This is **not** the same value on purpose:
  `build-artifacts.yml`'s release job (`mathieudutour/github-tag-action`) auto-tags every push to
  `main` with a semver bump, entirely independent of `CMakeLists.txt`. If `CFBundleVersion` only
  advanced when a human remembered to bump `PROJECT_VERSION`, Sparkle would either never detect a
  real update or always think one was available. Instead, CI passes
  `-DGRAVISYNTH_BUILD_NUMBER=${{ github.run_number }}` at configure time — a value that's known
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

**Not automated**: deploying `appcast.xml` to `https://agentsynth.app/updates/appcast.xml` — the
`SUFeedURL` baked into the app (`SYNTH_UPDATE_FEED_URL` in `CMakeLists.txt`). The zip itself stays
on GitHub Releases (`--download-url-prefix` points there directly, so only the small `appcast.xml`
file needs hosting elsewhere); publishing that one file to agentsynth.app still needs a manual step
or a future automated one (P5·1 marketing site / P5·4 both touch that domain's Cloudflare Pages
deployment, which this repo has no credentials for). Until that's wired up, "Check for Updates"
starts (once a key exists) but the feed check itself 404s — Sparkle's default UX for that is a
silent no-op on a background check and a plain "couldn't check" alert only if the user explicitly
clicks the menu item, so this fails safely rather than looking broken.

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
- Configure with your real public key: `cmake -S . -B build -DSYNTH_SPARKLE_PUBLIC_KEY=<your key> -DGRAVISYNTH_BUILD_NUMBER=1` and rebuild.
- Build a second copy with a *higher* build number (e.g. `-DGRAVISYNTH_BUILD_NUMBER=2`), zip its
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

**5. End-to-end against the real deployment** — once the appcast is actually hosted at
`https://agentsynth.app/updates/appcast.xml` (see "what's not built yet" above) and a signed
release exists, repeat step 3 against production. This can't be verified until then; treat it as
the acceptance test for whichever task wires up that deployment.
