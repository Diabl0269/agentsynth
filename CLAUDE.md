# CLAUDE.md

Guidance for Claude Code (claude.ai/code) in this repository. Keep this file **lean** — it is always loaded, so it holds only commands, conventions, and a tripwire index of the critical traps. The rules themselves live in per-directory `CLAUDE.md` files (`Source/`, `Source/Modules/`, `Source/Timeline/`, `Source/AI/`, `Source/UI/`, `Source/Plugin/`, `.github/`), auto-loaded when you work under that directory; the mechanism and history live in `docs/` (map below). When you change behavior, update the relevant doc, not this file.

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

Three rules stated in full because they're cheap to follow and catastrophic to miss:

- **Bypass/mute contract** — in every signal-processing `processBlock`, use **two separate branches**: `isBypassed()` → dry pass-through (return early WITHOUT touching audio channels; clear only CV channels ≥2 so mod CV doesn't leak as audio); `isMuted()` → `buffer.clear()` then return. Never `if (isBypassed() || isMuted()) buffer.clear()` — that mutes on bypass. **Exception:** modules with no dry audio path (pure sources like Oscillator / Poly MIDI; audio-in/CV-out taps like Envelope Follower / Comparator) clear on bypass, still as two branches. → [`docs/architecture.md`](docs/architecture.md)
- **Never relax `validatePatch` to raise the AI pass rate** — it is the security boundary for untrusted model output. Fix validity on the *generation* side, most upstream first: schema → bounded retry → narrow repair → prompt; measure with `Tools/AIPatchHarness` first. A node's `"state"` object (`ModuleBase::setExtraState`) is applied on the **trusted path only** — honouring it for provider output makes a patch suggestion an arbitrary file read. → [`docs/AI_Engine.md`](docs/AI_Engine.md)
- **`trusted=true` on `applyJSONToGraph` is about parameter fidelity, not skipping checks** — the untrusted path rescales in-`[0,1]` values against wider ranges (a heuristic for models), which corrupts app-authored values like a 0.5 Hz LFO rate. Replaying our own `graphToJSON` output applies trusted; if it came off disk, run `validatePatch(..., trusted=false)` as a separate gate first (`SnippetManager::insertSnippet` / `ProjectBundle::load` are the reference pairing). → [`docs/layout.md §12.5`](docs/layout.md)

Everything else below is a tripwire index. The full rule lives in the named area `CLAUDE.md` (auto-loaded when you work under that directory); the mechanism and history live in the linked doc — **read it before touching the area**. A new invariant gets one line here, its rule in the area file, and its detail in the doc.

**Engine & threading** (`Source/CLAUDE.md`):

- `HostMode::Hosted` never opens an audio device or MIDI input. → [`docs/architecture.md`](docs/architecture.md)
- The device callback's render buffer is shared in-place with the graph; never allocate in the callback; audio input stays opt-in (restore requests 0 inputs). → [`docs/architecture.md`](docs/architecture.md)
- A device/sample-rate change goes through the ONE hook (`AudioEngine::handleStreamFormatChange`), and a recording take never spans it. → [`docs/architecture.md`](docs/architecture.md)
- Timeline data crosses threads only via `EpochExchange`: opened once per render pass, published snapshot-first/bindings-second, republished after any graph change. → [`docs/architecture.md`](docs/architecture.md)
- Every graph change must reach `MainComponent::timelineChanged` / the reconcile pass (hook inventory: [`docs/architecture.md` §8](docs/architecture.md)). → [`docs/layout.md §16`](docs/layout.md)
- Every node-uuid write mirrors into the processor via `ModuleBase::setNodeUuid`; written once, never rewritten. → [`docs/architecture.md`](docs/architecture.md)

**Modules & channels** (`Source/Modules/CLAUDE.md`):

- A second audio leg goes on a new `kRightBase` block, never ch1; pan is a balance law (unity centre); Dual I/O "off" drops cables on the hidden right block. → [`docs/modules.md`](docs/modules.md)
- A module's channel count is fixed for its lifetime; variable-port modules declare their maximum and vary only the visible count; an over-wide hosted plugin is refused, never truncated. → [`docs/modules.md`](docs/modules.md)
- Every Wavetable warp mode must prove it doesn't alias (a documented defence + a parameterised-test entry). → [`docs/modules.md`](docs/modules.md)

**Timeline** (`Source/Timeline/CLAUDE.md`):

- Audio clips STREAM; only the prefetch thread may touch a reader; nothing on the audio path opens a file. → [`docs/architecture.md`](docs/architecture.md)
- Hosted-plugin automation lanes resolve only through `synth::resolveLaneParameter`, never by index alone. → [`docs/modulation.md`](docs/modulation.md)

**AI & trust boundaries** (`Source/AI/CLAUDE.md`):

- `applyJSONToGraph` merge mode auto-connects new nodes; exact-sub-graph callers pass `autoConnectNewNodes=false`. → [`docs/layout.md §12.5`](docs/layout.md)
- Patch-format reserved fields stay reserved (`"timeline"` refused untrusted; flat scalar params; `uuid` trusted-only). → [`docs/AI_Engine.md`](docs/AI_Engine.md)
- Conversation-history persistence is resolved server-side from the entitlement, never trusted from a client header. → [`docs/AI_Engine.md`](docs/AI_Engine.md)
- Persist a rotated refresh token before using the access token that came with it (`AccountService::completeSignIn` is the funnel). → [`docs/AI_Engine.md`](docs/AI_Engine.md)
- Installing an AI provider after construction requires calling `refreshModels()` again, or every `/api/chat` gets a 400. → [`docs/AI_Engine.md`](docs/AI_Engine.md)

**UI & theming** (`Source/UI/CLAUDE.md`, `Source/Plugin/CLAUDE.md`):

- No unconditional per-tick repaint; all animations use `AnimationDriver`; exactly two blessed exceptions. → [`docs/layout.md §10–11`](docs/layout.md)
- A cable is not a graph edge — enumerate via `GraphEditor::buildVisibleCables()`, colour via `synth::ui::resolveCableColour`. → [`docs/layout.md §14`](docs/layout.md)
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
- [`docs/layout.md`](docs/layout.md) — grid/snap/auto-arrange, toolbar & status-bar chrome, width buckets, visualizer components, UI perf, animation system (UIAnimation.h, AnimationDriver, micro-interactions), multi-select + group drag + snippets (§12), collapsible library sections (§13), timeline panel + track headers + keyboard/focus arbitration (§16)
- [`docs/theming.md`](docs/theming.md) — theme tokens, SVG icons, JSON user themes, LookAndFeel, font limitation
- [`docs/testing.md`](docs/testing.md) — test layers, build/test commands, CI pipeline, git hooks, coverage
- [`docs/Module_Development_Guide.md`](docs/Module_Development_Guide.md) — step-by-step guide to adding a module
- [`docs/AI_Engine.md`](docs/AI_Engine.md) · [`docs/AI_Usage_Guide.md`](docs/AI_Usage_Guide.md) — AI patching subsystem (OllamaProvider, AIStateMapper, chat UI)
- [`docs/midi_input.md`](docs/midi_input.md) · [`docs/shortcuts.md`](docs/shortcuts.md) — external MIDI routing, keyboard shortcuts
- [`docs/distribution.md`](docs/distribution.md) — version identity, Sparkle auto-update (macOS), EdDSA key generation, CI appcast publishing, WinSparkle status
- Feature planning artifacts (timeline concept & task tracker) live in the **private** `synth-platform` repo under `docs/plans/` — kept out of this public repo on purpose.
