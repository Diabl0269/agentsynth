# Testing Guide

All tests use GoogleTest and run headless (no audio device, no GUI window). ~460 tests across ~64 suites.

```bash
# Run all tests (ENABLE_TESTS defaults OFF — must be passed explicitly)
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build --target GravisynthTests
./build/Tests/GravisynthTests

# Run a specific suite
./build/Tests/GravisynthTests --gtest_filter="E2EWorkflow*"

# Check coverage (threshold: 85%)
bash scripts/coverage.sh
```

By default, builds skip tests to save time. Use the `-DENABLE_TESTS=ON` flag to enable them.

## Test Layers

### Audio Rendering Tests (~122 tests)

Headless DSP tests that render audio through individual modules and verify output characteristics — RMS levels, silence detection, frequency response, waveform accuracy.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| OscillatorTest | 11 | Waveform generation (sine, saw, square, triangle), MIDI response, tuning, frequency accuracy |
| FilterTest | 10 | Low-pass/high-pass filtering, cutoff/resonance parameters, frequency response across 7 filter types |
| ADSRTest | 10 | Attack/sustain/release shapes, retriggering, poly mode, parameter changes during playback |
| LFOModuleTest | 11 | LFO waveform output, rate modulation, sync behavior |
| VCAModuleTest | 5 | Gain application, envelope following, silence detection |
| AttenuverterModuleTest | 4 | CV signal attenuation, bipolar control, CV modulation |
| FX module tests | 46 | Delay (passthrough, feedback), Distortion (clipping, drive), Reverb (room size), Chorus, Phaser, Compressor, Flanger, Limiter |
| AntiClickTest | 4 | ADSR minimum release, smooth parameter transitions |
| EdgeCaseTests | 21 | Zero-length buffers, extreme parameters, single-sample buffers, rapid parameter changes, large buffers |
| AudioRenderingTests | 26 | Snapshot-based tests comparing bit-perfect output against reference files; covers full chains (Osc->Filter->VCA), modulation accuracy, and External MIDI input |

### Integration Tests (~38 tests)

Test module interactions within the audio graph and cross-system integrations.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| IntegrationTest | 7 | Signal chain routing (Osc->Filter->VCA), LFO->Filter modulation, preset loading with graph structure validation |
| ModMatrixTest | 17 | Add/remove mod routings, amount scaling, channel mapping, modulation chains |
| OllamaProviderTest | 5 | AI LLM HTTP requests, streaming responses, model management |
| AIIntegrationServiceTest | 9 | Module suggestions, parameter recommendations, graph state mapping |

### Component Workflow Tests (~50 tests)

Test UI component interactions using in-process construction (no window, no display).

| Suite | Tests | What it covers |
|-------|-------|----------------|
| MainComponentTest | 20 | AI panel visibility toggle, mod matrix toggle, default configuration, command manager registration, redo shortcut; toolbar narrow/wide mode at 480/1600 px, library sidebar toggle + persistence, AI panel persistence, status bar bounds, canvas non-zero at minimum size, timer gating (5 Hz), patch name default + update on preset load, DrawableButton header buttons |
| GraphEditorTest | 11 | Module drag-and-drop, port connection via beginConnectionDrag/endConnectionDrag, deletion, mod matrix visibility, snap-on-drop (position is grid-multiple of 8), overlap resolution (second drop at same coord produces non-overlapping bounding boxes) |
| ModuleComponentTest | 5 | Initialization, resizing, parameter attachment to UI sliders; bypass/mute/delete are DrawableButtons with correct header bounds; delete button triggers requestDeleteModule |
| MidiKeyboardModuleTest | 4 | Note on/off, key press handling, velocity |
| VisualBufferTest | 3 | Scope visualization buffer management, read/write, ringbuffer behavior |
| ModuleBaseTest | 4 | Parameter getters, port labels, bypass functionality |
| ModuleBypassTest | 5 | Default state, toggle, signal passing when bypassed |
| VisualSignalFlowTests | 8 | AttenuverterModule peak/mod value tracking, VisualBuffer RMS computation, AudioEngine::getModulationDisplayInfo() population |
| SettingsWindowTest | 8 | Tab structure, tab persistence, audio device selector, AI settings persistence, resize safety, shortcuts reference |
| ShortcutManagerTest | 8 | Default bindings, reverse lookup, conflict detection, persistence round-trip, reset to defaults, display strings |

#### `createComponentSnapshot` smoke-test pattern

Several new tests use `Component::createComponentSnapshot(bounds)` to verify that a component renders without crashing and produces non-empty pixels, without requiring a real display or window. Example: `StatusBarTests::RendersNonEmptyImage` and `ThemeTests::StyledWidgetSmokeTest.*`. The pattern is:

