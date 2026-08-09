# CLAUDE.md

Guidance for Claude Code (claude.ai/code) in this repository. Keep this file **lean** — it is the orientation layer (commands, conventions, critical traps). Detailed reference lives in `docs/` (map below); when you change behavior, update the relevant doc, not this file.

## Project

Modular synthesizer (JUCE, C++20) with a node-based graph editor for sound design — connect audio/CV modules in a visual patching environment. Two CMake targets:

- **Core** — the library: all audio-processing modules + core logic. Headless-testable (no audio device, no GUI).
- **AgentSynth** — the JUCE GUI application built on top of the core (GraphEditor, ModuleComponent, chrome).

## Commands

```bash
# Build
cmake -S . -B build && cmake --build build

# Test  (ENABLE_TESTS defaults OFF — must opt in)
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build --target Tests
./build/Tests/Tests
./build/Tests/Tests --gtest_filter="E2EWorkflow*"   # E2E only

# Coverage  (threshold 85%)
bash scripts/coverage.sh

# CI cache health check (also runs in CI after every build; tests run in the Lint job)
bash scripts/tests/ci-cache-check.test.sh

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

- **Bypass/mute contract** — in every signal-processing `processBlock`, use **two separate branches**: `isBypassed()` → dry pass-through (return early WITHOUT touching audio channels; clear only CV channels ≥2 so mod CV doesn't leak as audio); `isMuted()` → `buffer.clear()` then return. Never `if (isBypassed() || isMuted()) buffer.clear()` — that mutes on bypass instead of passing the signal through. **Exception:** modules with no dry audio path clear on bypass (still as two branches) — pure sources with no audio *input* (Oscillator, Poly MIDI) and audio-in/CV-out taps with no audio *output* (Envelope Follower). → [`docs/architecture.md`](docs/architecture.md)
- **No high-frequency logging** — AIChatComponent registers a global `juce::Logger` (Debug builds only) that pipes `writeToLog` into a UI-thread console. Never add per-sample / per-frame / per-parameter / per-connection logs (one caused a multi-second freeze; guarded by `AIStateMapperTest.PresetLoadDoesNotSpamLogger`). → [`docs/AI_Engine.md`](docs/AI_Engine.md)
- **No unconditional per-tick repaint** — `ModuleComponent` is `setBufferedToImage(true)` with a gated 15 Hz timer; the 30 Hz GraphEditor animation composites cached images. A theme switch is exactly one re-skin pass. All UI animations use `AnimationDriver` (VBlank-driven, **time-bounded** — stops at `t=1`); never add free-running or continuous repaints. Exception: the AI thinking spinner pulses only while a request is in flight, confined to its region. → [`docs/layout.md §10–11`](docs/layout.md)
- **Themes don't swap fonts** — all built-ins share Inter + JetBrains Mono; swapping the embedded typeface *family* at runtime corrupts text (JUCE 8 + CoreText). Themes differ by colour/treatment/glow only. → [`docs/theming.md`](docs/theming.md)
- **Never relax `validatePatch` to raise the AI pass rate** — it is the security boundary for untrusted model output. Fix patch validity on the *generation* side, most upstream first: schema (`getPatchSchema` — the backend enforces it as a grammar) → bounded retry with the specific error → narrow repair → prompt. Measure with `Tools/AIPatchHarness` before changing anything; validation reports only the first error, so fixing one class unmasks the next. Related: a node's `"state"` object (`ModuleBase::setExtraState`, e.g. the Sampler's file path) is applied on the **trusted path only** — never honour it for provider output, or a patch suggestion becomes an arbitrary file read. → [`docs/AI_Engine.md`](docs/AI_Engine.md)
- **`applyJSONToGraph` merge mode adds wires you didn't ask for** — when `clearExisting=false`, it auto-connects new audio nodes to Audio Output and new MIDI-accepting nodes to an existing MIDI source. That's an affordance for AI-authored patches; any caller reproducing an *exact* sub-graph (snippet insert, copy/paste, duplicate) must pass `autoConnectNewNodes=false` or the inserted group gets spliced into the surrounding patch. → [`docs/layout.md §12.5`](docs/layout.md)
- **`trusted=true` on `applyJSONToGraph` is about parameter fidelity, not about skipping checks** — the untrusted apply path rescales any value inside `[0,1]` whose parameter range is wider (a heuristic for models that ignore ranges), which silently corrupts app-authored values like a 0.5 Hz LFO rate. Anything replaying our own `graphToJSON` output (presets, undo/redo, snippets) must apply trusted. When that data came off disk, run `validatePatch(..., trusted=false)` as a separate gate *first* so a hand-edited file is still rejected whole — `SnippetManager::insertSnippet` is the reference for this pairing. → [`docs/layout.md §12.5`](docs/layout.md)
- **Every Wavetable warp mode must prove it doesn't alias** — warps run *after* mip selection, so they can put back exactly the harmonics the pyramid exists to remove. A new mode needs one of the two documented defences (a `warpRateFactor` entry so `selectMip` sees its steepest slope, or a `warpNeedsOversampling` flag) **and** an entry in the parameterised `WavetableWarpAliasTest` / `WavetableWarpBoundsTest` suites — they iterate `Warp::Count`, so a mode added without a strategy fails the build's test run rather than shipping quietly. → [`docs/modules.md`](docs/modules.md)
- **AI model discovery ordering** — `AIChatComponent`'s ctor-time `refreshModels()` is a no-op if its `AIIntegrationService` has no provider yet; any owner (e.g. `MainComponent`) that installs a provider afterward MUST call `refreshModels()` again post-`setProvider()`, or `currentModel` stays empty and every `/api/chat` gets a 400. → [`docs/AI_Engine.md`](docs/AI_Engine.md)
- **CI cache is load-bearing and fails silently** — three rules: `ci.yml` keeps its `push: main` trigger (GitHub scopes caches per ref; without a main-scoped cache *no PR can restore one* and every build runs cold); `build/_deps` is keyed on `cmake/DependencyVersions.cmake` only, **never** on `CMakeLists.txt` (a module adding a source line must not invalidate a 350 MB entry — that overran GitHub's 10 GB repo cache limit and evicted everything); `CCACHE_DIR` is set explicitly per job (defaults differ by OS *and* ccache version — a mismatch means the cache is never saved). Add a dependency? Pin it in `cmake/DependencyVersions.cmake`. Guarded by `scripts/ci-cache-check.sh`. → [`docs/testing.md`](docs/testing.md)
- **A cable is not a graph edge** — one drawn wire can be 1 edge (audio/MIDI), 2 edges + a hidden node (attenuverter chain), or N parallel edges (poly bus). Never identify, hit-test, colour, or delete a cable by `AudioProcessorGraph::Connection`; go through `GraphEditor::buildVisibleCables()`, which is the **single** enumeration feeding both `paint()` and hit-testing (compute them separately and the drawn curve drifts from the clickable one). Wire colour resolves *only* via `synth::ui::resolveCableColour` — never read a `*Wire` theme token at a paint site, or user colour overrides silently stop applying. → [`docs/layout.md §14`](docs/layout.md) · [`docs/theming.md §11`](docs/theming.md)
- **A module's channel count is fixed for its lifetime** — JUCE settles the bus layout in the `ModuleBase` constructor, and renegotiating it would drop every graph connection the node already has. A module whose port count varies (Macro bank) declares its **maximum** channels up front and varies only `getVisibleOutputPortCount()`, clearing the hidden channels each block. When the visible count shrinks, the owner must drop routings left on the hidden jacks — an invisible jack cannot be unplugged. → [`docs/modules.md`](docs/modules.md)
- **Persist a rotated refresh token before using the access token that came with it** — `AccountService`'s device-flow and refresh calls always return a *new* refresh token, and the auth service revokes the whole token family if a consumed one is ever presented again. Save the new refresh token to the `TokenStore` and confirm it succeeded **before** exposing the new access token or notifying listeners; a crash in that window otherwise leaves a dead token in the keychain and silently signs the user out next launch, with nothing to explain why. → [`docs/AI_Engine.md`](docs/AI_Engine.md)

## Docs map

- [`docs/architecture.md`](docs/architecture.md) — layers, core classes (ModuleBase, AudioEngine, GraphEditor, UndoManager, LookAndFeel), bypass/mute contract, signal flow
- [`docs/modules.md`](docs/modules.md) — per-module specs + poly channel layouts (Oscillator, Filter, VCA, ADSR, LFO, Sequencer, Poly MIDI, Voice Mixer, Math …)
- [`docs/fx_modules.md`](docs/fx_modules.md) — FX specs (Distortion, Delay, Reverb, Chorus, Phaser, Compressor, Flanger, Limiter, Pitch Shifter, Parametric EQ)
- [`docs/modulation.md`](docs/modulation.md) — routing model, logical-port API, poly-bus wires, attenuverters, visual signal flow
- [`docs/layout.md`](docs/layout.md) — grid/snap/auto-arrange, toolbar & status-bar chrome, width buckets, visualizer components, UI perf, animation system (UIAnimation.h, AnimationDriver, micro-interactions), multi-select + group drag + snippets (§12), collapsible library sections (§13)
- [`docs/theming.md`](docs/theming.md) — theme tokens, SVG icons, JSON user themes, LookAndFeel, font limitation
- [`docs/testing.md`](docs/testing.md) — test layers, build/test commands, CI pipeline, git hooks, coverage
- [`docs/Module_Development_Guide.md`](docs/Module_Development_Guide.md) — step-by-step guide to adding a module
- [`docs/AI_Engine.md`](docs/AI_Engine.md) · [`docs/AI_Usage_Guide.md`](docs/AI_Usage_Guide.md) — AI patching subsystem (OllamaProvider, AIStateMapper, chat UI)
- [`docs/midi_input.md`](docs/midi_input.md) · [`docs/shortcuts.md`](docs/shortcuts.md) — external MIDI routing, keyboard shortcuts
