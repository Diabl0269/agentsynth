# GEMINI.md

This file provides guidance to Gemini CLI when working with code in this repository.

## Project Overview

This is a **modular synthesizer** application built with JUCE and C++20, featuring a node-based graph editor for sound design. The application allows users to create complex sounds by connecting audio modules in a visual patching environment.

## Architecture

The project follows a modular architecture with:

1. **Core Library (`Core`)**: Contains all audio processing modules and core logic
2. **Application Layer (`AgentSynth`)**: GUI application built on JUCE that uses the core library
3. **UI Components**: Graph editor and module components for visual patching

### Key Components

- **ModuleBase**: Abstract base class for all audio modules with `ModuleType` enum, `ModulationTarget` metadata, `VisualBuffer` support, logical-port API (`mapInputChannel`/`mapOutputChannel`, `PortRole`, `LogicalPort`), and `isAutoPromotableModTarget` guard that prevents poly-mode CV inputs from being auto-wrapped in attenuverters
- **AudioEngine**: Manages audio device I/O, the audio processor graph, and modulation matrix routing; exposes `getModulationRoutings()` — a read-only derived view (`RoutingKind`: AttenuverterChain / DirectCV / PolyBus) that is never serialized; `getActiveModRoutings()` and `getModulationDisplayInfo()` are thin adapters over it
- **GraphEditor**: Visual editor with zoom/pan and drag-to-connect; draws poly-bus wires (collapsed N-voice `DirectCV` connections) with an "xN" badge and anchors all wires to visible jacks via the logical-port API; snaps module positions to the 8 px grid on drag-release and resolves overlaps via spiral search on every drop/drag; exposes `autoArrange()` (Cmd+L / "Auto Arrange" toolbar button) which topologically layers modules by signal-flow depth in one undo step
- **LayoutUtil** (`Source/UI/LayoutUtil.h/.cpp`): Stateless layout helpers — `snap`, `intersectsAny`, `findFreeSlot` (expanding-square spiral search), `computeAutoArrange` (longest-path topological layering); no JUCE GUI deps, fully headless-testable
- **ModuleComponent**: Auto-UI generation from module parameters with type-safe layout switching via `ModuleType` enum; modulation rings read `getModulationRoutings()`
- **AttenuverterModule**: Intermediary module for modulation routing with bypass and CV amount control; exposes `lastOutputPeak`/`lastModValue` atomics for UI visualization; constructor default Amount = 0.0 (set to 1.0 by `addModRouting`, left at 0.0 by `addEmptyModRouting`)
- **Port Labels**: Virtual `getInputPortLabel()`/`getOutputPortLabel()` on ModuleBase, overridden per-module for descriptive port names in the UI
- **AppUndoManager**: Snapshot-based undo/redo system wrapping `juce::UndoManager`, captures full graph state on every change
- **AI Integration** (`Source/AI/`): AIIntegrationService orchestrates LLM-powered features via OllamaProvider; AIStateMapper translates graph state for AI context
- **AppLookAndFeel** (`Source/UI/Theme/AppLookAndFeel.h/.cpp`): Central `LookAndFeel_V4` subclass that owns all stock-widget re-skins (ColourId mappings from theme tokens) and exposes public treatment helper draw methods (`drawModulePanel`, `drawConnectionWire`, `drawModulationRing`, `fillThemedBackground`) so module cards, wires, and rings all honor the active theme's treatment style. Holds a copy of the active `Theme` updated via `applyTheme()`. Owned by `AppApplication` and installed as `Desktop::setDefaultLookAndFeel`.
- **ThemeManager** (`Source/UI/Theme/ThemeManager.h/.cpp`): Theme registry + active selection + JSON user-theme loader + persistence via the shared `ApplicationProperties` (key `"themeId"`). Extends `juce::ChangeBroadcaster` so `MainComponent` (a `ChangeListener`) can trigger the single re-skin+repaint pass on every switch. Built-ins registered first; user `*.gtheme.json` files loaded from the user themes folder at startup and on demand.

### Audio Modules

