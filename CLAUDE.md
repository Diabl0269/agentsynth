# CLAUDE.md

Guidance for Claude Code (claude.ai/code) in this repository. Keep this file **lean** — it is the orientation layer (commands, conventions, critical traps). Detailed reference lives in `docs/` (map below); when you change behavior, update the relevant doc, not this file.

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

# CI script tests (both run in the Lint job; no compiler, no network, ~1s each)
bash scripts/tests/ci-cache-check.test.sh          # cache health check (also runs after every CI build)
bash scripts/tests/ci-install-linux-deps.test.sh   # Linux apt install + mirror failover

# Git hooks  (run once per clone — NOT auto-installed)
bash scripts/install-hooks.sh   # pre-commit: clang-format lint;  pre-push: lint + Release build + tests
# clang-format is pinned (.clang-format-version) — match CI locally:
pip install "clang-format==$(cat .clang-format-version)"
```

See [`docs/testing.md`](docs/testing.md) for the full build/test/CI/hooks reference.

## Planning Rules

Every implementation plan **must** include:

1. A **Tests** section — list new test cases, the test file, and what each verifies.
2. A **Docs Updates** section — list which docs (`docs/testing.md`, `CLAUDE.md`, etc.) need updating.

## Critical invariants (break these and you ship bugs)

Each bullet is the trap and the rule; the linked doc carries the full mechanism and history. Keep it that way: a new invariant gets 1–3 sentences here and its detail in the doc.

### Audio engine & DSP

- **Bypass/mute contract** — in every signal-processing `processBlock`, use **two separate branches**: `isBypassed()` → dry pass-through (return early WITHOUT touching audio channels; clear only CV channels ≥2 so mod CV doesn't leak as audio); `isMuted()` → `buffer.clear()` then return. Never `if (isBypassed() || isMuted()) buffer.clear()` — that mutes on bypass. **Exception:** modules with no dry audio path (pure sources like Oscillator / Poly MIDI; audio-in/CV-out taps like Envelope Follower / Comparator) clear on bypass, still as two branches. → [`docs/architecture.md`](docs/architecture.md)
- **`AudioEngine::HostMode::Hosted` never touches hardware** — no audio device, no MIDI input (the host owns both; opening MIDI directly double-triggers notes). Drive it only through `prepareForHost`/`processHostBlock`/`releaseFromHost`. → [`docs/architecture.md`](docs/architecture.md)
- **The device callback's render buffer is not the device's output pointers** — one buffer of `max(numIn, numOut)` channels serves both the Audio Input and Audio Output nodes (the graph renders in place), so device input is copied in before the graph runs and uncovered channels come from a scratch buffer sized in `audioDeviceAboutToStart` — the callback must never allocate. Audio input stays **opt-in**: absent saved state means `initialiseWithDefaultDevices(0, 2)`, and the restore path requests **0** input channels so a saved setup never switches a microphone on uninvited. Core never touches `ApplicationProperties`; state flows out via `onDeviceStateChanged`. → [`docs/architecture.md`](docs/architecture.md)
- **A device/sample-rate change goes through ONE hook, and a take never spans it** — `AudioEngine::handleStreamFormatChange` (called from both `audioDeviceAboutToStart` and `prepareForHost`) is where every prepare-path consumer is added, in order, transport first — never duplicated per call site. It invalidates all clip streams (a rate change is a discontinuity the ring's self-healing miss can't detect). An in-flight take is finalized at the boundary, never continued: `MainComponent::AudioTake` freezes `captureSampleRate`/`captureBpm`/`captureRecordingLatencySamples` at record-on; reading them live at commit mixes two real rates under one declared one. → [`docs/architecture.md`](docs/architecture.md)
- **Audio clips STREAM, and only the prefetch thread may touch a reader** — nothing on the audio path may open, read or map a file, and nothing pulls a clip into RAM whole (that is `SamplerModule`'s model, wrong here). Every `juce::AudioFormatReader` is owned exclusively by the prefetch thread; the message thread only publishes an assignment table. The ring's fill/publish order is load-bearing — read the doc before touching it. An offline bounce steers only `wantedFrame`, via `waitUntilPrimed` with nothing rendering, and counts a timeout as a reported dropout — never by reading a file from the render loop. → [`docs/architecture.md`](docs/architecture.md) · [`docs/modules.md`](docs/modules.md)

### Modules & channels

- **A second audio leg goes on a new `kRightBase` block, never on ch1** — on the voice modules ch1 is already a CV input, so relabelling it "Right" repoints every saved patch that modulates it. Wavetable/Oscillator/Filter/VCA put `Audio R` on a dedicated block above the mod-CV inputs: keep the declared output count above every CV input index, bound end-of-block CV clears at `kRightBase` (not `getNumChannels()`), and use `ModuleBase::panGains` (balance law, unity centre — equal-power would quieten every mono patch by 3 dB). Dual I/O "off" on these modules exposes the **left leg only** and must DROP cables on the hidden right block. Pair legs via `ModuleBase::rightAudioLegChannel()`, never by assuming ch1. And when a module's visible jack count grows, override the default port map — the fallback marks unclaimed channels as phantom poly heads and `getJackTargets` duplicates wires. → [`docs/modules.md`](docs/modules.md) · [`docs/fx_modules.md`](docs/fx_modules.md)
- **A module's channel count is fixed for its lifetime** — JUCE settles the bus layout in the `ModuleBase` constructor; renegotiating drops every existing connection. Variable-port modules (Macro bank, Audio Input, Hosted Plugin) declare their **maximum** and vary only `getVisibleInput/OutputPortCount()`, clearing hidden channels each block; when the visible count shrinks, drop routings on the now-hidden jacks (`GraphEditor::dropRoutingsOnHiddenJacks`). A hosted plugin wider than `kMaxPluginChannels` is **refused with a message, never truncated**. → [`docs/modules.md`](docs/modules.md)
- **Every Wavetable warp mode must prove it doesn't alias** — warps run *after* mip selection, so a new mode needs one of the two documented defences (a `warpRateFactor` entry or the `warpNeedsOversampling` flag) **and** an entry in the parameterised `WavetableWarpAliasTest` / `WavetableWarpBoundsTest` suites, which iterate `Warp::Count` so an unhandled mode fails the test run instead of shipping. → [`docs/modules.md`](docs/modules.md)

### Timeline & threading

- **Timeline data crosses threads only via an `EpochExchange`** — `AudioEngine::renderPass` is the one site that opens the snapshot and binding exchanges, exactly once each per render pass; never cache a returned reference across passes (epoch reclamation makes it a use-after-free). Modules read via `getPlayHead()` downcast to `synth::TransportService*` (same block-only lifetime; can be null). `AudioEngine::publishTimeline` is the only publisher — **snapshot first, bindings second** — and must be re-called after any graph change. Every `TimelineDoc` edit goes through its mutation API, never a direct field write. → [`docs/architecture.md`](docs/architecture.md)
- **`MainComponent` owns the app's live `TimelineDoc`, and every graph change has to tell it** — publish-to-the-audio-thread happens at ONE seam (`MainComponent::timelineChanged`, which also rebinds the `AutomationRecorder`); any path that adds/removes/replaces nodes must run the reconcile pass afterwards, and a new "replace the graph" path means a new entry in the hook inventory table in [`docs/architecture.md` §8](docs/architecture.md). An orphaned track binding is never re-established automatically — the user picks a node from the chip menu. → [`docs/layout.md §16`](docs/layout.md)
- **A node's uuid must be mirrored into its processor at every write site** — `node->properties` is message-thread-only, so every `properties.set("uuid", ...)` pairs with `ModuleBase::setNodeUuid` (`mirrorUuidIntoProcessor` in `AIStateMapper.cpp`; `MainComponent`'s add-track flow). The lock-free `getNodeUuid()` is only sound because the uuid is written once, before the node is audio-visible — never rewrite or clear it. → [`docs/architecture.md`](docs/architecture.md)
- **A hosted plugin's automation lanes never bind by index alone** — every lane-resolution call site shares ONE resolver, `synth::resolveLaneParameter` (`Source/Timeline/AutomationBinding.h`): exact id match wins; a stored `paramIndexHint` rescues only a parameter with no stable id at all; a hinted index that names a *different*, still-identified parameter **orphans** the lane rather than silently rebinding. → [`docs/modulation.md`](docs/modulation.md) · [`docs/modules.md`](docs/modules.md)

### AI & trust boundaries

- **Never relax `validatePatch` to raise the AI pass rate** — it is the security boundary for untrusted model output. Fix validity on the *generation* side, most upstream first: schema → bounded retry → narrow repair → prompt; measure with `Tools/AIPatchHarness` first. A node's `"state"` object (`ModuleBase::setExtraState`) is applied on the **trusted path only** — honouring it for provider output makes a patch suggestion an arbitrary file read. → [`docs/AI_Engine.md`](docs/AI_Engine.md)
- **`applyJSONToGraph` merge mode adds wires you didn't ask for** — with `clearExisting=false` it auto-connects new nodes to Audio Output / a MIDI source (an affordance for AI patches); any caller reproducing an *exact* sub-graph (snippet insert, copy/paste, duplicate) must pass `autoConnectNewNodes=false`. → [`docs/layout.md §12.5`](docs/layout.md)
- **`trusted=true` on `applyJSONToGraph` is about parameter fidelity, not skipping checks** — the untrusted path rescales in-`[0,1]` values against wider ranges (a heuristic for models), which corrupts app-authored values like a 0.5 Hz LFO rate. Replaying our own `graphToJSON` output applies trusted; if it came off disk, run `validatePatch(..., trusted=false)` as a separate gate first (`SnippetManager::insertSnippet` / `ProjectBundle::load` are the reference pairing). → [`docs/layout.md §12.5`](docs/layout.md)
- **Patch format forward-compatibility: reserved fields stay reserved** — `"timeline"` stays refused on the untrusted path; params stay a flat scalar map (time-varying data belongs under `"timeline"`); `schemaVersion` is never bumped for additive changes; per-node `uuid` is trusted-only; unknown top-level keys survive save/load inert. AI-authored timeline data (TL8) enters through a **separate door**, `synth::validateTimeline` — extend that, never weaken the `"timeline"` refusal. → [`docs/AI_Engine.md`](docs/AI_Engine.md)
- **Conversation-history persistence is resolved server-side from the entitlement, never trusted from a client header** — the client only decides which UI to *show* (`isProPlan(AccountSnapshot)`); the server checks the signed-in account's plan on every request. Same class of boundary as `trusted=true` above. → [`docs/AI_Engine.md`](docs/AI_Engine.md)
- **Persist a rotated refresh token before using the access token that came with it** — every `AccountService` sign-in/refresh returns a *new* refresh token, and the auth service revokes the whole family if a consumed one reappears. Save it to the `TokenStore` and confirm success **before** exposing the access token or notifying listeners (`AccountService::completeSignIn` is the one funnel). → [`docs/AI_Engine.md`](docs/AI_Engine.md)
- **AI model discovery ordering** — `AIChatComponent`'s ctor-time `refreshModels()` is a no-op with no provider installed; any owner that installs one afterward MUST call `refreshModels()` again post-`setProvider()`, or every `/api/chat` gets a 400. → [`docs/AI_Engine.md`](docs/AI_Engine.md)

### UI & theming

- **No unconditional per-tick repaint** — `ModuleComponent` is buffered-to-image with a gated 15 Hz timer; the 30 Hz GraphEditor animation composites cached images; a theme switch is exactly one re-skin pass. All animations use `AnimationDriver` (VBlank-driven, time-bounded). Exactly two exceptions: the AI thinking spinner (in-flight only, region-confined) and the timeline playhead (strip-confined, playing-only). → [`docs/layout.md §10–11`](docs/layout.md)
- **A cable is not a graph edge** — one drawn wire can be 1 edge, 2 edges + a hidden attenuverter node, or N poly-bus edges. Never identify, hit-test, colour or delete a cable by `AudioProcessorGraph::Connection`; go through `GraphEditor::buildVisibleCables()`, the single enumeration feeding both `paint()` and hit-testing. Wire colour resolves only via `synth::ui::resolveCableColour` — reading a `*Wire` theme token at a paint site breaks user colour overrides. → [`docs/layout.md §14`](docs/layout.md) · [`docs/theming.md §11`](docs/theming.md)
- **Themes don't swap fonts** — all built-ins share Inter + JetBrains Mono; swapping the embedded typeface *family* at runtime corrupts text (JUCE 8 + CoreText). Themes differ by colour/treatment/glow only. → [`docs/theming.md`](docs/theming.md)
- **A plugin editor must never call `Desktop::setDefaultLookAndFeel`** — it's process-global inside the host. Scope with `setLookAndFeel(&processor.getLookAndFeel())`; `ThemeManager`/`AppLookAndFeel` belong to the processor, since hosts recreate the editor repeatedly. → [`docs/architecture.md`](docs/architecture.md)
- **No high-frequency logging** — a global `juce::Logger` (Debug builds) pipes `writeToLog` into a UI-thread console; never add per-sample / per-frame / per-parameter / per-connection logs (one caused a multi-second freeze; guarded by `AIStateMapperTest.PresetLoadDoesNotSpamLogger`). → [`docs/AI_Engine.md`](docs/AI_Engine.md)

### CI

- **CI cache is load-bearing and fails silently** — four rules: `CMAKE_<LANG>_COMPILER_LAUNCHER` is **per language** (a language without its own launcher silently never reaches ccache; `scripts/ci-cache-check.sh` audits the generated ninja files for this); `ci.yml` keeps its `push: main` trigger (caches are ref-scoped — without a main-scoped cache no PR can restore one); `build/_deps` is keyed on `cmake/DependencyVersions.cmake` only, never `CMakeLists.txt` (pin new dependencies there); `CCACHE_DIR` is set explicitly per job. The Timeline-OFF job restores `build/_deps` but **never saves it** and owns its own `-timeline-off-` ccache namespace — keep both properties. → [`docs/testing.md`](docs/testing.md)

## Docs map

- [`docs/architecture.md`](docs/architecture.md) — layers, core classes (ModuleBase, AudioEngine, TransportService, TimelineDoc, GraphEditor, UndoManager, LookAndFeel), bypass/mute contract, signal flow, plugin layer (VST3/AU host modes, ownership, state format)
- [`docs/modules.md`](docs/modules.md) — per-module specs + poly channel layouts (Oscillator, Filter, VCA, ADSR, LFO, Sequencer, Poly MIDI, Voice Mixer, Math …)
- [`docs/fx_modules.md`](docs/fx_modules.md) — FX specs (Distortion, Delay, Reverb, Chorus, Phaser, Compressor, Flanger, Limiter, Pitch Shifter, Parametric EQ, Ring Modulator)
- [`docs/modulation.md`](docs/modulation.md) — routing model, logical-port API, poly-bus wires, attenuverters, visual signal flow
- [`docs/layout.md`](docs/layout.md) — grid/snap/auto-arrange, toolbar & status-bar chrome, width buckets, visualizer components, UI perf, animation system (UIAnimation.h, AnimationDriver, micro-interactions), multi-select + group drag + snippets (§12), collapsible library sections (§13), timeline panel + track headers + keyboard/focus arbitration (§16)
- [`docs/theming.md`](docs/theming.md) — theme tokens, SVG icons, JSON user themes, LookAndFeel, font limitation
- [`docs/testing.md`](docs/testing.md) — test layers, build/test commands, CI pipeline, git hooks, coverage
- [`docs/Module_Development_Guide.md`](docs/Module_Development_Guide.md) — step-by-step guide to adding a module
- [`docs/AI_Engine.md`](docs/AI_Engine.md) · [`docs/AI_Usage_Guide.md`](docs/AI_Usage_Guide.md) — AI patching subsystem (OllamaProvider, AIStateMapper, chat UI)
- [`docs/midi_input.md`](docs/midi_input.md) · [`docs/shortcuts.md`](docs/shortcuts.md) — external MIDI routing, keyboard shortcuts
- [`docs/distribution.md`](docs/distribution.md) — version identity, Sparkle auto-update (macOS), EdDSA key generation, CI appcast publishing, WinSparkle status
- Feature planning artifacts (timeline concept & task tracker) live in the **private** `synth-platform` repo under `docs/plans/` — kept out of this public repo on purpose.
