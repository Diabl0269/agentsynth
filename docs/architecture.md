# Architecture Overview

Agent Synth is built on a modular graph architecture using JUCE's `AudioProcessorGraph` (implicitly managed by `AudioEngine`).

---

## Project Structure

The build produces two CMake targets:

| Target | Kind | Contents |
|---|---|---|
| `Core` | Static library | All audio modules, `AudioEngine`, `PresetManager`, `AppUndoManager`, theme system, `LayoutUtil`. Headless-testable — no audio device or GUI window required. |
| `AgentSynth` | JUCE GUI app | Links `Core`. Houses all UI components: `GraphEditor`, `ModuleComponent`, `ToolbarComponent`, `StatusBarComponent`, etc. |

`Assets` is a separate binary-data target that embeds font files and SVG icons; it is linked into `Core` so both the app and test targets resolve `BinaryData` symbols.

---

## Core Components

### 1. AudioEngine

`Source/AudioEngine.h/.cpp`

Manages audio device I/O (via `juce::AudioIODeviceCallback`), the `juce::AudioProcessorGraph`, and modulation-matrix routing. Key responsibilities:

- **Audio callback** — `audioDeviceIOCallbackWithContext` drives `mainProcessorGraph.processBlock`, then applies master-mute zero-fill **after** `processBlock` so sequencers, LFOs, and envelopes keep advancing even while muted. `masterMuted_` is `std::atomic<bool>`.
- **Module lifecycle** — dynamic addition/removal of modules and connections in the graph.
- **Preset load/save** — delegates to `PresetManager`.
- **Modulation-matrix view** — `getModulationRoutings()` returns a `std::vector<ModulationRouting>` derived on demand from the live graph. Never serialized. `RoutingKind` values: `AttenuverterChain`, `DirectCV`, `PolyBus`. `getActiveModRoutings()` and `getModulationDisplayInfo()` are thin adapters over it.
- **Voice count / mute API** — `getDisplayVoiceCount()`, `setMasterMute(bool)`, `isMasterMuted()`.
- Requires `#include <bit>` for `std::popcount` (C++20).

### 2. ModuleBase

`Source/Modules/ModuleBase.h`

Abstract base class for every audio processing unit. Extends `juce::AudioProcessor`.

#### ModuleType enum

Every concrete module implements `virtual ModuleType getModuleType() const = 0`. Current values:

```
Oscillator, Filter, VCA, ADSR, LFO, Sequencer, PolySequencer,
MidiKeyboard, PolyMidi, ExternalMidi, Attenuverter,
Delay, Distortion, Reverb, Chorus, Phaser, Compressor, Flanger, Limiter, VoiceMixer,
Bitcrusher, PitchShifter, Noise
```

`ModuleType` is consumed by `LayoutUtil::getModuleWidthBucket` to classify modules into width buckets (Narrow / Single / Double) and by `ModuleComponent` for type-safe UI layout switching.

#### ModulationTarget / ModulationCategory

- `ModulationTarget { juce::String name; int channelIndex; }` — describes a modulatable parameter and the audio-buffer channel that carries its CV signal. Returned by `virtual getModulationTargets()`.
- `ModulationCategory` enum: `Envelope, LFO, Oscillator, Sequencer, Filter, FX, Other`. Returned by `virtual getModulationCategory()`.

#### VisualBuffer

Modules may opt in to a thread-safe `VisualBuffer` (circular buffer of `std::atomic<float>`) for scope visualization. Enabled/disabled via `enableVisualBuffer(bool)` and accessed via `getVisualBuffer()`.

#### Logical-Port API

Maps raw audio-buffer channel indices to the visible jack slots shown in the UI.

| Symbol | Description |
|---|---|
| `PortRole` | Enum: `Audio, ModCV, Pitch, Gate, Midi, Other` |
| `LogicalPort` | `{ int visibleJackIndex; PortRole role; bool isPolyGroupHead; int polyVoiceSpan; }` |
| `mapInputChannel(int rawChannel)` | Virtual — returns `LogicalPort` for a raw input channel |
| `mapOutputChannel(int rawChannel)` | Virtual — returns `LogicalPort` for a raw output channel |
| `getVisibleInputPortCount()` / `getVisibleOutputPortCount()` | How many jacks the UI renders |

`GraphEditor` uses this API to anchor wire endpoints to the correct visible jack regardless of how many raw channels are fanned out underneath.

#### isAutoPromotableModTarget

`virtual bool isAutoPromotableModTarget(int dstChannel) const`

Guards poly-mode CV inputs from being auto-wrapped in an `AttenuverterModule` by the AI/routing layer. Default: returns `true` iff `dstChannel` is in `getModulationTargets()`. Modules override this to exclude poly-voice pitch/gate channels which should not receive an attenuverter.

#### Port Labels

