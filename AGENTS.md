# AGENTS.md

Guidance for AGENTS Code (AGENTS.ai/code) in this repository. Keep this file **lean** — it is the orientation layer (commands, conventions, critical traps). Detailed reference lives in `docs/` (map below); when you change behavior, update the relevant doc, not this file.

## Project

Modular synthesizer (JUCE, C++20) with a node-based graph editor for sound design — connect audio/CV modules in a visual patching environment. Two CMake targets:

- **GravisynthCore** — the library: all audio-processing modules + core logic. Headless-testable (no audio device, no GUI).
- **Gravisynth** — the JUCE GUI application built on top of the core (GraphEditor, ModuleComponent, chrome).

## Commands

```bash
# Build
cmake -S . -B build && cmake --build build

# Test  (ENABLE_TESTS defaults OFF — must opt in)
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build --target GravisynthTests
./build/Tests/GravisynthTests
./build/Tests/GravisynthTests --gtest_filter="E2EWorkflow*"   # E2E only

# Coverage  (threshold 85%)
bash scripts/coverage.sh

# Git hooks  (run once per clone — NOT auto-installed)
bash scripts/install-hooks.sh   # pre-commit: clang-format lint;  pre-push: lint + Release build + tests
# clang-format is pinned (.clang-format-version) — match CI locally:
pip install "clang-format==$(cat .clang-format-version)"
```

See [`docs/testing.md`](docs/testing.md) for the full build/test/CI/hooks reference.

## Planning Rules

Every implementation plan **must** include:

1. A **Tests** section — list new test cases, the test file, and what each verifies.
2. A **Docs Updates** section — list which docs (`docs/testing.md`, `AGENTS.md`, etc.) need updating.

## Critical invariants (break these and you ship bugs)

- **Bypass/mute contract** — in every signal-processing `processBlock`, use **two separate branches**: `isBypassed()` → dry pass-through (return early WITHOUT touching audio channels; clear only CV channels ≥2 so mod CV doesn't leak as audio); `isMuted()` → `buffer.clear()` then return. Never `if (isBypassed() || isMuted()) buffer.clear()` — that mutes on bypass instead of passing the signal through. **Exception:** pure source modules with no audio input (Oscillator, Poly MIDI) clear on bypass — no dry signal to pass. → [`docs/architecture.md`](docs/architecture.md)
- **No high-frequency logging** — AIChatComponent registers a global `juce::Logger` (Debug builds only) that pipes `writeToLog` into a UI-thread console. Never add per-sample / per-frame / per-parameter / per-connection logs (one caused a multi-second freeze; guarded by `AIStateMapperTest.PresetLoadDoesNotSpamLogger`). → [`docs/AI_Engine.md`](docs/AI_Engine.md)
- **No unconditional per-tick repaint** — `ModuleComponent` is `setBufferedToImage(true)` with a gated 15 Hz timer; the 30 Hz GraphEditor animation composites cached images. A theme switch is exactly one re-skin pass. All UI animations use `AnimationDriver` (VBlank-driven, **time-bounded** — stops at `t=1`); never add free-running or continuous repaints. Exception: the AI thinking spinner pulses only while a request is in flight, confined to its region. → [`docs/layout.md §10–11`](docs/layout.md)
- **Themes don't swap fonts** — all built-ins share Inter + JetBrains Mono; swapping the embedded typeface *family* at runtime corrupts text (JUCE 8 + CoreText). Themes differ by colour/treatment/glow only. → [`docs/theming.md`](docs/theming.md)

## Docs map

- [`docs/architecture.md`](docs/architecture.md) — layers, core classes (ModuleBase, AudioEngine, GraphEditor, UndoManager, LookAndFeel), bypass/mute contract, signal flow
- [`docs/modules.md`](docs/modules.md) — per-module specs + poly channel layouts (Oscillator, Filter, VCA, ADSR, LFO, Sequencer, Poly MIDI, Voice Mixer …)
- [`docs/fx_modules.md`](docs/fx_modules.md) — FX specs (Distortion, Delay, Reverb, Chorus, Phaser, Compressor, Flanger, Limiter)
- [`docs/modulation.md`](docs/modulation.md) — routing model, logical-port API, poly-bus wires, attenuverters, visual signal flow
- [`docs/layout.md`](docs/layout.md) — grid/snap/auto-arrange, toolbar & status-bar chrome, width buckets, visualizer components, UI perf, animation system (UIAnimation.h, AnimationDriver, micro-interactions), alignment guides
- [`docs/theming.md`](docs/theming.md) — theme tokens, SVG icons, JSON user themes, LookAndFeel, font limitation, Metrics fields (including code-only rendering constants)
- [`docs/testing.md`](docs/testing.md) — test layers, build/test commands, CI pipeline, git hooks, coverage
- [`docs/Module_Development_Guide.md`](docs/Module_Development_Guide.md) — step-by-step guide to adding a module
- [`docs/AI_Engine.md`](docs/AI_Engine.md) · [`docs/AI_Usage_Guide.md`](docs/AI_Usage_Guide.md) — AI patching subsystem (OllamaProvider, AIStateMapper, chat UI)
- [`docs/midi_input.md`](docs/midi_input.md) · [`docs/shortcuts.md`](docs/shortcuts.md) — external MIDI routing, keyboard shortcuts
