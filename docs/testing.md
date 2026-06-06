# Testing Guide

All tests use GoogleTest and run headless (no audio device, no GUI window). ~371 tests across ~53 suites.

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

### Component Workflow Tests (~32 tests)

Test UI component interactions using in-process construction (no window, no display).

| Suite | Tests | What it covers |
|-------|-------|----------------|
| MainComponentTest | 6 | AI panel visibility toggle, mod matrix toggle, default configuration, command manager registration, redo shortcut |
| GraphEditorTest | 9 | Module drag-and-drop, port connection via beginConnectionDrag/endConnectionDrag, deletion, mod matrix visibility |
| ModuleComponentTest | 3 | Initialization, resizing, parameter attachment to UI sliders |
| MidiKeyboardModuleTest | 4 | Note on/off, key press handling, velocity |
| VisualBufferTest | 3 | Scope visualization buffer management, read/write, ringbuffer behavior |
| ModuleBaseTest | 4 | Parameter getters, port labels, bypass functionality |
| ModuleBypassTest | 5 | Default state, toggle, signal passing when bypassed |
| VisualSignalFlowTests | 8 | AttenuverterModule peak/mod value tracking, VisualBuffer RMS computation, AudioEngine::getModulationDisplayInfo() population |
| SettingsWindowTest | 8 | Tab structure, tab persistence, audio device selector, AI settings persistence, resize safety, shortcuts reference |
| ShortcutManagerTest | 8 | Default bindings, reverse lookup, conflict detection, persistence round-trip, reset to defaults, display strings |

### State Management Tests (~44 tests)

Test persistence, serialization, and state restoration.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| PresetManagerTest | 9 | Preset listing, load all presets, default preset validation, audio output connectivity |
| UndoRedoTest | 11 | Add/remove modules, connections, parameter changes, complex sequences, rapid operations |
| AIStateMapperTest | 24 | Graph JSON round-trip serialization, parameter validation, modulation serialization, merge mode, schema generation |

### E2E Workflow Tests (23 tests)

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
