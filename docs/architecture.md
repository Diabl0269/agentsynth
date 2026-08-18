# Architecture Overview

Agent Synth is built on a modular graph architecture using JUCE's `AudioProcessorGraph` (implicitly managed by `AudioEngine`).

---

## Project Structure

The build produces four CMake targets:

| Target | Kind | Contents |
|---|---|---|
| `Core` | Static library | All audio modules, `AudioEngine`, `PresetManager`, `AppUndoManager`, theme system, `LayoutUtil`. Headless-testable — no audio device or GUI window required. |
| `AppUI` | Static library | `MainComponent` plus everything under `Source/UI` not already in `Core` (`GraphEditor`, `ModuleComponent`, `AIChatComponent`, `SettingsWindow`, `ModMatrixComponent`, etc.). Links `Core` PUBLIC. Shared by `AgentSynth` and `AgentSynthPlugin` so the app and the plugin build **one** copy of the editor UI from **one** source list — a plugin whose UI drifts from the app's is the exact failure this library exists to prevent. Deliberately not folded into `Core`: `Core` is the headless-testable layer, and the `Tests` target still compiles these sources itself with `JUCE_MODAL_LOOPS_PERMITTED=1`, which `AppUI` is not built with. |
| `AgentSynth` | JUCE GUI app | Links `AppUI` + `Core`. Adds only `Source/Main.cpp` — the standalone `JUCEApplication` entry point (and the generated `JuceHeader.h`), which has no meaning inside a plugin and so stays out of `AppUI`. |
| `AgentSynthPlugin` | Audio plugin — VST3 on every platform, + AU on macOS | Links `AppUI` + `Core`. Wraps the same `AudioEngine`/`MainComponent` in a `juce::AudioProcessor` (`Source/Plugin/`). Built when the `ENABLE_PLUGIN` CMake option is on (default `ON`). Standalone format is deliberately excluded from its `FORMATS` list — JUCE would emit a second "Agent Synth.app" that collides with the `AgentSynth` target's own release artifact. See [Plugin Layer](#plugin-layer) below. |

`Assets` is a separate binary-data target that embeds font files and SVG icons; it is linked into `Core` (and therefore `AppUI`, `AgentSynth`, and `AgentSynthPlugin`) so every target resolves `BinaryData` symbols.

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
- **Host modes** — `HostMode::Standalone` (default) vs `HostMode::Hosted` (used by the audio-plugin wrapper). Both funnel through one private `renderNextBlock` so master-mute semantics can't drift between them. See [Plugin Layer](#plugin-layer) below.
- Requires `#include <bit>` for `std::popcount` (C++20).

### 2. ModuleBase

`Source/Modules/ModuleBase.h`

Abstract base class for every audio processing unit. Extends `juce::AudioProcessor`.

#### ModuleType enum

Every concrete module implements `virtual ModuleType getModuleType() const = 0`. Current values:

