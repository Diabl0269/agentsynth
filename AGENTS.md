# CLAUDE.md

Guidance for Claude Code (claude.ai/code) in this repository. Keep this file **lean** — it is always loaded, so it holds only commands, conventions, and a tripwire index of the critical traps. The rules themselves live in per-directory `CLAUDE.md` files (`Source/`, `Source/Modules/`, `Source/Timeline/`, `Source/AI/`, `Source/UI/`, `Source/Plugin/`, `.github/`), auto-loaded when you work under that directory; the mechanism and history live in `docs/` (map below). When you change behavior, update the relevant doc, not this file — docs must never go stale, so treat updating them as part of the change itself, not a follow-up.

## Project

Modular synthesizer (JUCE, C++20) with a node-based graph editor for sound design — connect audio/CV modules in a visual patching environment. Four CMake targets:

- **Core** — the library: all audio-processing modules + core logic. Headless-testable (no audio device, no GUI).
- **AppUI** — the editor UI (MainComponent, GraphEditor, ModuleComponent, chrome), shared by the app and the plugin.
- **AgentSynth** — the JUCE GUI application (`Main.cpp` on top of AppUI).
- **AgentSynthPlugin** — VST3 (+ AU on macOS) plugin wrapping the same AppUI/Core in a `juce::AudioProcessor`.

## Commands

```bash
# Build (ENABLE_PLUGIN defaults ON, so this also builds the VST3/AU plugin — pass -DENABLE_PLUGIN=OFF for an app-only loop)
cmake -S . -B build && cmake --build build

# Test  (ENABLE_TESTS defaults OFF — must opt in)
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build --target Tests
./build/Tests/Tests
./build/Tests/Tests --gtest_filter="E2EWorkflow*"   # E2E only

# Coverage  (threshold 85%)
bash scripts/coverage.sh

# CI script tests (all run in the Lint job; no compiler, no network, ~1s each)
bash scripts/tests/ci-cache-check.test.sh          # cache health check (also runs after every CI build)
bash scripts/tests/ci-install-linux-deps.test.sh   # Linux apt install + mirror failover
bash scripts/tests/check-nonascii-literals.test.sh # no raw/escaped non-ASCII in string literals (fromUTF8/CharPointer_UTF8 exempt)
bash scripts/tests/utf8-literal-check.test.sh      # non-ASCII \x escape wrapping (also runs directly in the Lint job)

# Git hooks  (run once per clone — NOT auto-installed)
bash scripts/install-hooks.sh   # pre-commit: clang-format lint;  pre-push: lint + Release build + tests
# clang-format is pinned (.clang-format-version) — match CI locally:
pip install "clang-format==$(cat .clang-format-version)"
```

See [`docs/development/testing.md`](docs/development/testing.md) for the full build/test reference.

## Planning Rules

Every implementation plan **must** include:

1. A **Tests** section — list new test cases, the test file, and what each verifies.
2. A **Docs Updates** section — list which docs (`docs/development/testing.md`, `CLAUDE.md`, etc.) need updating.

## Critical invariants (break these and you ship bugs)

Three rules stated in full because they're cheap to follow and catastrophic to miss:

