# Distribution

Direct-download distribution, no app stores. This doc covers how a build gets its version identity
and what its signing state is. [`auto-update.md`](auto-update.md) covers Sparkle and WinSparkle;
[`releases.md`](releases.md) covers the CI pipeline that builds, publishes and promotes a release.

## Version identity

Two numbers are baked into every macOS build, and they mean different things:

- **`CFBundleShortVersionString`** (`SYNTH_MARKETING_VERSION` cache var in `CMakeLists.txt`, e.g.
  `0.13.2`) — the human-facing marketing version, also used as the plugin's own `VERSION` and the
  Windows installer's Add/Remove Programs entry. It defaults to `PROJECT_VERSION`, the version
  declared by `project(AgentSynth VERSION ...)`, which is what a plain local build gets. On CI a
  `version` job runs *before* the build matrix and dry-runs the same tag computation the release job
  is about to publish, strips any dry-run prerelease suffix, and passes the result to every build
  leg via `-DSYNTH_MARKETING_VERSION`. The release job then tags with `custom_tag` set to that same
  computed value.

  **Why compute it up front.** The version embedded in a release build is then guaranteed to equal
  the tag it ships under, and nobody has to hand-bump `project()` to keep them in sync. Before this,
  the marketing version went stale while the build number kept advancing, so an update dialog could
  read `0.13.2 (257) is now available — you have 0.13.2 (124)` and look broken when it was not.

- **`CFBundleVersion`** (`SYNTH_BUILD_NUMBER` cache var) — the value Sparkle actually compares to
  decide whether an update is available. This is **not** the same value as
  `SYNTH_MARKETING_VERSION`, on purpose: CI passes `-DSYNTH_BUILD_NUMBER=${{ github.run_number }}`
  at configure time, a value that is known before the build starts and increases every workflow run,
  independent of the `version` job's tag computation or of semver formatting. Local and dev builds
  default to `0` and are never distributed.

## What's New (build-time, no network)

A "What's New..." Help-menu item (`AppCommands::whatsNew`) and a link on the welcome screen's
footer show recent changes with **no HTTP client and no offline-handling complexity** — everything
is sourced from this repo's own local git history at CMake **configure** time, so it works even in
an airgapped build.

- **Generation.** The root `CMakeLists.txt`, right after the `SYNTH_BUILD_NUMBER` block, runs two
  `execute_process` calls against `CMAKE_SOURCE_DIR`: `git describe --tags --abbrev=0` for a release
  tag (falling back to the literal `"development build"` if it fails or the repo has no tags — a
  shallow clone must not break the configure) and `git log --pretty=format:%s -n 15` for the last 15
  commit **subject lines** (falling back to `"No release history available."` for a source tarball
  with no `.git` at all). Both captures are sanitized — escaped for a C string literal, then reduced
  to the printable-ASCII range plus newline, where CMake's regex engine has no `\xNN` hex-escape
  syntax so the bracket expression spells the range literally as `[^ -~\n]` — before being written
  into `${CMAKE_BINARY_DIR}/generated/WhatsNewData.h` as `synth::whatsnew::kReleaseTag` /
  `kHighlights[]` / `kHighlightsCount`. `AppUI` and `Tests` (which compiles the
  `Source/MainComponent/` units directly rather than linking `AppUI`) both get
  `${CMAKE_BINARY_DIR}/generated` on their private include path.
- **Configure-time, not build-time.** This is inline `CMakeLists.txt` code, not a custom build-time
  command, so it captures git state once per `cmake -S . -B build`; a plain incremental `cmake
  --build` shows whatever the last configure captured. **Why:** the alternative, a real build-time
  command, would re-run `git` on every invocation for a feature whose whole point is being cheap.
- **Sensitive-info boundary: commit subject lines only.** Never the commit body, never author name
  or email. This project's own commit history is public, so subject lines alone are safe to ship in
  every build.
- **Rendering.** `MainComponent::showWhatsNewDialog()` is a synchronous `juce::AlertWindow` listing
  `kReleaseTag` and each `kHighlights[]` entry as a bullet line — no async state machine, nothing
  for a test to accidentally trigger. Tests assert the generated data's mechanism only, never
  specific commit text, since that is machine-dependent. The welcome screen's version label and its
  `whatsNewButton` both read `kReleaseTag` and that dialog as their one source of truth for "what
  version is this".

See [`../architecture/project-bundle.md`](../architecture/project-bundle.md) for the generated
header's place in the app's wiring.

## Signing state

- **macOS builds are ad-hoc signed** (`codesign --force --deep -s -` in the release workflow), not
  Developer ID signed and not notarized. Sparkle's own update-signature check (the EdDSA key in
  [`auto-update.md`](auto-update.md#generating-the-eddsa-signing-key)) does not require notarization
  to function, but Gatekeeper warns on the *initial* install — and, because an ad-hoc signature's
  designated requirement changes on every build, macOS re-prompts for microphone access after an
  update.
- **`AgentSynthSetup.exe` is unsigned** — no Windows code-signing certificate exists — so SmartScreen
  warns on install. WinSparkle's EdDSA signature check is a separate, orthogonal mechanism covering
  update integrity, and does not touch this.

The local equivalent, and why it exists, is [`local-ci.md`](local-ci.md#dev-signing).