- Oscillator: Waveform generator (Sine, Saw, Square, Triangle); poly mode consumes shared mod-CV on ch8=Waveform, 9=Octave, 10=Coarse, 11=Fine, 12=Level; declared with 14 outputs to prevent JUCE graph buffer aliasing
- Filter: Multi-mode filter with 7 types (LPF24, LPF12, HPF24, HPF12, BPF24, BPF12, Notch), cutoff/resonance control, and frequency response visualizer
- VCA: Voltage Controlled Amplifier; poly mode sums 8 voices with per-voice envelope CV
- ADSR: Envelope generator for amplitude/filter modulation; poly mode drives 8 per-voice ADSRs from gate CV
- LFO: Low Frequency Oscillator for modulation
- Sequencer: Step sequencer with per-step pitch control
- MIDI Keyboard: Interactive on-screen keyboard for MIDI input
- FX Modules: Delay, Distortion, Reverb, Chorus, Phaser, Compressor, Flanger, Limiter, Pitch Shifter
- Preset System: Factory presets with categorized organization
- Poly MIDI: Polyphonic MIDI input handling (ch0-7=pitch Hz, ch8-15=gate); `voiceMaskAtomic_` (`std::atomic<uint8_t>`) written end-of-processBlock — read by `AudioEngine::getDisplayVoiceCount()` via `std::popcount` without locks
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
cmake --build build --target Tests
./build/Tests/Tests
# Run only E2E workflow tests:
./build/Tests/Tests --gtest_filter="E2EWorkflow*"
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

Post-merge, `.github/workflows/build-artifacts.yml` runs on push to main (4 jobs): build+package on Ubuntu/macOS/Windows (no tests — CI already ran them), then tag+release. Docs-only pushes (markdown, `docs/`, LICENSE, .gitignore, `.clang-format(.version)`, `.claude/`, `mockups/`) are skipped via `paths-ignore` — no build, no version bump, no release; mixed code+docs pushes release as normal.

**Optimizations:**
- **ccache**: Compiler cache avoids recompiling unchanged files. `CMAKE_C/CXX_COMPILER_LAUNCHER=ccache`, cached at `~/.ccache` (Linux) / `~/Library/Caches/ccache` (macOS), 500M max, keyed by commit SHA with prefix restore.
- **FetchContent caching**: `build/_deps` (JUCE 8.0.3 + GoogleTest 1.14.0) cached via `actions/cache`, keyed on `CMakeLists.txt` + `Tests/CMakeLists.txt` hashes.
- **JUCE_WEB_BROWSER=0**: Disabled unused WebBrowserComponent, removed WebKit/libsoup deps from Linux builds.
- **coverage.sh --report-only**: In CI, skips redundant configure/build/test and only does profdata merge + report.
- **Separate lint job**: Instant formatting feedback (~30s) without waiting for full build.
- **apt package caching**: `awalsh128/cache-apt-pkgs-action` caches Ubuntu packages across runs.
- **Path filtering**: PRs only trigger CI when `Source/`, `Tests/`, `CMakeLists.txt`, `scripts/`, or the workflow file change. Post-merge builds are skipped for docs-only pushes (see `paths-ignore` in `build-artifacts.yml`).

**What didn't work:** Unity builds (`CMAKE_UNITY_BUILD`) are incompatible with JUCE — Obj-C++ `.mm` files can't be merged into C++ unity translation units.

**What didn't work:** Precompiled headers (PCH) — JUCE compiles its own module `.cpp` files as part of the target and they have guards against being pre-included. On macOS, `.mm` files also need Obj-C++ mode which conflicts with C++ PCH.

## Testing Strategy

~460 tests across ~64 suites, all headless (no audio device, no GUI window). Eight test layers: audio rendering (DSP verification), integration (signal chains, mod routing), component workflow (UI interactions), state management (presets, undo/redo, serialization), theme system (token round-trips, ColourId mapping, WCAG contrast, icon tinting), layout (grid snap, anti-overlap, auto-arrange, width buckets), icon library (SVG loading, tint stability, null fallback), status bar (CPU/voice polling, master-mute, format helpers), and E2E workflow (full application paths). Code coverage threshold: 85%. See [`docs/testing.md`](docs/testing.md) for the full breakdown, patterns, and how to add tests for new modules.

