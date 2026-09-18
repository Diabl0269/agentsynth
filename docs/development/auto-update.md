# Auto-Update

Sparkle on macOS, WinSparkle on Windows, behind one two-method interface
(`Source/Update/UpdateManager.h`: `isAvailable()`, `checkForUpdates()`). Version identity is in
[`distribution.md`](distribution.md#version-identity); the CI side that signs and publishes an
appcast is in [`releases.md`](releases.md).

## Sparkle (macOS)

- Fetched as a prebuilt `.xcframework` distribution via `FetchContent`
  (`cmake/DependencyVersions.cmake`'s `SYNTH_SPARKLE_URL`/`SYNTH_SPARKLE_SHA256`), APPLE-only. It is
  a binary artifact, not a CMake project, so `FetchContent_MakeAvailable` just populates it — no
  `add_subdirectory`.
- `SparkleUpdateManager.mm` wraps `SPUStandardUpdaterController`. The `.mm` file is compiled with
  `-fobjc-arc` explicitly (JUCE's own `.mm` sources are non-ARC — see the
  `set_source_files_properties` call in `CMakeLists.txt`), so this one bridge file manages its own
  memory without hand-written retain/release.
- **Safe-by-default startup.** The constructor reads `SUPublicEDKey`/`SUFeedURL` from the running
  app's `Info.plist` and only starts Sparkle's updater if **both** are non-empty. When inert,
  `isAvailable()` returns `false` and `MainComponent::getCommandInfo` marks the Help ▸ Check for
  Updates… menu item inactive (greyed out) rather than hiding it.

  **Why.** Without a real public key configured, Sparkle shows an "app is misconfigured" alert on
  every launch. This keeps dev builds, and any build made before a signing key exists, completely
  inert.
- **Framework embedding.** This project builds with Ninja, not Xcode, which has no automatic "Embed
  Frameworks" build phase. `CMakeLists.txt` copies `Sparkle.framework` into `Contents/Frameworks/`
  via a `POST_BUILD` custom command, using `ditto`, which preserves the framework's
  `Versions/Current` symlink structure, and sets `INSTALL_RPATH` to `@executable_path/../Frameworks`
  so the binary's `@rpath/Sparkle.framework/...` load command resolves. CI's existing `codesign
  --force --deep -s -` step re-signs the embedded framework along with the rest of the bundle, so no
  separate signing step is needed.
- **No App Sandbox / XPC services.** The app is not sandboxed, so the simpler non-sandboxed Sparkle
  integration applies (see Sparkle's
  [sandboxing docs](https://sparkle-project.org/documentation/sandboxing/) if that ever changes).
- **The plugin targets link Sparkle weakly, not the app's hard `-framework`.** `MainComponent`
  (shared via `AppUI`) owns an `UpdateManager` member unconditionally, so `AgentSynthPlugin_VST3` and
  `_AU` need the same `.mm` source and framework as the app just to satisfy the linker — but
  `juce_vst3_helper` dlopens the freshly-linked bundle immediately after linking, to write
  `moduleinfo.json`, *before* that target's own framework-embed step has run, so a hard dependency
  makes that load fail outright. `-weak_framework Sparkle` (`LC_LOAD_WEAK_DYLIB`) lets the bundle
  load regardless of whether Sparkle is present yet. This also matches actual intent: the plugin's
  `Info.plist` never gets `SUFeedURL`/`SUPublicEDKey` merged in — only the app's does — so
  `isAvailable()` is always `false` there. Sparkle is genuinely optional for the plugin, not just
  incidentally absent at one point in the build.

## WinSparkle (Windows)

- Fetched as a prebuilt binary distribution via `FetchContent`
  (`cmake/DependencyVersions.cmake`'s `SYNTH_WINSPARKLE_URL`/`SYNTH_WINSPARKLE_SHA256`), WIN32-only
  — the same "binary artifact, no `add_subdirectory`" shape as Sparkle's `.xcframework`. The
  distribution ships prebuilt per-arch (`Win32`/`x64`/`ARM64`) directories; only `x64` is wired up,
  matching this project's CI matrix and JUCE target architecture.
- `WinSparkleUpdateManager.cpp` presents the same two-method interface as the macOS side, wrapping
  WinSparkle's C API (`win_sparkle_set_appcast_url`, `win_sparkle_set_eddsa_public_key`,
  `win_sparkle_init`, `win_sparkle_check_update_with_ui`).
- **Safe-by-default startup**: the same contract as macOS, but there is no `Info.plist` on Windows
  to read from — the feed URL and public key are baked in at configure time as
  `target_compile_definitions` string literals (`SYNTH_UPDATE_FEED_URL_STR` /
  `SYNTH_WINSPARKLE_PUBLIC_KEY_STR`) rather than merged into a bundle resource. Empty either one and
  the updater never calls `win_sparkle_init()`.
- **Graceful shutdown, not a kill.** WinSparkle asks the host to close — via
  `win_sparkle_set_can_shutdown_callback` / `win_sparkle_set_shutdown_request_callback` — right
  after launching the downloaded installer, from a background thread. It does not terminate the
  process itself. The callback calls `JUCEApplicationBase::quit()`, which is documented safe to call
  from any thread.
- **A real installer, not a raw exe.** WinSparkle's default behaviour, with no
  `win_sparkle_set_user_run_installer_callback` override, is to execute whatever file the appcast
  enclosure points at. Running the bare portable `Agent Synth.exe` as "the update" would launch a
  second instance rather than install anything, so CI ships an NSIS installer
  (`installer/windows/AgentSynth.nsi`) as `AgentSynthSetup.exe` instead of the raw exe.
  - **Per-user install** (`$LOCALAPPDATA\AgentSynth`, HKCU registry, `RequestExecutionLevel user`),
    deliberately not Program Files. Not strictly required for correctness — WinSparkle shows its own
    "update available" dialog before running the installer, so a UAC prompt mid-flow would not break
    anything — but it avoids the prompt entirely, matching the "no code-signing cert, no admin
    story" position in [`distribution.md`](distribution.md#signing-state).
  - Upgrade-in-place overwrites files in the existing install dir, which is safe because WinSparkle
    has already asked the running app to quit by the time the installer runs, so there is no
    file-lock conflict.
- **The plugin target links WinSparkle delay-loaded, not the app's hard import-lib link.**
  `MainComponent` owns an `UpdateManager` member unconditionally, so `AgentSynthPlugin_VST3` needs
  the same source and library as the app just to satisfy the linker — mirroring the exact reason the
  macOS plugin links Sparkle *weakly*: `juce_vst3_helper` loads the freshly-linked plugin DLL right
  after linking, before this target's own `WinSparkle.dll`-copy step has run. Unlike macOS's
  weak-framework option, Windows import-lib linking resolves ALL imports at load time regardless of
  whether a function is ever called, so a hard link would hit the same failure the moment
  `WinSparkle.dll` is not next to the plugin DLL yet. `/DELAYLOAD:WinSparkle.dll` defers resolution
  to the first *call* of a WinSparkle function, and the plugin's inert (empty key/URL) path never
  calls one.

## Generating the EdDSA signing key

One-time, and run by a person, never by CI. macOS and Windows use the same EdDSA (Ed25519)
primitive but **separate and independent** key pairs — never reuse one for the other.

### macOS (Sparkle)

Sparkle signs update archives with a key pair kept in the macOS Keychain. This is separate from
Apple code signing; it is Sparkle's own update-integrity mechanism.

1. Build once locally on macOS (`cmake -S . -B build && cmake --build build`) so Sparkle's
   distribution is fetched to `build/_deps/sparkle-src/`.
2. Generate, or look up an existing, key pair, stored in the login Keychain:
   ```bash
   ./build/_deps/sparkle-src/bin/generate_keys
   ```
   This prints the public key and the exact `SUPublicEDKey` `Info.plist` snippet.
3. Set the public key as a **repository variable**, not a secret — it is public by design:
   Settings ▸ Secrets and variables ▸ Actions ▸ Variables ▸ new variable `SPARKLE_PUBLIC_KEY`. Pass
   it locally to test the real flow: `-DSYNTH_SPARKLE_PUBLIC_KEY=<key>`.
4. Export the private key for CI to sign releases with:
   ```bash
   ./build/_deps/sparkle-src/bin/generate_keys -x /tmp/sparkle_private_key
   ```
   Add its contents as the **repository secret** `SPARKLE_PRIVATE_KEY`, then delete
   `/tmp/sparkle_private_key`. Never commit it.

Once both exist, the release workflow's `publish-appcast` job (gated on `vars.SPARKLE_PUBLIC_KEY !=
''`) starts running for real instead of skipping.

### Windows (WinSparkle)

**Must run on a real Windows machine you control, not this repo's CI.** `agentsynth` is a public
repo — a GitHub Actions artifact or log is downloadable by any signed-in GitHub user for as long as
it exists, so a CI job is not a safe place for the private key to ever pass through, even briefly.
Generate locally and push the secret from your own authenticated `gh` session instead.

1. Download WinSparkle's prebuilt release (the same `SYNTH_WINSPARKLE_URL` pin as
   `cmake/DependencyVersions.cmake`) and find `bin/winsparkle-tool.exe`.
2. The CLI, confirmed against the real `.exe`:
   ```
   winsparkle-tool.exe generate-key --file private.key
   winsparkle-tool.exe public-key --private-key-file private.key   # prints the public key -- safe to share/log
   winsparkle-tool.exe sign --private-key-file private.key <file>  # matches the CI signing step
   ```
   `generate-key` only writes `private.key`; run `public-key` against that file to get the public
   key to hand off.
3. Set the public key as a **repository variable**, not a secret:
   `gh variable set WINSPARKLE_PUBLIC_KEY --body "<key>"`, or Settings ▸ Secrets and variables ▸
   Actions ▸ Variables. Pass it locally to test the real flow:
   `-DSYNTH_WINSPARKLE_PUBLIC_KEY=<key>`.
4. Add `private.key`'s contents as the **repository secret** `WINSPARKLE_PRIVATE_KEY` directly from
   the Windows machine, or from wherever your `gh` session already has repo admin access:
   `gh secret set WINSPARKLE_PRIVATE_KEY < private.key`. Then delete the local file. Never commit
   it, and never paste it into a chat, CI log or artifact.

Once both exist, the release workflow's `publish-appcast-windows` job (gated on
`vars.WINSPARKLE_PUBLIC_KEY != ''`) starts running for real instead of skipping, and
`promote-release.yml` starts requiring an `appcast-windows.xml` release asset before it will promote
a tag to stable — see [`releases.md`](releases.md#promoting-a-build-to-stable).

## Verifying an update by hand

There is no automated coverage for this feature. `UpdateManager`'s only logic is two one-line
methods delegating straight to `SPUStandardUpdaterController` — a native macOS GUI framework with
its own, separately maintained, widely used test suite upstream — so there is nothing
headless-testable to lock down; the real risk surface is build, packaging and CI wiring, which
GoogleTest cannot exercise either.

### macOS

**1. Compiles and embeds correctly:**
```bash
cmake -S . -B build && cmake --build build
otool -L "build/AgentSynth_artefacts/AgentSynth.app/Contents/MacOS/Agent Synth" | grep Sparkle
# expect: @rpath/Sparkle.framework/Versions/B/Sparkle
ls "build/AgentSynth_artefacts/AgentSynth.app/Contents/Frameworks/Sparkle.framework"
```

**2. Inert without a key:** launch the built app, open Help ▸ Check for Updates… — it should be
greyed out, with no misconfiguration alert.

**3. Full local update flow**, once a key exists:
- Configure with the real public key: `cmake -S . -B build -DSYNTH_SPARKLE_PUBLIC_KEY=<your key>
  -DSYNTH_BUILD_NUMBER=1` and rebuild.
- Build a second copy with a *higher* build number (`-DSYNTH_BUILD_NUMBER=2`), zip its `.app`, and
  run Sparkle's `generate_appcast` against a directory containing just that zip:
  ```bash
  ./build/_deps/sparkle-src/bin/generate_appcast /path/to/dir-with-the-zip
  ```
- Serve that directory locally: `python3 -m http.server 8000` from inside it.
- Point the *first* (lower build number) app at it for this one test run:
  `-DSYNTH_UPDATE_FEED_URL=http://127.0.0.1:8000/appcast.xml`. Sparkle's App Transport Security
  normally requires HTTPS; `http://127.0.0.1` is exempted by default as loopback, so no ATS
  exception plist entry should be needed. If it is blocked, add a Debug-only
  `NSAppTransportSecurity` / `NSExceptionDomains` entry for `127.0.0.1`, never for the real feed URL.
  Rebuild the first app with this override.
- Launch it, Help ▸ Check for Updates… — Sparkle should offer the higher-numbered build, download,
  verify the EdDSA signature, and install it.

**4. Signature rejection**, the actually security-relevant case: re-run `generate_appcast` against
the same zip but with a different, throwaway key (`generate_keys --account test-throwaway` first),
or hand-edit the `<sparkle:edSignature>` in the generated `appcast.xml`. Confirm Sparkle refuses the
update instead of installing it.

**5. End to end against the real deployment** — `https://agentsynth.app/updates/appcast.xml` is
live; with a real key and a signed release, repeat step 3 against production instead of a local
server.

### Four traps when testing a real macOS update

Each is a real trap for a tester, not a one-off:

1. **A `.zip` downloaded via `gh release download` or `curl` carries no `com.apple.quarantine`
   attribute** — only browsers, and a few other apps that opt into `LSQuarantine`, set it. Testing
   Gatekeeper behaviour against a CLI-fetched build is testing nothing; the flag has to be added
   back (`xattr -w com.apple.quarantine "0081;...;Google Chrome;<uuid>"`) to reproduce what a real
   tester's browser download looks like to the OS.
2. **A quarantined, ad-hoc-signed app gets a hard Gatekeeper block with no "Open Anyway" button** in
   the first dialog — only "Move to Trash" / "Done". The bypass is: Done → System Settings ▸ Privacy
   & Security → "Open Anyway" next to the blocked-app message → confirm the follow-up dialog, which
   *does* have a real Open button. The right-click ▸ Open workaround the download page already tells
   users about is a separate, also-valid path to the same place.
3. **A quarantined app launched without first being moved to `/Applications` runs under App
   Translocation** — a randomized, read-only `/private/var/folders/.../AppTranslocation/...` copy,
   silently, with no dialog. Sparkle detects this itself and refuses to self-update from there:
   "Agent Synth can't be updated if it's running from the location it was downloaded to. Quit Agent
   Synth, move it into your Applications folder, relaunch it from there, and try again." Worth
   calling out explicitly in tester-facing instructions rather than leaving implied.
4. **There is no About box**, so `CFBundleVersion` read out of the installed `Info.plist` with
   `PlistBuddy` is the only reliable way to confirm which build is running after an update.

### Windows

Not yet exercised against real Windows hardware or a real WinSparkle key pair — treat this as the
acceptance test for whoever first runs it, not as confirmed-working.

**1. Compiles and the DLL sits beside the exe:**
```
cmake -S . -B build -G Ninja && cmake --build build
dumpbin /DEPENDENTS "build\AgentSynth_artefacts\Release\Agent Synth.exe" | findstr WinSparkle
:: expect: WinSparkle.dll
dir "build\AgentSynth_artefacts\Release\WinSparkle.dll"
```

**2. Inert without a key:** launch the built app, open Help ▸ Check for Updates… — it should be
greyed out, with no misconfiguration alert.

**3. Installer builds and installs per-user:**
```
makensis /DVERSION=0.13.2 /DSTAGE_DIR="build\AgentSynth_artefacts\Release" installer\windows\AgentSynth.nsi
installer\windows\AgentSynthSetup.exe
```
Confirm it installs to `%LOCALAPPDATA%\AgentSynth` with **no UAC prompt**, creates Start Menu
shortcuts, and appears in Add/Remove Programs. Confirm `AgentSynthSetup.exe /S` runs silently.

**4. Full local update flow**, once a WinSparkle key exists:
- Configure with the real public key: `cmake -S . -B build -DSYNTH_WINSPARKLE_PUBLIC_KEY=<your key>
  -DSYNTH_BUILD_NUMBER=1`, then rebuild and package a first installer.
- Build and package a second installer with a higher `-DSYNTH_BUILD_NUMBER=2`.
- Sign the second installer: `winsparkle-tool.exe sign -f private.key AgentSynthSetup.exe`.
- Hand-write a local `appcast-windows.xml` — the same shape `publish-appcast-windows` generates in
  CI — pointing at the second installer, and serve it: `python -m http.server 8000` from the
  directory containing both.
- Point the *first* (lower build number) app at it for this one test run:
  `-DSYNTH_UPDATE_FEED_URL=http://127.0.0.1:8000/appcast-windows.xml`. Rebuild the first app with
  this override.
- Launch it, Help ▸ Check for Updates… — WinSparkle should offer the higher-numbered build,
  download, verify the EdDSA signature, ask the app to quit (confirm it actually quits — this is the
  untested `handleShutdownRequest` callback), and run the installer.

**5. Signature rejection**: re-sign with a different, throwaway key, or hand-edit the
`sparkle:edSignature` attribute in the local `appcast-windows.xml`. Confirm WinSparkle refuses the
update instead of installing it.

**6. End to end against the real deployment** — once
`https://agentsynth.app/updates/appcast-windows.xml` is live and a real key and signed release
exist, repeat step 4 against production instead of a local server.
