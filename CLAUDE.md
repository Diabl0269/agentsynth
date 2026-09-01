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

See [`docs/testing.md`](docs/testing.md) for the full build/test/CI/hooks reference.

## Planning Rules

Every implementation plan **must** include:

1. A **Tests** section — list new test cases, the test file, and what each verifies.
2. A **Docs Updates** section — list which docs (`docs/testing.md`, `CLAUDE.md`, etc.) need updating.

## Critical invariants (break these and you ship bugs)

Three rules stated in full because they're cheap to follow and catastrophic to miss:

- **Bypass/mute contract** — in every signal-processing `processBlock`, use **two separate branches**: `isBypassed()` → dry pass-through (return early WITHOUT touching audio channels; clear only CV channels ≥2 so mod CV doesn't leak as audio); `isMuted()` → `buffer.clear()` then return. Never `if (isBypassed() || isMuted()) buffer.clear()` — that mutes on bypass. **Exception:** modules with no dry audio path (pure sources like Oscillator / Poly MIDI; audio-in/CV-out taps like Envelope Follower / Comparator) clear on bypass, still as two branches. → [`docs/architecture.md`](docs/architecture.md)
- **Never relax `validatePatch` to raise the AI pass rate** — it is the security boundary for untrusted model output. Fix validity on the *generation* side, most upstream first: schema → bounded retry → narrow repair → prompt; measure with `Tools/AIPatchHarness` first. A node's `"state"` object (`ModuleBase::setExtraState`) is applied on the **trusted path only** — honouring it for provider output makes a patch suggestion an arbitrary file read. → [`docs/AI_Engine.md`](docs/AI_Engine.md)
- **`trusted=true` on `applyJSONToGraph` is about parameter fidelity, not skipping checks** — the untrusted path rescales in-`[0,1]` values against wider ranges (a heuristic for models), which corrupts app-authored values like a 0.5 Hz LFO rate. Replaying our own `graphToJSON` output applies trusted; if it came off disk, run `validatePatch(..., trusted=false)` as a separate gate first (`SnippetManager::insertSnippet` / `ProjectBundle::load` are the reference pairing). → [`docs/layout_selection_canvas.md §1.5`](docs/layout_selection_canvas.md)

Everything else below is a tripwire index. The full rule lives in the named area `CLAUDE.md` (auto-loaded when you work under that directory); the mechanism and history live in the linked doc — **read it before touching the area**. A new invariant gets one line here, its rule in the area file, and its detail in the doc.

**Engine & threading** (`Source/CLAUDE.md`):

- `HostMode::Hosted` never opens an audio device or MIDI input. → [`docs/architecture.md`](docs/architecture.md)
- The device callback's render buffer is shared in-place with the graph; never allocate in the callback; audio input stays opt-in (restore requests 0 inputs). → [`docs/architecture.md`](docs/architecture.md)
- A device/sample-rate change goes through the ONE hook (`AudioEngine::handleStreamFormatChange`), and a recording take never spans it. → [`docs/architecture.md`](docs/architecture.md)
- Timeline data crosses threads only via `EpochExchange`: opened once per render pass, published snapshot-first/bindings-second, republished after any graph change. → [`docs/architecture.md`](docs/architecture.md)
- `MainComponent` owns the app's live `TimelineDoc`; every graph change must reach `MainComponent::timelineChanged` / the reconcile pass (hook inventory: [`docs/architecture.md` §8](docs/architecture.md)); a binding is never re-established automatically. → [`docs/timeline_panel_core.md §3`](docs/timeline_panel_core.md)
- Every node-uuid write mirrors into the processor via `ModuleBase::setNodeUuid`; written once, never rewritten. → [`docs/architecture.md`](docs/architecture.md)
- Every document-replacing action goes through `MainComponent::guardUnsavedChanges` (async — hand it the work, never do it then ask), and any path that replaces the document with something that is not a bundle drops `currentBundleDir_`. → [`docs/architecture.md`](docs/architecture.md)
- Autosave writes a sidecar (`autosave.json`), never `project.json`, and rotates a configurable number of numbered backups; gates on edit-serial movement (not `isDirty_`) and never fires during a recording take or a bounce. → [`docs/architecture.md`](docs/architecture.md)
- No non-ASCII bytes in a `Source/` string literal — `juce::String`'s `const char*` ctor decodes as Latin-1, so `"Rename…"` (or its hex-escape spelling) ships mojibake; use ASCII or `juce::CharPointer_UTF8`/`String::fromUTF8`. Guarded by `scripts/tests/check-nonascii-literals.test.sh`. → [`docs/testing.md`](docs/testing.md)

**Modules & channels** (`Source/Modules/CLAUDE.md`):

- A second audio leg goes on a new `kRightBase` block, never ch1; pan is a balance law (unity centre); Dual I/O is **inherited** from channel shape (`hasStereoOutputPairShape`), never per-module registered, and "off" drops cables on the hidden right block. → [`docs/modules.md`](docs/modules.md)
- A module's channel count is fixed for its lifetime; variable-port modules declare their maximum and vary only the visible count; an over-wide hosted plugin is refused, never truncated. → [`docs/modules.md`](docs/modules.md)
- Every Wavetable warp mode must prove it doesn't alias (a documented defence + a parameterised-test entry). → [`docs/modules.md`](docs/modules.md)

**Timeline** (`Source/Timeline/CLAUDE.md`):

- Audio clips STREAM; only the prefetch thread may touch a reader; nothing on the audio path opens a file. → [`docs/architecture.md`](docs/architecture.md) · [`docs/modules.md`](docs/modules.md)
- Hosted-plugin automation lanes resolve only through `synth::resolveLaneParameter`, never by index alone. → [`docs/modulation.md`](docs/modulation.md) · [`docs/modules.md`](docs/modules.md)

**AI & trust boundaries** (`Source/AI/CLAUDE.md`):

- `applyJSONToGraph` merge mode auto-connects new nodes; exact-sub-graph callers pass `autoConnectNewNodes=false`. → [`docs/layout_selection_canvas.md §1.5`](docs/layout_selection_canvas.md)
- Patch-format reserved fields stay reserved (`"timeline"` refused untrusted; flat scalar params; `uuid` trusted-only). → [`docs/AI_Engine.md`](docs/AI_Engine.md)
- Conversation-history persistence is resolved server-side from the entitlement, never trusted from a client header. → [`docs/AI_Engine_providers_accounts.md §1`](docs/AI_Engine_providers_accounts.md)
- Persist a rotated refresh token before using the access token that came with it (`AccountService::completeSignIn` is the funnel). → [`docs/AI_Engine_providers_accounts.md §5`](docs/AI_Engine_providers_accounts.md)
- Installing an AI provider after construction requires calling `refreshModels()` again, or every `/api/chat` gets a 400. → [`docs/AI_Engine.md`](docs/AI_Engine.md)

**UI & theming** (`Source/UI/CLAUDE.md`, `Source/Plugin/CLAUDE.md`):

- No unconditional per-tick repaint; all animations use `AnimationDriver`; exactly two blessed exceptions. → [`docs/layout_visuals_animation.md §2–3`](docs/layout_visuals_animation.md)
- A cable is not a graph edge — enumerate via `GraphEditor::buildVisibleCables()`, colour via `synth::ui::resolveCableColour`. → [`docs/layout_selection_canvas.md §3`](docs/layout_selection_canvas.md) · [`docs/theming.md §11`](docs/theming.md)
- Themes never swap font families (JUCE 8 + CoreText corrupts text); colour/treatment/glow only. → [`docs/theming.md`](docs/theming.md)
- A plugin editor never calls `Desktop::setDefaultLookAndFeel` — it's process-global inside the host. → [`docs/architecture.md`](docs/architecture.md)
- No per-sample / per-frame / per-parameter logging — a global Logger pipes into a UI-thread console. → [`docs/AI_Engine.md`](docs/AI_Engine.md)

**CI** (`.github/CLAUDE.md`):

- The CI cache is load-bearing and fails silently: per-language compiler launchers, keep the `push: main` trigger, key `build/_deps` on `cmake/DependencyVersions.cmake` only (pin new dependencies there), explicit `CCACHE_DIR` per job. → [`docs/testing.md`](docs/testing.md)

## Docs map

- [`docs/architecture.md`](docs/architecture.md) — layers, core classes (ModuleBase, AudioEngine, TransportService, TimelineDoc, GraphEditor, UndoManager, LookAndFeel), bypass/mute contract, signal flow, plugin layer (VST3/AU host modes, ownership, state format)
- [`docs/modules.md`](docs/modules.md) — per-module specs + poly channel layouts (Oscillator, Filter, VCA, ADSR, LFO, Sequencer, Poly MIDI, Voice Mixer, Math …)
- [`docs/fx_modules.md`](docs/fx_modules.md) — FX specs (Distortion, Delay, Reverb, Chorus, Phaser, Compressor, Flanger, Limiter, Pitch Shifter, Parametric EQ, Ring Modulator)
- [`docs/modulation.md`](docs/modulation.md) — routing model, logical-port API, poly-bus wires, attenuverters, visual signal flow
- [`docs/layout.md`](docs/layout.md) — grid/snap/auto-arrange, toolbar & status-bar chrome, width buckets, LayoutUtil API, drag affordance + smart connections
- [`docs/layout_visuals_animation.md`](docs/layout_visuals_animation.md) — visualizer components, UI rendering performance, animation system (UIAnimation.h, AnimationDriver, PanelSlide, micro-interactions), alignment guides
- [`docs/layout_selection_canvas.md`](docs/layout_selection_canvas.md) — multi-select + group drag + snippets/clipboard (§1.5), collapsible library sections, cable interaction, minimap overlay
- [`docs/timeline_panel_core.md`](docs/timeline_panel_core.md) — timeline panel overview, ruler/grid/zoom/snap + markers, track headers/binding chips, playhead, transport bar, metronome, edit-tool strip
- [`docs/timeline_panel_clips_automation.md`](docs/timeline_panel_clips_automation.md) — clip lanes, piano roll, automation strip, keyboard & focus arbitration
- [`docs/theming.md`](docs/theming.md) — theme tokens, SVG icons, JSON user themes, LookAndFeel, font limitation
- [`docs/testing.md`](docs/testing.md) — test layers, build/test commands, CI pipeline, git hooks, coverage
- [`docs/Module_Development_Guide.md`](docs/Module_Development_Guide.md) — step-by-step guide to adding a module
- [`docs/AI_Engine.md`](docs/AI_Engine.md) · [`docs/AI_Usage_Guide.md`](docs/AI_Usage_Guide.md) — AI patching subsystem: architecture, communication pattern, patch validity/few-shot/untrusted-timeline/security model, AIChatComponent
- [`docs/AI_Engine_providers_accounts.md`](docs/AI_Engine_providers_accounts.md) — conversation history, provider registry (OllamaProvider/RemoteProvider), account sign-in, device id/trial, quota UI
- [`docs/midi_input.md`](docs/midi_input.md) · [`docs/shortcuts.md`](docs/shortcuts.md) — external MIDI routing, keyboard shortcuts
- [`docs/distribution.md`](docs/distribution.md) — version identity, Sparkle auto-update (macOS), EdDSA key generation, CI appcast publishing, WinSparkle status
- Feature planning artifacts (timeline concept & task tracker) live in a private repo, kept out of this public repo on purpose.