```cpp
comp.setSize(width, height);
auto img = comp.createComponentSnapshot({0, 0, width, height});
EXPECT_GT(img.getWidth(), 0);
```

#### AppProperties isolation pattern for persistence tests

Tests that read/write `ApplicationProperties` use an isolated temporary directory to avoid contaminating the shared settings file across test runs. The `MainComponent` exposes `getAppPropertiesForTest()` so the test fixture can target a temp dir:

```cpp
// In test SetUp():
auto tmpDir = juce::File::createTempFile("").getParentDirectory()
                  .getChildFile(juce::Uuid().toDashedString());
tmpDir.createDirectory();
// Write a value then verify MainComponent reads it back:
comp.getAppPropertiesForTest().getUserSettings()->setValue("librarySidebarVisible", "0");
// TearDown(): reset to defaults, then tmpDir.deleteRecursively()
```

### State Management Tests (~46 tests)

Test persistence, serialization, and state restoration.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| PresetManagerTest | 12 | Preset listing, load all presets, default preset validation, audio output connectivity, all 7 factory presets load with zero pairwise bounding-box overlaps at kCollisionGap=12; `AllPresetsPositionsOnGrid` (every baked x,y %8==0); estimateModuleSize mirror updated: Sequencer/PolySequencer/MidiKeyboard→560 (kDoubleWidth), Attenuverter excluded via `continue` |
| UndoRedoTest | 12 | Add/remove modules, connections, parameter changes, complex sequences, rapid operations, auto-arrange is a single undo step (one Cmd+Z restores all pre-arrange positions) |
| AIStateMapperTest | 24 | Graph JSON round-trip serialization, parameter validation, modulation serialization, merge mode, schema generation |

### Layout Tests (~8 tests)

Pure/headless tests for the grid-layout and anti-overlap helpers in `Tests/LayoutUtilTests.cpp`.
No JUCE GUI components required.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| LayoutUtilTest | 8 | `snap` round-trips (negative-safe, midpoint), `intersectsAny` gap enforcement + selfId exclusion, `findFreeSlot` returns desired when clear, `findFreeSlot` resolves dense cluster (returned slot overlaps none), `computeAutoArrange` signal-depth layering (x strictly increases per depth, Audio Output in last column, no result-box overlaps); **width-bucket mapping** (Sequencer/PolySequencer/MidiKeyboard→Double, Attenuverter→Narrow, all others→Single); **bucket constants on grid** (kNarrowWidth/kSingleWidth/kDoubleWidth all %8==0, kDoubleWidth==2×kSingleWidth); **column stride** (kSingleWidth + kLayerGapX == 360) |

### Theme Tests (~30 tests)

Tests for the theme system — `Tests/ThemeTests.cpp`. All headless.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| ThemeBuiltInsTest | 2 | Built-in theme registration (obsidian/neon/warm), WCAG AA contrast (≥4.5) for all built-ins |
| ThemeLoaderTest | 8 | JSON round-trip (exact colour/metric/typography/treatment equality), obsidian.gtheme.json vs makeObsidian(), required-key rejection, bad hex rejection, treatment float clamping, schema version rejection, style string round-trip |
| ThemeTest (fixture) | 4 | Persistence + restore via ApplicationProperties, broadcast on change / idempotency, unknown id rejection, user theme replace-by-id |
| ThemeLookAndFeelTest | 2 | All ColourId mappings from spec section 3, draw-helper smoke tests (fillThemedBackground / drawModulePanel / drawConnectionWire / drawModulationRing / drawRotarySlider into headless image) |
| ThemeLookAndFeelTest (extended) | 4 | `ApplyThemeSetsEveryColourId` extended to cover ListBox and TabbedButtonBar ColourIds; `RetintIconsCalledByApplyTheme` (getIcon non-null across 3 built-ins); `MetricsCodeOnlyFieldsHaveExpectedDefaults` (toolbarHeight==36, statusBarHeight==24, etc.); `MetricsCodeOnlyFieldsNotInJSON` (ThemeLoader output does not contain "toolbarHeight") |
| StyledWidgetSmokeTest | 9 | `drawComboBox` normal/pressed/disabled × 3 themes; `drawComboBoxTextWhenNothingSelected`; `drawPopupMenuItem` separator/highlighted/ticked/disabled/hasSubMenu; `getDefaultScrollbarWidth()==6`; `drawScrollbar` V/H × over/down; `drawScrollbarButton`; `drawTabbedButtonBarBackground` + `drawTabButton` active/inactive/hover with snapshot pixel check |

### Icon Library Tests (~7 tests)

