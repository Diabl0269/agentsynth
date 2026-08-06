# Testing Guide

All tests use GoogleTest and run headless (no audio device, no GUI window). ~523 tests across ~68 suites.

```bash
# Run all tests (ENABLE_TESTS defaults OFF — must be passed explicitly)
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build --target Tests
./build/Tests/Tests

# Run a specific suite
./build/Tests/Tests --gtest_filter="E2EWorkflow*"

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
| MacroControlModuleTest | 14 | Port model (16 channels, 8 visible by default, no MIDI, ModCV role), per-macro CV output, channels above the `Knobs` count silent, hidden knobs keep their values when the bank is re-grown, bipolar mapping, 20 ms smoothing, bypass/mute silence, zero-channel buffer; factory creation, `macroCount`+knob JSON round-trip, one macro fanned out to two destinations |
| FX module tests | 46 | Delay (passthrough, feedback), Distortion (clipping, drive), Reverb (room size), Chorus, Phaser, Compressor, Flanger, Limiter |
| AntiClickTest | 4 | ADSR minimum release, smooth parameter transitions |
| EdgeCaseTests | 21 | Zero-length buffers, extreme parameters, single-sample buffers, rapid parameter changes, large buffers |
| AudioRenderingTests | 26 | Snapshot-based tests comparing bit-perfect output against reference files; covers full chains (Osc->Filter->VCA), modulation accuracy, and External MIDI input |

### Integration Tests (~38 tests)

Test module interactions within the audio graph and cross-system integrations.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| IntegrationTest | 7 | Signal chain routing (Osc->Filter->VCA), LFO->Filter modulation, preset loading with graph structure validation |
| ModMatrixTest | 22 | Add/remove mod routings, amount scaling, channel mapping, modulation chains; Phase 4 adds: `RowHeightIs48` — `kRowHeight == 48`; `ZebraAlternates` — `isZebraRow` false for even, true for odd indices; `HoverStateUpdates` — `setHoveredRow`/`getHoveredRow` round-trip, default -1, redundant-set no-op, reset to -1; `ModMatrixComponentPaintSmokeTest` — paint with two routings, no crash; `HoverResetsAfterRowRemoval` — hover clears to -1 when a routing is removed (componentsChanged path) |
| OllamaProviderTest | 10 | AI LLM HTTP requests, streaming responses, model management, non-blocking discovery; `SendPromptWithNoModelFailsWithoutHittingNetwork` — fail-fast with no network call when `currentModel` is empty; `SendPromptIncludesSelectedModelInRequestBody` — captures the POST body and asserts `"model"` matches `setModel()` (regression lock for f7cba4a / empty-model 400s); `QueuedRequestDuringThreadShutdownStillCompletes` — a request enqueued as the worker winds down still gets a callback (request-loss race); `PendingRequestsAreFailedOnDestruction` — requests still queued at destruction are failed *before* `~OllamaProvider()` returns. Both use bounded `condition_variable` waits and fail on timeout rather than sleeping. **Never call `stopThread(0)` in a test** — it force-kills via `pthread_cancel`, which aborts under glibc |
| AIIntegrationServiceTest | 9 | Module suggestions, parameter recommendations, graph state mapping |

### Component Workflow Tests (~50 tests)

Test UI component interactions using in-process construction (no window, no display).

| Suite | Tests | What it covers |
|-------|-------|----------------|
| MainComponentTest | 21 | AI panel visibility toggle, mod matrix toggle, default configuration, command manager registration, redo shortcut; toolbar narrow/wide mode at 480/1600 px, library sidebar toggle + persistence, AI panel persistence, status bar bounds, canvas non-zero at minimum size, timer gating (5 Hz), patch name default + update on preset load, DrawableButton header buttons; `AiProviderGetsModelSelectedOnStartup` — regression lock (f7cba4a) that a model is selected on startup via `aiChatComponent.refreshModels()` called AFTER `setProvider()` |
| AIChatComponentTest | 3 | Initialization/resizing, send-message updates UI + history via mock provider; `RefreshModelsSelectsModelWhenProviderInstalledAfterConstruction` — reproduces MainComponent's member-init ordering (chat component constructed before a provider is installed), asserting `getCurrentModel()` stays empty until a post-construction `setProvider()` + `refreshModels()` |
| GraphEditorTest | 13 | Module drag-and-drop, port connection via beginConnectionDrag/endConnectionDrag, deletion, mod matrix visibility, snap-on-drop (position is grid-multiple of 8), overlap resolution (second drop at same coord produces non-overlapping bounding boxes); **Macro bank resize** — growing 4→16 knobs keeps the bank's top-left fixed, pushes the module below past `kCollisionGap` and persists its new y to node properties; shrinking 16→4 drops the routing on a hidden jack (and its attenuverter) while leaving a still-visible jack's routing intact |
| ModuleComponentTest | 11 | Initialization, resizing, parameter attachment to UI sliders; bypass/mute/delete are DrawableButtons with correct header bounds; delete button triggers requestDeleteModule; **Macro bank layout** — height equals `macroBankHeight(count)` for 1/4/8/16 knobs, each output jack sits on its own knob row and hit-tests to that macro's index, no input or MIDI jacks, knobs clear the output-label gutter and stay inside the module, rows above the count are hidden |
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
| LayoutUtilTest | 15 | `snap` round-trips (negative-safe, midpoint), `intersectsAny` gap enforcement + selfId exclusion, `findFreeSlot` returns desired when clear, `findFreeSlot` resolves dense cluster (returned slot overlaps none), `computeAutoArrange` signal-depth layering (x strictly increases per depth, Audio Output in last column, no result-box overlaps); **width-bucket mapping** (Sequencer/PolySequencer/MidiKeyboard→Double, Attenuverter→Narrow, all others→Single); **bucket constants on grid** (kNarrowWidth/kSingleWidth/kDoubleWidth all %8==0, kDoubleWidth==2×kSingleWidth); **column stride** (kSingleWidth + kLayerGapX == 360); **Macro bank geometry** (`macroBankHeight` grows one `kMacroRowH` per macro, `macroRowCentreY` evenly spaced and always inside the bank); **`resolveOverlapsAfterResize`** (no-op when clear, pushes the neighbour below past the new bottom edge and on-grid, never moves the resized module, cascades through a stack until nothing overlaps, shrinking moves nobody back) |

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

### Icon Library Tests (~11 tests)

Suite `Tests/IconLibraryTests.cpp` covering `Source/UI/Theme/IconLibrary.h/.cpp`.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| IconLibraryTest | 11 | `AllIconEnumValuesHaveEntry` — every Icon < kCount returns non-null getDrawable when assets present; `NullFallbackWhenAssetsAbsent` — no-assets path returns nullptr without crash; `ClonedDrawableIsIndependent` — two getDrawable calls return distinct raw pointers; `TintColourChangeApplied` — setTintColour(icon, c1) then setTintColour(icon, c2) → result contains c2 not c1; `RetintMultipleSwitchesStable` — 100× alternating setTintColour loop, final colour matches last set value; `SvgBinaryDataNamingConvention` — raw BinaryData symbol non-null (guards CMake renames); `TransportPlayIsScaffolding` — Icon::TransportPlay loads without crash; no DrawableButton wired this phase; Phase 4 adds: `WaveformIconsLoad` — all four WaveformSine/Saw/Square/Triangle return non-null; `WaveformIconsTintedToTextPrimary` — tint colour matches textPrimary after retintIcons(); `WaveformIconKTableCount` — static_assert count == 26 enforced at compile time; `WaveformBinaryDataSymbolsPresent` — waveformsine/saw/square/triangle_svg symbols non-null |

### Status Bar Tests (~9 tests)

New suite `Tests/StatusBarTests.cpp` covering `Source/UI/StatusBarComponent.h/.cpp` and AudioEngine additions.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| StatusBarTest | 9 | `ConstructsWithoutCrash`; `RendersNonEmptyImage` (createComponentSnapshot 400×24); `FormatCpu` — 0.0%, 75.6%, 100.0%; `FormatVoices` — 0→"0 voices", 1→"1 voice", 8→"8 voices"; `FormatPatch` — ""→"Untitled", named patch passes through; `GatedRepaintDoesNotFireOnUnchangedValues` — update() twice with same values triggers repaint only once; `AudioEngine_GetActiveVoiceInfo_ReturnsZeroWithoutPolyModules`; `AudioEngine_CountsPolyMidiVoices` — maxVoices==8 after adding PolyMidiModule; `MasterMute_ZeroesOutput` — setMasterMute(true) → output buffer all zeros post-processBlock |

### Frequency Response Tests (~13 tests)

New suite `Tests/FrequencyResponseTests.cpp` covering `Source/UI/FrequencyResponseComponent.h`.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| FrequencyResponseTest | 13 | `PeakBinFindsMaximum`/`PeakBinFindsFirstMaxWhenTied`/`PeakBinHandlesEmpty`/`PeakBinSingleElement` — `findPeakBin` returns the max-magnitude bin (first index on ties, -1 for null/zero-length, 0 for single element); `FormatHzLabel_100Hz`/`_1kHz`/`_10kHz`/`_SubKiloHz`/`_FractionalKiloHz` — `formatHzLabel` yields "100Hz"/"1kHz"/"10kHz"/"440Hz"/"1.5…kHz"; `FreqMappingMonotonic`/`FreqMappingEndpoints` — `freqToXStatic` log map is monotonic and pins 20 Hz→x=0, 20 kHz→x=width; `DbMappingMonotonic` — `dbToYStatic` monotonic across +20/0/-20 dB; `PaintSmoke` — paints into a `juce::Image` with no crash, produces opaque pixels |

### Scope Tests (~10 tests)

New suite `Tests/ScopeTests.cpp` covering `Source/UI/ScopeComponent.h`.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| ScopeTest | 10 | `NoSignalThreshold_Zero`/`_BoundaryInclusive`/`_JustAbove`/`_FullAmplitude` — `isNoSignal` is true for peak ≤ 0.02f (boundary inclusive), false above; `AmplitudeMapping_TopNearBoundsTop`/`_BottomNearBoundsBottom`/`_ZeroIsVerticalCentre`/`_Symmetry` — `amplitudeToY` maps +1→above centre, -1→below centre, 0→exact vertical centre, symmetric about centre; `PaintSmokeNoSignal`/`PaintSmokeWithSignal` — paint the silent (No-Signal empty-state) and signal states into a `juce::Image` with no crash |

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

### AI Patch Validation Tests (~19 tests)

`Tests/AIPatchValidationTests.cpp` is table-driven: one deliberately malformed patch per
`PatchValidationError` value, each asserting the **exact** enumerator rather than merely "was
rejected". `EveryErrorValueIsCovered` walks the enum and fails if a newly added value has no case,
so the table cannot silently fall behind the enum.

The same file pins the `getPatchSchema()` contract — the module-type enum matches the factory,
choice parameters are enumerated, `additionalProperties` stays open for numeric parameters, and no
reference data is offered as an output field.

`Tests/AIPatchRetryTests.cpp` covers `applyPatchWithRetry` with a scripted provider double that
answers synchronously (so no message loop is needed): that the validation message reaches the
model, that retries stop at `kMaxPatchRetries`, and that the mode repair fires only for a
rejected, mode-less patch and never in the destructive merge-to-replace direction.

## Measurement Harness (not a test)

`Tools/AIPatchHarness` measures how often real model output passes `validatePatch`, replaying a
fixed prompt set through the production path and tallying rejections by error value. It needs a
live Ollama, so it is opt-in and never built by CI:

```bash
cmake -S . -B build -DENABLE_AI_HARNESS=ON
cmake --build build --target AIPatchHarness
./build/Tools/AIPatchHarness/AIPatchHarness --model <tag> --runs 2
```

See `Tools/AIPatchHarness/README.md` — in particular that `format` enforcement is backend-dependent
and that `OllamaProvider` times out at 240 s (`kChatRequestTimeoutMs`), which shows up as a
provider error rather than a rejection.

`Tools/AIEvalHarness` scores a different thing: of the patches that pass validation and apply, are
they actually usable — has an output, that output is reachable from a real sound source, every
parameter in range? It replays 40 golden prompts and runs `Source/AI/PatchEval.h`'s checks against
the resulting graph. Same opt-in flag:

```bash
cmake -S . -B build -DENABLE_AI_HARNESS=ON
cmake --build build --target AIEvalHarness
./build/Tools/AIEvalHarness/AIEvalHarness --model <tag> --runs 2
```

The structural checks themselves (`evaluatePatch()`) have no model dependency and are covered by
`Tests/PatchEvalTests.cpp` in the regular suite — only the golden-prompt replay needs Ollama.

## Adding Tests for New Modules

When adding a new audio module:

1. **Unit tests** in `Tests/<ModuleName>Tests.cpp` — test DSP output, parameter handling, edge cases
2. **E2E coverage** — add the module's name string to the `moduleTypes` array in `E2EWorkflowTest.DropAllModuleTypes_NoCrash`
3. **Add to `Tests/CMakeLists.txt`**

## Snapshot Testing

The `AudioRenderingTests` suite compares rendered audio against "golden" reference files stored in `Tests/reference/`.

- **To run**: `./build/Tests/Tests --gtest_filter="AudioRenderingTest.Snapshot*"`
- **To update references**: If you intentionally change DSP logic (e.g., a better filter algorithm) and want to update the baseline:
  ```bash
  bash scripts/update-reference.sh
  ```
- **To listen**: Use `scripts/play-reference.sh <filename>` (requires `ffplay` or `aplay`).

## Build

```bash
cmake -S . -B build
cmake --build build
```

`ENABLE_TESTS` defaults `OFF` — pass `-DENABLE_TESTS=ON` to generate the `Tests` target. `ENABLE_COVERAGE` is a separate opt-in flag used by the Ubuntu CI job and `scripts/coverage.sh`.

## Git Hooks

Install once per clone (hooks are **not** auto-installed):

```bash
bash scripts/install-hooks.sh
```

Two hooks are registered:

- **pre-commit** (`scripts/pre-commit-lint.sh`): runs `clang-format --dry-run --Werror` on staged `Source/` and `Tests/` C/C++ files. Fast; mirrors the CI Lint job. Also warns if the local `clang-format` version differs from the pin in `.clang-format-version`.
- **pre-push** (`scripts/pre-push-release-test.sh`): runs clang-format lint on all C/C++ sources, then a Release build + full test suite. The first push configures the `build-release/` directory; subsequent pushes are fast incremental rebuilds. This catches UB and segfaults that Debug mode hides (zero-initialized memory masks use-after-free). Also warns if the local `clang-format` version differs from the pin in `.clang-format-version`.

Bypass a single invocation with `--no-verify`:

```bash
git commit --no-verify
git push --no-verify
```

Run manually at any time:

```bash
bash scripts/pre-commit-lint.sh        # lint staged files
bash scripts/pre-push-release-test.sh  # lint + Release build + tests
```

**clang-format version note:** clang-format is pinned via the PyPI `clang-format` wheel to the version recorded in `.clang-format-version`. CI installs that exact version with `pip install "clang-format==$(cat .clang-format-version)"` (after `actions/setup-python`), so CI and the local hooks run the identical binary — eliminating "hook passes locally but CI fails" drift. Install or update locally with the same command:

```bash
pip install "clang-format==$(cat .clang-format-version)"
```

## CI Pipeline

CI runs via `.github/workflows/ci.yml` on pull requests to `main` only, path-filtered to changes under `Source/**`, `Tests/**`, `CMakeLists.txt`, `Tests/CMakeLists.txt`, `scripts/**`, or either workflow file (`ci.yml` / `build-artifacts.yml`).

There are **five jobs**:

| Job | Runner | Config | Notes |
|-----|--------|--------|-------|
| **Lint** | `ubuntu-latest` | — | Installs the pinned `clang-format` PyPI wheel (version from `.clang-format-version`) via `pip install "clang-format==$(cat .clang-format-version)"` after `actions/setup-python`; runs `--dry-run --Werror` over `Source/` and `Tests/`. Fast (~30 s) — gives formatting feedback without waiting for a full build. |
| **Build, Test, and Coverage** | `ubuntu-latest` | Debug + clang + `ENABLE_COVERAGE=ON` | Runs tests, then `bash scripts/coverage.sh --report-only` (skips re-build; only merges profdata and checks the 85% line-coverage threshold). |
| **Build and Test (ASAN)** | `ubuntu-latest` | `RelWithDebInfo` + `-fsanitize=address` | **Label-gated** — only runs when the PR carries the `run-asan` label. `ASAN_OPTIONS=detect_leaks=0`. |
| **Build and Test (macOS)** | `macos-latest` | Release | Catches UB/segfaults and cross-platform issues. |
| **Build and Test (Windows)** | `windows-latest` | Release | Catches UB/segfaults and cross-platform issues. |

### Optimizations

- **ccache**: Compiler cache avoids recompiling unchanged files. `CMAKE_C/CXX_COMPILER_LAUNCHER=ccache`, 500 MB max size, cached at `~/.ccache` (Linux) / `~/Library/Caches/ccache` (macOS) / `C:\Users\runneradmin\AppData\Local\ccache` (Windows), keyed by commit SHA with prefix restore.
- **FetchContent caching**: `build/_deps` (JUCE 8.0.3 + GoogleTest 1.14.0) cached by `actions/cache`, keyed on `CMakeLists.txt` + `Tests/CMakeLists.txt` hashes.
- **`JUCE_WEB_BROWSER=0`**: Drops unused WebBrowserComponent and removes WebKit/libsoup deps on Linux.
- **Separate lint job**: Instant formatting feedback without waiting for a full build.
- **apt package caching**: `awalsh128/cache-apt-pkgs-action` caches Ubuntu packages across runs.
- **`coverage.sh --report-only`**: In CI, skips redundant configure/build/test steps and only merges profdata + generates the report.

### What didn't work

- **Unity builds** (`CMAKE_UNITY_BUILD`): Incompatible with JUCE — Obj-C++ `.mm` files cannot be merged into C++ unity translation units.
- **Precompiled headers**: JUCE module `.cpp` files have guards against being pre-included; on macOS, `.mm` files also require Obj-C++ mode which conflicts with a C++ PCH.

### Post-merge artifact builds

After a merge to `main`, `.github/workflows/build-artifacts.yml` triggers automatically. It builds and packages the app on Ubuntu, macOS, and Windows (no tests — CI already ran them on the PR) using **Ninja on all three platforms** (Windows via the MSVC dev environment), so the build path matches the PR CI exactly; it then bumps the version tag and creates a GitHub release with all three platform artifacts. The tag-and-release step runs **only on `push` to `main`** — a manual `workflow_dispatch` run is a build-only dry-run, useful for validating the matrix (including the Windows build) before merging. Docs-only / non-code merges are skipped via a `paths-ignore` filter: pushes that touch only `**/*.md`, `docs/**`, `LICENSE`, `.gitignore`, `.clang-format`, `.clang-format-version`, `.claude/**`, or `mockups/**` produce no build, no version bump, and no release; mixed code+docs merges still release as normal.