- **Bypass/mute contract** — in every signal-processing `processBlock`, use **two separate branches**: `isBypassed()` → dry pass-through (return early WITHOUT touching audio channels; clear only CV channels ≥2 so mod CV doesn't leak as audio); `isMuted()` → `buffer.clear()` then return. Never `if (isBypassed() || isMuted()) buffer.clear()` — that mutes on bypass. **Exception:** modules with no dry audio path (pure sources like Oscillator / Poly MIDI; audio-in/CV-out taps like Envelope Follower / Comparator) clear on bypass, still as two branches. → [`docs/architecture/module-base.md#bypassmute-contract`](docs/architecture/module-base.md#bypassmute-contract)
- **Never relax `validatePatch` to raise the AI pass rate** — it is the security boundary for untrusted model output. Fix validity on the *generation* side, most upstream first: schema → bounded retry → narrow repair → prompt; measure with `Tools/AIPatchHarness` first. A node's `"state"` object (`ModuleBase::setExtraState`) is applied on the **trusted path only** — honouring it for provider output makes a patch suggestion an arbitrary file read. → [`docs/ai/patch-safety.md`](docs/ai/patch-safety.md)
- **`trusted=true` on `applyJSONToGraph` is about parameter fidelity, not skipping checks** — the untrusted path rescales in-`[0,1]` values against wider ranges (a heuristic for models), which corrupts app-authored values like a 0.5 Hz LFO rate. Replaying our own `graphToJSON` output applies trusted; if it came off disk, run `validatePatch(..., trusted=false)` as a separate gate first (`SnippetManager::insertSnippet` / `ProjectBundle::load` are the reference pairing). → [`docs/layout/snippets-clipboard.md`](docs/layout/snippets-clipboard.md)

Everything else below is a tripwire index. The full rule lives in the named area `CLAUDE.md` (auto-loaded when you work under that directory); the mechanism and history live in the linked doc — **read it before touching the area**. A new invariant gets one line here, its rule in the area file, and its detail in the doc.

**Engine & threading** (`Source/CLAUDE.md`):

- `HostMode::Hosted` never opens an audio device or MIDI input. → [`docs/architecture/plugin-layer.md#host-modes-audioenginehostmode`](docs/architecture/plugin-layer.md#host-modes-audioenginehostmode)
- The device callback's render buffer is shared in-place with the graph; never allocate in the callback; audio input stays opt-in (restore requests 0 inputs). → [`docs/architecture/audio-engine.md#audioengine`](docs/architecture/audio-engine.md#audioengine)
- A device/sample-rate change goes through the ONE hook (`AudioEngine::handleStreamFormatChange`), and a recording take never spans it. → [`docs/architecture/app-wiring.md#device--sample-rate-changes`](docs/architecture/app-wiring.md#device--sample-rate-changes)
- Timeline data crosses threads only via `EpochExchange`: opened once per render pass, published snapshot-first/bindings-second, republished after any graph change. → [`docs/architecture/timeline.md#timelinesnapshot-the-audio-threads-view-of-the-timeline`](docs/architecture/timeline.md#timelinesnapshot-the-audio-threads-view-of-the-timeline)
- `MainComponent` owns the app's live `TimelineDoc`; every graph change must reach `MainComponent::timelineChanged` / the reconcile pass (hook inventory: [`docs/architecture/app-wiring.md`](docs/architecture/app-wiring.md#app-wiring--who-owns-the-timeline-and-every-hook-that-keeps-it-in-step)); a binding is never re-established automatically. → [`docs/timeline/tracks.md`](docs/timeline/tracks.md#a-binding-is-never-re-established-automatically)
- Every node-uuid write mirrors into the processor via `ModuleBase::setNodeUuid`; written once, never rewritten. → [`docs/architecture/module-base.md#node-uuid-mirror-setnodeuuid--getnodeuuid`](docs/architecture/module-base.md#node-uuid-mirror-setnodeuuid--getnodeuuid)
- Every document-replacing action goes through `MainComponent::guardUnsavedChanges` (async — hand it the work, never do it then ask), and any path that replaces the document with something that is not a bundle drops `currentBundleDir_`. → [`docs/architecture/project-bundle.md#dirty-state-and-the-unsaved-changes-guard`](docs/architecture/project-bundle.md#dirty-state-and-the-unsaved-changes-guard)
- Autosave writes a sidecar (`autosave.json`), never `project.json`, and rotates a configurable number of numbered backups; gates on edit-serial movement (not `isDirty_`) and never fires during a recording take or a bounce. → [`docs/architecture/project-bundle.md#autosave-and-crash-recovery`](docs/architecture/project-bundle.md#autosave-and-crash-recovery)
- No non-ASCII bytes in a `Source/` string literal — `juce::String`'s `const char*` ctor decodes as Latin-1, so `"Rename…"` (or its hex-escape spelling) ships mojibake; use ASCII or `juce::CharPointer_UTF8`/`String::fromUTF8`. Guarded by `scripts/tests/check-nonascii-literals.test.sh`. → [`docs/development/ascii-literal-guard.md`](docs/development/ascii-literal-guard.md)

**Modules & channels** (`Source/Modules/CLAUDE.md`):

- A second audio leg goes on a new `kRightBase` block, never ch1; pan is a balance law (unity centre); Dual I/O is **inherited** from channel shape (`hasStereoOutputPairShape`), never per-module registered, and "off" drops cables on the hidden right block. → [`docs/modules/fx-modules.md#stereo-io-dual-io-toggle`](docs/modules/fx-modules.md#stereo-io-dual-io-toggle)
- A module's channel count is fixed for its lifetime; variable-port modules declare their maximum and vary only the visible count; an over-wide hosted plugin is refused, never truncated. → [`docs/modules/modules.md#hosted-plugin-module-third-party-vst3--au-hidden`](docs/modules/modules.md#hosted-plugin-module-third-party-vst3--au-hidden)
- Every Wavetable warp mode must prove it doesn't alias (a documented defence + a parameterised-test entry). → [`docs/modules/wavetable.md#warp`](docs/modules/wavetable.md#warp)

**Timeline** (`Source/Timeline/CLAUDE.md`):

- Audio clips STREAM; only the prefetch thread may touch a reader; nothing on the audio path opens a file. → [`docs/architecture/app-wiring.md#audioclipstreamer-disk-streaming-clip-playback`](docs/architecture/app-wiring.md#audioclipstreamer-disk-streaming-clip-playback) · [`docs/modules/modules.md#track-audio-module-timeline-audio-source-hidden`](docs/modules/modules.md#track-audio-module-timeline-audio-source-hidden)
- Hosted-plugin automation lanes resolve only through `synth::resolveLaneParameter`, never by index alone. → [`docs/modules/modulation.md#hosted-plugin-parameters-as-automation-lanes`](docs/modules/modulation.md#hosted-plugin-parameters-as-automation-lanes) · [`docs/modules/modules.md#load-ux`](docs/modules/modules.md#load-ux)

**AI & trust boundaries** (`Source/AI/CLAUDE.md`):

- `applyJSONToGraph` merge mode auto-connects new nodes; exact-sub-graph callers pass `autoConnectNewNodes=false`. → [`docs/layout/snippets-clipboard.md`](docs/layout/snippets-clipboard.md)
- Patch-format reserved fields stay reserved (`"timeline"` and `"macros"` (P8-12) both refused untrusted; flat scalar params; `uuid` trusted-only). → [`docs/ai/patch-format.md`](docs/ai/patch-format.md) · [`docs/layout/macro-cards.md`](docs/layout/macro-cards.md)
- Conversation-history persistence is resolved server-side from the entitlement, never trusted from a client header. → [`docs/ai/history.md`](docs/ai/history.md#server-side-conversation-history)
- Persist a rotated refresh token before using the access token that came with it (`AccountService::completeSignIn` is the funnel). → [`docs/ai/accounts.md`](docs/ai/accounts.md#rotation-before-use)
- Installing an AI provider after construction requires calling `refreshModels()` again, or every `/api/chat` gets a 400. → [`docs/ai/chat-component.md`](docs/ai/chat-component.md#model-discovery-ordering-contract)

**UI & theming** (`Source/UI/CLAUDE.md`, `Source/Plugin/CLAUDE.md`):

- No unconditional per-tick repaint; all animations use `AnimationDriver`; exactly two blessed exceptions. → [`docs/layout/rendering.md`](docs/layout/rendering.md) · [`docs/layout/animation.md`](docs/layout/animation.md)
- A cable is not a graph edge — enumerate via `GraphEditor::buildVisibleCables()`, colour via `synth::ui::resolveCableColour`. → [`docs/layout/cables.md`](docs/layout/cables.md)
- Themes never swap font families (JUCE 8 + CoreText corrupts text); colour/treatment/glow only. → [`docs/layout/theming.md`](docs/layout/theming.md)
- A plugin editor never calls `Desktop::setDefaultLookAndFeel` — it's process-global inside the host. → [`docs/architecture/plugin-layer.md#who-owns-what`](docs/architecture/plugin-layer.md#who-owns-what)
- No per-sample / per-frame / per-parameter logging — a global Logger pipes into a UI-thread console. → [`docs/ai/chat-component.md`](docs/ai/chat-component.md#logging-rules)

**CI** (`.github/CLAUDE.md`):

- The CI cache is load-bearing and fails silently: per-language compiler launchers, keep the `push: main` trigger, key `build/_deps` on `cmake/DependencyVersions.cmake` only (pin new dependencies there), explicit `CCACHE_DIR` per job. → [`docs/development/ci-caching.md`](docs/development/ci-caching.md)

## Docs map

The map of every doc under `docs/` (one line each, grouped by area) lives in [`docs/README.md`](docs/README.md). Read a doc before touching its area; a new doc gets a line there, not here.
