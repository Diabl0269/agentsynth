# GEMINI.md

This file provides guidance to Gemini CLI when working with code in this repository.

## Project Overview

This is a **modular synthesizer** application built with JUCE and C++20, featuring a node-based graph editor for sound design. The application allows users to create complex sounds by connecting audio modules in a visual patching environment.

## Architecture

The project follows a modular architecture with:

1. **Core Library (`GravisynthCore`)**: Contains all audio processing modules and core logic
2. **Application Layer (`Gravisynth`)**: GUI application built on JUCE that uses the core library
3. **UI Components**: Graph editor and module components for visual patching

### Key Components

- **ModuleBase**: Abstract base class for all audio modules with `ModuleType` enum, `ModulationTarget` metadata, `VisualBuffer` support, logical-port API (`mapInputChannel`/`mapOutputChannel`, `PortRole`, `LogicalPort`), and `isAutoPromotableModTarget` guard that prevents poly-mode CV inputs from being auto-wrapped in attenuverters
- **AudioEngine**: Manages audio device I/O, the audio processor graph, and modulation matrix routing; exposes `getModulationRoutings()` — a read-only derived view (`RoutingKind`: AttenuverterChain / DirectCV / PolyBus) that is never serialized; `getActiveModRoutings()` and `getModulationDisplayInfo()` are thin adapters over it
- **GraphEditor**: Visual editor with zoom/pan and drag-to-connect; draws poly-bus wires (collapsed N-voice `DirectCV` connections) with an "xN" badge and anchors all wires to visible jacks via the logical-port API
- **ModuleComponent**: Auto-UI generation from module parameters with type-safe layout switching via `ModuleType` enum; modulation rings read `getModulationRoutings()`
- **AttenuverterModule**: Intermediary module for modulation routing with bypass and CV amount control; exposes `lastOutputPeak`/`lastModValue` atomics for UI visualization; constructor default Amount = 0.0 (set to 1.0 by `addModRouting`, left at 0.0 by `addEmptyModRouting`)
- **Port Labels**: Virtual `getInputPortLabel()`/`getOutputPortLabel()` on ModuleBase, overridden per-module for descriptive port names in the UI
- **GravisynthUndoManager**: Snapshot-based undo/redo system wrapping `juce::UndoManager`, captures full graph state on every change
- **AI Integration** (`Source/AI/`): AIIntegrationService orchestrates LLM-powered features via OllamaProvider; AIStateMapper translates graph state for AI context

### Audio Modules

- Oscillator: Waveform generator (Sine, Saw, Square, Triangle); poly mode consumes shared mod-CV on ch8=Waveform, 9=Octave, 10=Coarse, 11=Fine, 12=Level; declared with 14 outputs to prevent JUCE graph buffer aliasing
- Filter: Multi-mode filter with 7 types (LPF24, LPF12, HPF24, HPF12, BPF24, BPF12, Notch), cutoff/resonance control, and frequency response visualizer
- VCA: Voltage Controlled Amplifier; poly mode sums 8 voices with per-voice envelope CV
- ADSR: Envelope generator for amplitude/filter modulation; poly mode drives 8 per-voice ADSRs from gate CV
- LFO: Low Frequency Oscillator for modulation
- Sequencer: Step sequencer with per-step pitch control
- MIDI Keyboard: Interactive on-screen keyboard for MIDI input
- FX Modules: Delay, Distortion, Reverb, Chorus, Phaser, Compressor, Flanger, Limiter
- Preset System: Factory presets with categorized organization
- Poly MIDI: Polyphonic MIDI input handling (ch0-7=pitch Hz, ch8-15=gate)
- Poly Sequencer: Polyphonic step sequencer
- Voice Mixer: 8-to-stereo mixer with tanh soft saturation and Level control; alternative to VCA's internal poly summing

## Build System

The project uses CMake with:
- JUCE framework (fetched automatically via FetchContent)
- GoogleTest for unit testing
- Code coverage support (enabled via `ENABLE_COVERAGE` option)
- `JUCE_WEB_BROWSER=0` — WebBrowserComponent is unused; disabling removes WebKit/libsoup deps on Linux

## Planning Rules

Every implementation plan **must** include:
1. A **Tests** section — list new test cases, test file, and what each test verifies
2. A **Docs Updates** section — list which docs files need updating (testing.md, CLAUDE.md, etc.)

## Development Commands

### Build
```bash
cmake -S . -B build
cmake --build build
```