New suite `Tests/IconLibraryTests.cpp` covering `Source/UI/Theme/IconLibrary.h/.cpp`.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| IconLibraryTest | 7 | `AllIconEnumValuesHaveEntry` — every Icon < kCount returns non-null getDrawable when assets present; `NullFallbackWhenAssetsAbsent` — no-assets path returns nullptr without crash; `ClonedDrawableIsIndependent` — two getDrawable calls return distinct raw pointers; `TintColourChangeApplied` — setTintColour(icon, c1) then setTintColour(icon, c2) → result contains c2 not c1; `RetintMultipleSwitchesStable` — 100× alternating setTintColour loop, final colour matches last set value; `SvgBinaryDataNamingConvention` — raw BinaryData symbol non-null (guards CMake renames); `TransportPlayIsScaffolding` — Icon::TransportPlay loads without crash; no DrawableButton wired this phase |

### Status Bar Tests (~9 tests)

New suite `Tests/StatusBarTests.cpp` covering `Source/UI/StatusBarComponent.h/.cpp` and AudioEngine additions.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| StatusBarTest | 9 | `ConstructsWithoutCrash`; `RendersNonEmptyImage` (createComponentSnapshot 400×24); `FormatCpu` — 0.0%, 75.6%, 100.0%; `FormatVoices` — 0→"0 voices", 1→"1 voice", 8→"8 voices"; `FormatPatch` — ""→"Untitled", named patch passes through; `GatedRepaintDoesNotFireOnUnchangedValues` — update() twice with same values triggers repaint only once; `AudioEngine_GetActiveVoiceInfo_ReturnsZeroWithoutPolyModules`; `AudioEngine_CountsPolyMidiVoices` — maxVoices==8 after adding PolyMidiModule; `MasterMute_ZeroesOutput` — setMasterMute(true) → output buffer all zeros post-processBlock |

### E2E Workflow Tests (24 tests)

Full application workflow tests in `Tests/E2EWorkflowTests.cpp`. Each test constructs a complete `MainComponent` with a mock AI provider and exercises real UI interaction code paths.

| Area | Tests | What it covers |
|------|-------|----------------|
| App Initialization | 3 | Default patch has nodes/connections, panel toggle visibility, fresh undo state |
| Preset Management | 3 | Load preset updates graph, load all 7 presets without crash, load-modify-undo |
| Module Management | 4 | Drop module via `itemDropped()`, drop all 17 module types, delete module, replace module type |
| Connections | 4 | Connect ports via `beginConnectionDrag()`/`endConnectionDrag()` with `localPointToGlobal()` coordinate conversion, disconnect, MIDI connections, mod routing creates attenuverter |
| Mod Matrix | 4 | Add empty routing, configure source/dest, adjust CV amount, delete routing |
| Undo/Redo | 4 | Undo add-module, complex sequences, preset-load-then-modify, rapid 5-module sequence |
| Combined Workflows | 1 | Full preset-modify-connect-undo-redo workflow |
| Layout / Auto-Arrange | 1 | Load preset, call autoArrange(); all module comps non-overlapping AND connection/node counts unchanged |

#### Key patterns

- **Coordinate conversion**: Components are nested inside MainComponent, so connection tests use `localPointToGlobal()` to convert port positions to screen coordinates for `endConnectionDrag()`.
- **Relative counts**: The default patch creates ~14 nodes, so all tests use `initialCount + N` rather than absolute values.
- **Module lookup**: `findNewModule(name, initialNodeIDs)` finds only modules added after a snapshot, avoiding false matches with default patch modules.
- **Preset loading**: Must call `editor().detachAllModuleComponents()` before `PresetManager::loadPreset()` to avoid use-after-free.

## Adding Tests for New Modules

When adding a new audio module:

1. **Unit tests** in `Tests/<ModuleName>Tests.cpp` — test DSP output, parameter handling, edge cases
2. **E2E coverage** — add the module's name string to the `moduleTypes` array in `E2EWorkflowTest.DropAllModuleTypes_NoCrash`
3. **Add to `Tests/CMakeLists.txt`**

## Snapshot Testing

The `AudioRenderingTests` suite compares rendered audio against "golden" reference files stored in `Tests/reference/`.

- **To run**: `./build/Tests/GravisynthTests --gtest_filter="AudioRenderingTest.Snapshot*"`
- **To update references**: If you intentionally change DSP logic (e.g., a better filter algorithm) and want to update the baseline:
  ```bash
  bash scripts/update-reference.sh
  ```
- **To listen**: Use `scripts/play-reference.sh <filename>` (requires `ffplay` or `aplay`).

## CI

Tests run automatically on every PR across Ubuntu, macOS, and Windows. Coverage is enforced at 85% on the Ubuntu Debug build. See [CLAUDE.md](../CLAUDE.md) for full CI details.