## Keyboard Shortcuts

Refer to [docs/shortcuts.md](docs/shortcuts.md) for the full list of configurable keyboard shortcuts.


## Key Files to Understand

- `CMakeLists.txt`: Main build configuration (version 0.13.2); `Assets` binary-data target embeds 17 OFL font files from `assets/fonts/` plus 22 SVG icon files from `assets/icons/`; linked PUBLIC into `Core` with `HAS_FONT_ASSETS=1` (so app + tests both resolve `BinaryData` typefaces and icon assets); CMake guard enforces hyphen-only icon filenames (underscore in filename → fatal error at configure time)
- `Source/Main.cpp`: Application entry point; `MainWindow` ctor calls `setResizeLimits(480, 400, 8192, 8192)` — hard platform floor on the `DocumentWindow` so the toolbar can never collapse to zero width; called BEFORE `centreWithSize(1600, 900)`
- `Source/AudioEngine.h/cpp`: Audio processing engine, device management; `getModulationRoutings()` returns read-only `ModulationRouting` structs (`RoutingKind`: AttenuverterChain / DirectCV / PolyBus) derived from the live graph; `getActiveModRoutings()` / `getModulationDisplayInfo()` are adapters over it; `getDisplayVoiceCount()` / `setMasterMute()` / `isMasterMuted()` for status bar + transport; `masterMuted_` is `std::atomic<bool>` — zero-fill runs AFTER `processBlock` so clocks/envelopes keep advancing; requires `#include <bit>` for `std::popcount` (C++20)
- `Source/AppUndoManager.h/cpp`: Snapshot-based undo/redo with `SnapshotAction`, safe detach/reattach lifecycle
- `Source/Modules/ModuleBase.h`: Base class with `ModuleType` enum, `ModulationTarget`, `ModulationCategory`; logical-port API (`PortRole`, `LogicalPort`, `mapInputChannel`, `mapOutputChannel`); `isAutoPromotableModTarget` guard for poly-mode connections; `ModuleType` is used by `LayoutUtil::getModuleWidthBucket` to classify modules into NARROW/SINGLE/DOUBLE width buckets
- `Source/Modules/OscillatorModule.h`: Oscillator with PolyBLEP/PolyBLAMP anti-aliasing, waveform crossfade; poly mode reads shared mod-CV on ch8-12 and saves them before clearing the buffer; declared with 14 outputs to prevent JUCE graph buffer aliasing
- `Source/Modules/FilterModule.h`: Multi-mode filter (LadderFilter for LPF/HPF/BPF + SVF for notch), atomic modulated params for visualizer, type parameter
- `Source/Modules/VisualBuffer.h`: Thread-safe circular buffer using `std::atomic<float>`
- `Source/PresetManager.h/cpp`: Factory presets with categorized organization; Poly Pad (case 6) includes Amp Env -> Osc Level (ch12) as DirectCV plus Amp Env -> VCA per-voice CV (PolyBus); all factory preset x/y positions are baked onto the standard column model (column stride 360 px, signal row y=40, mod row y=420) and are collision-clean at `kCollisionGap=12` — validated by `PresetManagerTest.AllFactoryPresetsLoadWithoutOverlap`; Phase 3 rebakes presets 0, 1, 5: Sequencer width changed to `kDoubleWidth=560` (right edge=570), so AmpEnv moved from x=560 to x=**584** and FilterEnv from x=870 to x=**880**; presets 2, 3, 4, 6 were clean and unchanged
- `Source/UI/ModuleComponent.cpp`: Auto-UI with type-safe `ModuleType` switching, parameter listener for undo, safe detach lifecycle, FrequencyResponseComponent integration and spectrum toggle; modulation rings read `getModulationRoutings()`; bypass/mute are `juce::DrawableButton` (not TextButton); `deleteButton` (`DrawableButton`) at header-right position `(w-26, 2, 22, 20)` calls `owner.requestDeleteModule(nodeId)` on click; `applyHeaderButtonIcons()` / `lookAndFeelChanged()` retint all three buttons from `AppLookAndFeel` — null-guarded for headless tests
- `Source/UI/FrequencyResponseComponent.h`: Serum-style frequency response curve with FFT spectrum overlay
- `Source/UI/GraphEditor.cpp`: Graph editor with attenuverter knob rendering, poly-bus wire drawing (collapsed N-voice connections with "xN" badge), modulation routing, and undo integration; uses logical-port API to anchor wires to visible jacks; drag-preview system (`beginDragPreview`/`updateDragPreview`/`endDragPreview`) shows a themed grid-dot overlay + live landing ghost (snapped + anti-overlapped in real time) during any module drag; library drops use `finalizeModuleDrag` on the real component after `updateComponents()` so the final position always anti-overlaps using the true pixel dimensions (not the size estimate); `finalizeModuleDrag()` snap+anti-overlap on drag-release, `resolvePlacement()` anti-overlap on library drop, `autoArrange()` topological auto-layout (Cmd+L / "Auto Arrange" toolbar button, single undo step); `requestDeleteModule(NodeID)` is the canonical delete entry point — `ModuleComponent::deleteButton.onClick` delegates here
- `Source/UI/LayoutUtil.h/.cpp`: Stateless grid-layout helpers (`snap`, `intersectsAny`, `findFreeSlot`, `computeAutoArrange`); no JUCE GUI deps — all geometry in canvas coordinates; Phase 3 adds width-bucket constants (`kNarrowWidth=40`, `kSingleWidth=280`, `kDoubleWidth=560`) and `getModuleWidthBucket(ModuleType)` / `moduleWidth()` helpers (Sequencer/PolySequencer/MidiKeyboard→Double, Attenuverter→Narrow, all others→Single); `kColumnStride = kSingleWidth + kLayerGapX = 360`; see `docs/layout.md` for the full model and API reference
- `Source/UI/ToolbarComponent.h/.cpp`: FlexBox-based responsive toolbar — holds refs to 9 `DrawableButton*` slots (Library, Save, Load, Settings, Undo, Redo, AutoArrange, ToggleModMatrix, ToggleAiPanel); `layoutButtons(bounds)` runs a single `juce::FlexBox` row with a flex spacer between the left group and right group; narrow mode fires when `bounds.getWidth() <= narrowThreshold_` (480 px) — all buttons collapse to 32 px icon-only width; `isNarrowMode()` bool read by `MainComponent::resized()` to gate `applyToolbarIcons()` to narrow-mode transitions only (avoids Drawable clone storm on every resize); `paint()` fills `theme.colors.bg0` via dynamic_cast LnF, fallback `0xff0B0D10`
- `Source/UI/StatusBarComponent.h/.cpp`: Bottom status bar (height 24 px) — paint-plus-button component; displays patch name (left), CPU % (warning colour >80%), voice count (right); owns `masterMuteButton_` (`DrawableButton`, far-right); `update(cpu, voices, patch)` is gated — calls `repaint()` only when any value changes by threshold; `resized()` positions `masterMuteButton_` at `(w-28, 2, 20, h-4)`; static format helpers `formatCpu` / `formatVoices` / `formatPatch` are headless-testable; **ZERO `writeToLog` calls** — status polling runs at 5 Hz from `MainComponent`'s 10 Hz timer (every 2nd tick via `statusBarTickCount_`)
- `Source/UI/SettingsWindow.h/cpp`: Consolidated tabbed settings window (Audio, AI, General tabs) with tab persistence
- `Source/ShortcutManager.h`: Configurable keyboard shortcut manager with action→KeyPress mapping, persistence, case-insensitive key matching, and conflict detection; Phase 3 adds Cmd+B default binding → Toggle Module Library Sidebar (wired via `ApplicationCommandTarget` in `MainComponent`)
- `Source/UI/ModuleLibraryComponent.h`: Categorized sidebar with section headers for module drag-and-drop; `paint()` draws a 16×16 category icon (from `lf->peekIcon(catIcon)`) at x=10 per header entry, shifting header text to x=30; null-guarded (falls back to text-at-x=10 when LnF absent)
- `Source/UI/ModMatrixComponent.cpp`: Modulation matrix with undo tracking for routing and parameter changes
- **Visual Signal Flow**: GraphEditor draws animated dots on connections (white for audio, cyan for modulation), pulsing modulation lines, and activity glow on modules. ModuleComponent renders Serum-style modulation rings on knobs. Driven by `AudioEngine::getModulationDisplayInfo()` cached at 30fps.
- `Source/Modules/FX/ChorusModule.h`: Chorus effect using `juce::dsp::Chorus`, CV modulation on Rate/Depth
- `Source/Modules/FX/PhaserModule.h`: Phaser effect using `juce::dsp::Phaser`, CV modulation on Rate/Depth
- `Source/Modules/FX/CompressorModule.h`: Compressor with manual makeup gain
- `Source/Modules/FX/FlangerModule.h`: Flanger via `juce::dsp::Chorus` with low-delay tuning
- `Source/Modules/FX/LimiterModule.h`: Brickwall limiter with input gain drive
- `Source/Modules/FX/PitchShifterModule.h`: Dual-mode pitch (delay-line transposition) / frequency (Hilbert SSB) shifter
- `Source/UI/AIChatComponent.cpp/.h`: Chat interface for AI-assisted patching. **Logging gotcha:** this component registers itself as the global `juce::Logger` (`setCurrentLogger`) and pipes every `juce::Logger::writeToLog(...)` into a `TextEditor` debug console (Debug builds only). Appends are coalesced + the console is length-bounded, but DO NOT add high-frequency `writeToLog` calls (per-parameter, per-sample, per-frame, per-connection) — they run on the UI thread. Keep logging to errors/rare events. (A per-parameter log on preset load once caused a multi-second UI freeze; guarded by `AIStateMapperTest.PresetLoadDoesNotSpamLogger`.) Visibility persists via `ApplicationProperties` key `"aiPanelVisible"` (default false); read at top of `MainComponent::initialiseCommon()`
- **UI rendering perf:** `ModuleComponent` uses `setBufferedToImage(true)` and gates its 15Hz `timerCallback` repaint (RMS-change / active-modulation / sequencer-step-change only) so the GraphEditor's 30Hz connection animation composites cached module images instead of re-running JUCE text layout every frame. Don't reintroduce unconditional per-tick `repaint()` on modules or their always-visible children. A theme switch triggers exactly ONE re-skin pass (`AppLookAndFeel::applyTheme` → `sendLookAndFeelChangeMessage` → single `repaint()`) — no timer or continuous repaint is added. `applyToolbarIcons()` (Drawable clone work) is gated to narrow-mode transitions in `MainComponent::resized()` — not called on every resize frame. Status bar polls at 5 Hz (every 2nd 10 Hz timer tick) and repaints only itself; ZERO `writeToLog` calls in any status-polling path.
- `Source/UI/ScopeComponent.h`: Oscilloscope/waveform display component
- `Source/Modules/FX/DistortionModule.h`: Distortion effect with configurable oversampling (Off/2x/4x), soft-clipping using `tanh`-based curve, Drive and Mix parameters
- `Tests/E2EWorkflowTests.cpp`: E2E workflow tests — preset loading, module drop/delete/replace, connection drag, mod matrix, undo/redo sequences, and stress tests
- `Tests/`: ~460 tests across ~64 suites (audio rendering, integration, component workflow, state management, theme, icon library, status bar, layout, E2E workflow); `IconLibraryTests.cpp` and `StatusBarTests.cpp` are new in Phase 3
- `docs/modulation.md`: Full reference for the modulation routing model, logical-port API, and poly-bus wire rendering
- `Source/UI/Theme/Theme.h`: Pure data model — `Colors`, `Metrics`, `Typography`, `Treatment`, `ThemeStyle`, `Theme` structs with Obsidian defaults; no JUCE GUI deps beyond `juce::Colour`/`juce::String`
- `Source/UI/Theme/ThemeManager.h/.cpp`: Theme registry, active-theme selection, JSON user-theme loading, persistence via shared `ApplicationProperties`
- `Source/UI/Theme/ThemeLoader.h/.cpp`: JSON ↔ `Theme` (parse + serialize); `parseHexColour`, `parseStyle`, `styleToString` helpers; `getLastError()` for test/log use
- `Source/UI/Theme/BuiltInThemes.h/.cpp`: The three built-in `Theme` literals (`makeObsidian`, `makeNeon`, `makeWarm`, `builtInThemes`) with exact colour/metric/treatment values
- `Source/UI/Theme/IconLibrary.h/.cpp`: SVG icon registry — 22 icons in `synth::theme::Icon` enum (TransportPlay through CatUtility); loads icons from `BinaryData` (22 `assets/icons/*.svg` files, hyphen-named); parallel arrays `originals_[]` (untinted) + `drawables_[]` (tinted); `setTintColour(id, colour)` always clones from `originals_[]` to avoid tint accumulation across theme switches; `getDrawable(id)` returns a fresh clone (caller owns); `peekDrawable(id)` returns non-owning pointer; exhaustive `kTable` lookup (`static_assert` guards count); `#ifdef HAS_FONT_ASSETS` guard — returns `nullptr` for all icons in headless test builds without asset target; all callers must null-check. **Icons are SVG Drawable, NOT a font** — no runtime font-family swap involved. `Icon::TransportPlay` is scaffolding (no DrawableButton wired this phase). Waveform glyphs deferred to Phase 4.
- `Source/UI/Theme/AppLookAndFeel.h/.cpp`: Central `LookAndFeel_V4` — stock-widget ColourId mappings + treatment draw helpers (`drawModulePanel`, `drawConnectionWire`, `drawModulationRing`, `fillThemedBackground`); typeface resolution with `HAS_FONT_ASSETS` fallback; 270° knob sweep constants `kRotaryStart`/`kRotaryEnd`; Phase 3 adds: `iconLibrary_` member + `retintIcons()` (called from `applyTheme()` — same single re-skin pass) + `getIcon(id)` / `peekIcon(id)` accessors; `applyTheme()` also sets ListBox and TabbedButtonBar ColourIds; fully themed: ComboBox (pressed/chevron), PopupMenu (drawn tick, submenu arrow, disabled dim), ScrollBar (6 px slim, track+thumb), TabbedButtonBar (bg0 + hairline + accent indicator on active tab). **Font limitation:** all built-in themes share Inter + JetBrains Mono — swapping the embedded typeface *family* at runtime corrupts text globally (JUCE 8 + CoreText mis-indexes already-shaped glyph runs), so themes differ by colour/treatment/glow, not font. The ctor pre-creates all embedded typefaces at startup (a runtime `createSystemTypefaceFor` after rendering is part of the corruption); module-card drop shadows use a cheap layered fill (not `juce::DropShadow`'s gaussian blur) so zoom stays smooth. See `docs/theming.md` for the user-facing note.
- `Source/UI/AppearanceSettingsTab.h/.cpp`: The Settings "Appearance" tab — theme picker `ListBox`, swatches, "Open Themes Folder" / "Reload Themes" buttons; `ChangeListener` so external theme switches update the selection
- `docs/theming.md`: User + developer theming guide (token reference, JSON schema, tutorial, treatment params, contrast guidance); Phase 3 adds: Icon enum + tinting section (token→tint map, null-fallback contract, parallel-array design, BinaryData naming convention, how-to-add guide), code-only Metrics layout tokens table, fully-themed widget list (ComboBox/PopupMenu/ScrollBar/TabbedButtonBar), MidiKeyboard unthemed limitation, TransportPlay scaffolding note, waveform-glyphs deferred note
- `docs/layout.md`: Soft-grid model, kGridSize=8 rationale, anti-overlap spiral search, auto-arrange topological layering algorithm (spacing constants, role ranks, undo integration), LayoutUtil API reference; Phase 3 adds: Toolbar & Status Bar Layout section (FlexBox, narrow threshold 480 px, gate logic, Metrics tokens table, panel collapse/persistence), Module Width Buckets section (NARROW/SINGLE/DOUBLE table, kColumnStride derivation, DOUBLE auto-arrange advance, preset 0/1/5 rebake note)