### Run Tests
```bash
# ENABLE_TESTS defaults OFF — must configure with it explicitly
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build --target GravisynthTests
./build/Tests/GravisynthTests
# Run only E2E workflow tests:
./build/Tests/GravisynthTests --gtest_filter="E2EWorkflow*"
```

### Build and Test (Release)
A pre-push git hook automatically runs clang-format lint check + Release build + tests before every push. Install it with:
```bash
bash scripts/install-hooks.sh
```
The first push configures the `build-release` directory; subsequent pushes do fast incremental rebuilds. This catches UB/segfaults that only manifest with optimizations enabled (Debug mode hides use-after-free by zero-initializing memory).

To run manually:
```bash
bash scripts/pre-push-release-test.sh
```

### Check Coverage
```bash
bash scripts/coverage.sh
```

### Git Hooks
```bash
bash scripts/install-hooks.sh    # Install pre-push hook (runs lint + Release build+test before push)
```

## CI Pipeline

CI runs via `.github/workflows/ci.yml` on PRs only (4 parallel jobs):
- **Lint** (Ubuntu, ~30s) — clang-format check
- **Build, Test, and Coverage** (Ubuntu Debug) — coverage threshold 85%
- **Build and Test** (macOS) — Release build, catches UB/segfaults + cross-platform
- **Build and Test** (Windows) — Release build, catches UB/segfaults + cross-platform

Post-merge, `.github/workflows/build-artifacts.yml` runs on push to main (4 jobs): build+package on Ubuntu/macOS/Windows (no tests — CI already ran them), then tag+release.

**Optimizations:**
- **ccache**: Compiler cache avoids recompiling unchanged files. `CMAKE_C/CXX_COMPILER_LAUNCHER=ccache`, cached at `~/.ccache` (Linux) / `~/Library/Caches/ccache` (macOS), 500M max, keyed by commit SHA with prefix restore.
- **FetchContent caching**: `build/_deps` (JUCE 8.0.3 + GoogleTest 1.14.0) cached via `actions/cache`, keyed on `CMakeLists.txt` + `Tests/CMakeLists.txt` hashes.
- **JUCE_WEB_BROWSER=0**: Disabled unused WebBrowserComponent, removed WebKit/libsoup deps from Linux builds.
- **coverage.sh --report-only**: In CI, skips redundant configure/build/test and only does profdata merge + report.
- **Separate lint job**: Instant formatting feedback (~30s) without waiting for full build.
- **apt package caching**: `awalsh128/cache-apt-pkgs-action` caches Ubuntu packages across runs.
- **Path filtering**: PRs only trigger CI when `Source/`, `Tests/`, `CMakeLists.txt`, `scripts/`, or the workflow file change. Push to main always runs.

**What didn't work:** Unity builds (`CMAKE_UNITY_BUILD`) are incompatible with JUCE — Obj-C++ `.mm` files can't be merged into C++ unity translation units.

**What didn't work:** Precompiled headers (PCH) — JUCE compiles its own module `.cpp` files as part of the target and they have guards against being pre-included. On macOS, `.mm` files also need Obj-C++ mode which conflicts with C++ PCH.

## Testing Strategy

~371 tests across ~53 suites, all headless (no audio device, no GUI window). Five test layers: audio rendering (DSP verification), integration (signal chains, mod routing), component workflow (UI interactions), state management (presets, undo/redo, serialization), and E2E workflow (full application paths). Code coverage threshold: 85%. See [`docs/testing.md`](docs/testing.md) for the full breakdown, patterns, and how to add tests for new modules.

## Keyboard Shortcuts

Refer to [docs/shortcuts.md](docs/shortcuts.md) for the full list of configurable keyboard shortcuts.


## Key Files to Understand