```
Oscillator, Filter, VCA, ADSR, LFO, Sequencer, PolySequencer,
MidiKeyboard, PolyMidi, ExternalMidi, Attenuverter,
Delay, Distortion, Reverb, Chorus, Phaser, Compressor, Flanger, Limiter,
ParametricEQ, VoiceMixer, Bitcrusher, PitchShifter, Noise, Math, Sampler, Wavetable,
MacroControl, SampleHold, EnvelopeFollower, Comparator
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
| `JackTarget` / `getJackTargets(jack, isInput)` | Inverse of `mapInput/OutputChannel` — every poly-group head anchored to a visible jack; drives connection creation in `GraphEditor` |

`GraphEditor` uses this API to anchor wire endpoints to the correct visible jack regardless of how many raw channels are fanned out underneath, and to resolve which raw channels a dragged cable or poly toggle should wire (`GraphEditor::resolvePolyLink`, `rewireForPolyChange`). See [docs/modulation.md](modulation.md#creating-poly-connections).

#### isAutoPromotableModTarget

`virtual bool isAutoPromotableModTarget(int dstChannel) const`

Guards poly-mode CV inputs from being auto-wrapped in an `AttenuverterModule` by the AI/routing layer. Default: returns `true` iff `dstChannel` is in `getModulationTargets()`. Modules override this to exclude poly-voice pitch/gate channels which should not receive an attenuverter.

#### Extra (non-parameter) State

`virtual juce::var getExtraState() const` / `virtual void setExtraState(const juce::var&)`

For state that has to survive a graph rebuild but is not expressible as a `juce::AudioProcessorParameter` — today only `SamplerModule`'s loaded file path. `AIStateMapper::graphToJSON` writes whatever `getExtraState()` returns as the node's `"state"` property, and `applyJSONToGraph` feeds it back through `setExtraState()`. Return a **void** `var` when there is nothing to persist, so modules that do not use the hook add no JSON.

This matters because preset load — and any undo that has to fall back to a full rebuild — goes through `graphToJSON` → `applyJSONToGraph`, which rebuilds processors from scratch: anything not in that JSON is silently lost. (An ordinary undo restores by diffing and keeps the processor, so it re-applies `"state"` only when it actually changed — see [AppUndoManager](#appundomanager).)

`setExtraState` is only ever called on the **trusted** path (our own snapshots and presets). A module may legitimately read this as a filename, so honouring it for untrusted model output would turn a patch suggestion into an arbitrary file read.

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

**Exception:** modules with no dry audio path clear their output on bypass, because there is nothing to pass through. Two shapes qualify:

- **Pure sources** — no audio *input* (e.g. `OscillatorModule`, `PolyMidiModule`).
- **Audio-in / CV-out taps** — no audio *output* (e.g. `EnvelopeFollowerModule`, whose ch0 output is Env CV, and `ComparatorModule`, whose outputs are Gate / Inverse). Passing the dry signal through would push audio-rate samples into a CV destination, which is worse than emitting no modulation.

Both still use two separate branches, never a fused `if (isBypassed() || isMuted())`, so the intent stays explicit.

#### Output Level Stage

`ModuleBase` offers an opt-in output-level parameter for modules whose output is audio — `addOutputLevelParameter()` in the ctor, `prepareOutputLevel(sampleRate)` in `prepareToPlay`, `applyOutputLevel(buffer, numAudioChannels)` at the end of the **normal** `processBlock` path. It is a no-op on modules that never opt in.

Two constraints follow from the contract above: `applyOutputLevel` must sit **after** both early returns (a bypassed module passes dry audio through at full level; a muted one is already cleared), and `numAudioChannels` must exclude CV channels. Full rules, including why this is opt-in rather than universal and why it must be the last parameter added, live in [`fx_modules.md § Output Level`](fx_modules.md#output-level-shared-stage).

Related: look parameters up with `findParameterByID(processor, "paramID")` rather than `getParameters()[n]`. Parameter order is not part of a module's contract, and positional lookups silently repoint when a parameter is added.

### 3. GraphEditor

`Source/UI/GraphEditor.h/.cpp`

The visual patching interface. Lives in the `AgentSynth` app target.

- **Zoom/pan** — `zoomLevel` + `panOffset`; `mouseWheelMove` / `mouseDrag` on the canvas.
- **Wire drawing** — poly-bus wires (collapsed N-voice `DirectCV` connections) rendered with an "xN" badge; wire endpoints anchored to visible jacks via `ModuleBase`'s logical-port API.
- **Drag-to-connect** — `beginConnectionDrag` / `dragConnection` / `endConnectionDrag`; resolves the dragged jacks through the logical-port API (`resolvePolyLink`) and creates one connection per voice when both ends front an equally-wide poly fan, so a single drag between two poly jacks wires the whole fan at once. `disconnectPort` removes every raw channel a jack owns, including all voices of a fan.
- **Poly toggle rewire** — `rewireForPolyChange` re-anchors a module's existing cables to its new channel layout when its `poly` parameter changes (mono <-> fan), driven by `ModuleComponent`'s `"poly"` parameter listener.
- **Module drag** — `finalizeModuleDrag` snaps the released module to the 8 px grid and resolves overlaps via spiral search. A live drag-preview system (`beginDragPreview` / `updateDragPreview` / `endDragPreview`) shows a themed grid-dot overlay plus a snapped landing ghost during drags.
- **Library drops** — `resolvePlacement` + `finalizeModuleDrag` run on the real component after `updateComponents()` so the final position anti-overlaps using true pixel dimensions.
- **Auto-arrange** — `autoArrange()` (triggered by Cmd+L or the toolbar button) topologically layers modules by signal-flow depth in a single undo step. See `docs/layout.md` for the full layout model.
- **Delete** — `requestDeleteModule(NodeID)` is the canonical deletion entry point; `ModuleComponent::deleteButton.onClick` delegates here.

See [`docs/layout.md`](layout.md) for the grid model, anti-overlap algorithm, and `autoArrange` constants.

---

## Plugin Layer

`Source/Plugin/` wraps the same `AudioEngine` and `MainComponent` the standalone app runs in a `juce::AudioProcessor` (`AgentSynthAudioProcessor` / `AgentSynthPluginEditor`), producing the VST3/AU targets described in Project Structure above.

### Host modes (`AudioEngine::HostMode`)

- **`Standalone`** — `AudioEngine` owns a `juce::AudioDeviceManager`, opens the default output device and every available MIDI input, and is clocked by its own `audioDeviceIOCallbackWithContext`.
- **`Hosted`** — the only mode `AgentSynthAudioProcessor` uses. `initialise()` opens **no** audio device and **no** MIDI input — it only builds the initial patch. The host drives the graph instead, through a mirror of the device-callback trio: `prepareForHost(sampleRate, blockSize, numIn, numOut)` (from `prepareToPlay`), `processHostBlock(buffer, midi)` (from `processBlock`), `releaseFromHost()` (from `releaseResources`). Opening a device from inside a plugin would fight the host for the hardware; opening MIDI input directly would double-trigger notes the host already forwards.
- Both modes funnel through one private `AudioEngine::renderNextBlock`, which calls `mainProcessorGraph.processBlock` and then zero-fills on master-mute — the single choke point that keeps mute semantics from drifting between the two paths.

### Who owns what

- **AudioEngine** — owned by `MainComponent` (`ownedAudioEngine`) on the standalone path; owned by `AgentSynthAudioProcessor` and *injected* into `MainComponent` by reference on the plugin path (`MainComponent(tm, lf, AudioEngine&, ...)`). `MainComponent::initialiseCommon` skips `audioEngine.initialise()`/`shutdown()` whenever it doesn't own the engine, so opening/closing the plugin editor never re-initialises or tears down the running graph — only the processor's constructor/destructor call `initialise()`/`shutdown()`.
- **ThemeManager + AppLookAndFeel** — owned by `AgentSynthAudioProcessor`, not by the editor. A host may create and destroy `AgentSynthPluginEditor` many times over one plugin instance's life (every time its window is closed and reopened), and the LookAndFeel must outlive every `Component` that references it. `PluginProcessor.h` declares `themeManager`/`lookAndFeel` **before** `engine` specifically so they are destroyed *after* it and after any editor (which the base `AudioProcessor` tears down first) — the same shutdown-order guard `Main.cpp` uses for the standalone `AppApplication`.
- **LookAndFeel scope** — `AgentSynthPluginEditor` calls `setLookAndFeel(&processor.getLookAndFeel())` on itself and clears it in its destructor. It never calls `Desktop::setDefaultLookAndFeel`, which is process-global inside the host and would re-skin the host's own windows and every sibling plugin.
- **Settings window** — `MainComponent` passes `showAudioTab = !audioEngine.isHosted()` to `SettingsWindow`, which omits the Audio device tab in the plugin: the host owns the device, so an `AudioDeviceSelectorComponent` there would be inert at best, and dangerous if the user touched it.

### Plugin state format

`AgentSynthAudioProcessor::getStateInformation` / `setStateInformation` serialize a small JSON envelope — `stateVersion`, `patch`, `editorWidth`, `editorHeight`. The `patch` value is exactly `AIStateMapper::graphToJSON`'s output, the same shape a `.json` preset file uses, so plugin session state and preset files stay interchangeable; `setStateInformation` also accepts a bare patch object with no envelope, so a raw preset dropped straight into a host's state slot still loads.

Restore always calls `applyJSONToGraph(..., trusted=false)`: a host session file travels between machines and users, so it goes through the full `validatePatch` boundary (see [`docs/AI_Engine.md`](AI_Engine.md)) and is rejected whole — never partially applied — if it doesn't check out. Around the load, an open editor detaches its module components first (`prepareForGraphReplacement`) and rebuilds them after (`refreshAfterGraphReplacement`) — the same detach-before-clear ordering `GraphEditor::loadPreset` and `MainComponent::aiPatchAboutToApply` use to avoid a `ScopeComponent` timer firing against a freed `VisualBuffer`.

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

#### Restoring a snapshot is a diff, not a rebuild

`SnapshotAction` restores through `AIStateMapper::applySnapshotPreservingNodes`, which compares the target snapshot against the live graph and touches only what differs. It does **not** replay the snapshot through `applyJSONToGraph`, which reaches the same end state by destroying and re-creating every node.

- **Identity is the per-node `uuid`**, not the integer `id` (merge mode renumbers ids). A live node whose uuid appears in the snapshot is **kept** and updated in place — parameters via the trusted param path, position from `"position"`, and `"state"` re-applied through `setExtraState` **only when it changed** (it reloads a sample/wavetable off disk, so an unconditional re-apply would hit the filesystem on every Cmd+Z). A live node whose uuid is absent from the snapshot is removed; a snapshot node with no live match is created, re-adopting both its uuid and its original id when that id is free.
- **A parameter-only undo performs zero topology operations.** No node and no connection is added or removed, so JUCE never rebuilds its render sequence and the audio callback never blocks. Mixed restores batch their topology ops (`UpdateKind::none`) and rebuild exactly once at the end. No graph callback lock is taken at any point.
- **Module runtime state survives.** Sequencer position, envelope stage and sounding voices belong to the processor instance, which is no longer thrown away. This is also what makes hosted plugins and timeline-driven MIDI sources viable: neither can afford to be re-instantiated on every Cmd+Z.
- **Any doubt falls back to the old full rebuild.** `applySnapshotPreservingNodes` plans the whole restore before mutating anything and returns `false` — graph untouched — on a live or snapshot node with no uuid, a duplicate uuid or id, a uuid whose module type no longer matches, an unknown type, a connection naming an undefined node, or a merge delta (`"remove"` / `"removeModulations"`) rather than a full snapshot. `SnapshotAction` then runs `applyJSONToGraph(..., clearExisting=true, trusted=true)`, which is always correct. Correctness beats preservation.
- **UI teardown is now conditional.** The action's `preRestore` hook (`GraphEditor::detachAllModuleComponents`, and the AI service's `aiPatchAboutToApply`) exists to detach UI from processors that are about to be freed, so it fires only when a node is actually being removed — or on the fallback, which frees everything. When nothing is freed there is nothing to detach, and `updateComponents()` (the `postRestore` hook) reconciles additively, so a parameter-only undo no longer destroys and re-creates every `ModuleComponent` either.

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