`virtual juce::String getInputPortLabel(int channelIndex) const` / `getOutputPortLabel(int channelIndex)` — overridden per-module to provide descriptive jack names (e.g. "CV In", "Audio L") shown in the UI.

#### Bypass/Mute Contract

Every `processBlock` override **must** honour both flags using **two separate branches**:

```cpp
if (isBypassed()) {
    // Dry pass-through — do NOT touch audio channels (ch0, ch1).
    // Clear CV channels (index >= 2) so mod CV does not leak as audio.
    return;
}
if (isMuted()) {
    buffer.clear();
    return;
}
// ... normal DSP ...
```

**Never** collapse to `if (isBypassed() || isMuted()) buffer.clear()` — that silences on bypass instead of passing the dry signal through.

**Exception:** pure source modules with no audio input (e.g. `OscillatorModule`, `PolyMidiModule`) clear their output on bypass because there is no dry signal to pass through.

### 3. GraphEditor

`Source/UI/GraphEditor.h/.cpp`

The visual patching interface. Lives in the `AgentSynth` app target.

- **Zoom/pan** — `zoomLevel` + `panOffset`; `mouseWheelMove` / `mouseDrag` on the canvas.
- **Wire drawing** — poly-bus wires (collapsed N-voice `DirectCV` connections) rendered with an "xN" badge; wire endpoints anchored to visible jacks via `ModuleBase`'s logical-port API.
- **Drag-to-connect** — `beginConnectionDrag` / `dragConnection` / `endConnectionDrag`; maps UI gestures to `AudioProcessorGraph` connections.
- **Module drag** — `finalizeModuleDrag` snaps the released module to the 8 px grid and resolves overlaps via spiral search. A live drag-preview system (`beginDragPreview` / `updateDragPreview` / `endDragPreview`) shows a themed grid-dot overlay plus a snapped landing ghost during drags.
- **Library drops** — `resolvePlacement` + `finalizeModuleDrag` run on the real component after `updateComponents()` so the final position anti-overlaps using true pixel dimensions.
- **Auto-arrange** — `autoArrange()` (triggered by Cmd+L or the toolbar button) topologically layers modules by signal-flow depth in a single undo step. See `docs/layout.md` for the full layout model.
- **Delete** — `requestDeleteModule(NodeID)` is the canonical deletion entry point; `ModuleComponent::deleteButton.onClick` delegates here.

See [`docs/layout.md`](layout.md) for the grid model, anti-overlap algorithm, and `autoArrange` constants.

---

## Supporting Components

### LayoutUtil

`Source/UI/LayoutUtil.h/.cpp`

Stateless grid-layout helpers (`snap`, `intersectsAny`, `findFreeSlot`, `computeAutoArrange`). No JUCE GUI dependencies — fully headless-testable. See [`docs/layout.md`](layout.md) for the full API reference.

### ModuleComponent

`Source/UI/ModuleComponent.h/.cpp`

Auto-generates parameter UI from `ModuleBase` metadata using type-safe `ModuleType` switching. Modulation rings read `AudioEngine::getModulationRoutings()`. Uses `setBufferedToImage(true)` and gates its 15 Hz timer repaint so the `GraphEditor`'s 30 Hz connection animation composites cached module images without re-running JUCE text layout every frame.

### AttenuverterModule

`Source/Modules/AttenuverterModule.h`

Intermediary inserted between a modulation source and its destination to scale CV signals. Exposes `lastOutputPeak` / `lastModValue` atomics for UI metering. Constructor default `Amount = 0.0`; set to `1.0` by `addModRouting`, left at `0.0` by `addEmptyModRouting`. See [`docs/modulation.md`](modulation.md) for the full modulation routing model.

### AppUndoManager

`Source/AppUndoManager.h/.cpp`

Snapshot-based undo/redo wrapping `juce::UndoManager`. Structural graph changes (add/remove module, connect/disconnect) are captured as JSON before/after snapshots via `SnapshotAction`. Parameter and position changes have dedicated action types. Safe detach/reattach lifecycle — `setGraphEditor(nullptr)` before graph teardown.

### AppLookAndFeel + ThemeManager

Central `LookAndFeel_V4` subclass and theme registry. Owns all stock-widget re-skins, treatment draw helpers, and the SVG `IconLibrary`. See [`docs/theming.md`](theming.md) for the full token reference, JSON schema, and how-to-add guide.

---

## Signal Flow

Modules communicate via two main signal types:

- **Audio Channels**: Stereo (usually) audio buffers containing PCM data.
- **CV (Control Voltage)**: Handled as control signals within the audio buffer (e.g., ADSR output feeding into VCA input 1).

---

## Quality Standards

All modules follow specific DSP requirements:

- **Smoothing**: All gain/cutoff parameters use linear smoothing to avoid clicks.
- **Antialiasing**: Oscillators use PolyBLEP for sharp waveforms.
- **Oversampling**: Nonlinear effects support configurable oversampling (e.g., Distortion offers Off/2x/4x modes).