- `CMakeLists.txt`: Main build configuration (version 0.13.2)
- `Source/AudioEngine.h/cpp`: Audio processing engine, device management; `getModulationRoutings()` returns read-only `ModulationRouting` structs (`RoutingKind`: AttenuverterChain / DirectCV / PolyBus) derived from the live graph; `getActiveModRoutings()` / `getModulationDisplayInfo()` are adapters over it
- `Source/GravisynthUndoManager.h/cpp`: Snapshot-based undo/redo with `SnapshotAction`, safe detach/reattach lifecycle
- `Source/Modules/ModuleBase.h`: Base class with `ModuleType` enum, `ModulationTarget`, `ModulationCategory`; logical-port API (`PortRole`, `LogicalPort`, `mapInputChannel`, `mapOutputChannel`); `isAutoPromotableModTarget` guard for poly-mode connections
- `Source/Modules/OscillatorModule.h`: Oscillator with PolyBLEP/PolyBLAMP anti-aliasing, waveform crossfade; poly mode reads shared mod-CV on ch8-12 and saves them before clearing the buffer; declared with 14 outputs to prevent JUCE graph buffer aliasing
- `Source/Modules/FilterModule.h`: Multi-mode filter (LadderFilter for LPF/HPF/BPF + SVF for notch), atomic modulated params for visualizer, type parameter
- `Source/Modules/VisualBuffer.h`: Thread-safe circular buffer using `std::atomic<float>`
- `Source/PresetManager.h/cpp`: Factory presets with categorized organization; Poly Pad (case 6) includes Amp Env -> Osc Level (ch12) as DirectCV plus Amp Env -> VCA per-voice CV (PolyBus)
- `Source/UI/ModuleComponent.cpp`: Auto-UI with type-safe `ModuleType` switching, parameter listener for undo, safe detach lifecycle, FrequencyResponseComponent integration and spectrum toggle; modulation rings read `getModulationRoutings()`
- `Source/UI/FrequencyResponseComponent.h`: Serum-style frequency response curve with FFT spectrum overlay
- `Source/UI/GraphEditor.cpp`: Graph editor with attenuverter knob rendering, poly-bus wire drawing (collapsed N-voice connections with "xN" badge), modulation routing, and undo integration; uses logical-port API to anchor wires to visible jacks
- `Source/UI/SettingsWindow.h/cpp`: Consolidated tabbed settings window (Audio, AI, General tabs) with tab persistence
- `Source/ShortcutManager.h`: Configurable keyboard shortcut manager with action→KeyPress mapping, persistence, case-insensitive key matching, and conflict detection
- `Source/UI/ModuleLibraryComponent.h`: Categorized sidebar with section headers for module drag-and-drop
- `Source/UI/ModMatrixComponent.cpp`: Modulation matrix with undo tracking for routing and parameter changes
- **Visual Signal Flow**: GraphEditor draws animated dots on connections (white for audio, cyan for modulation), pulsing modulation lines, and activity glow on modules. ModuleComponent renders Serum-style modulation rings on knobs. Driven by `AudioEngine::getModulationDisplayInfo()` cached at 30fps.
- `Source/Modules/FX/ChorusModule.h`: Chorus effect using `juce::dsp::Chorus`, CV modulation on Rate/Depth
- `Source/Modules/FX/PhaserModule.h`: Phaser effect using `juce::dsp::Phaser`, CV modulation on Rate/Depth
- `Source/Modules/FX/CompressorModule.h`: Compressor with manual makeup gain
- `Source/Modules/FX/FlangerModule.h`: Flanger via `juce::dsp::Chorus` with low-delay tuning
- `Source/Modules/FX/LimiterModule.h`: Brickwall limiter with input gain drive
- `Source/UI/AIChatComponent.cpp/.h`: Chat interface for AI-assisted patching. **Logging gotcha:** this component registers itself as the global `juce::Logger` (`setCurrentLogger`) and pipes every `juce::Logger::writeToLog(...)` into a `TextEditor` debug console (Debug builds only). Appends are coalesced + the console is length-bounded, but DO NOT add high-frequency `writeToLog` calls (per-parameter, per-sample, per-frame, per-connection) — they run on the UI thread. Keep logging to errors/rare events. (A per-parameter log on preset load once caused a multi-second UI freeze; guarded by `AIStateMapperTest.PresetLoadDoesNotSpamLogger`.)
- **UI rendering perf:** `ModuleComponent` uses `setBufferedToImage(true)` and gates its 15Hz `timerCallback` repaint (RMS-change / active-modulation / sequencer-step-change only) so the GraphEditor's 30Hz connection animation composites cached module images instead of re-running JUCE text layout every frame. Don't reintroduce unconditional per-tick `repaint()` on modules or their always-visible children.
- `Source/UI/ScopeComponent.h`: Oscilloscope/waveform display component
- `Source/Modules/FX/DistortionModule.h`: Distortion effect with configurable oversampling (Off/2x/4x), soft-clipping using `tanh`-based curve, Drive and Mix parameters
- `Tests/E2EWorkflowTests.cpp`: E2E workflow tests — preset loading, module drop/delete/replace, connection drag, mod matrix, undo/redo sequences, and stress tests
- `Tests/`: ~371 tests across ~53 suites (audio rendering, integration, component workflow, state management, E2E workflow)
- `docs/modulation.md`: Full reference for the modulation routing model, logical-port API, and poly-bus wire rendering
