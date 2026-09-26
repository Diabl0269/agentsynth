# Architecture Overview

Agent Synth is built on a modular graph architecture using JUCE's `AudioProcessorGraph` (implicitly managed by `AudioEngine`).

This doc is the hub: the project layout, the signal-flow model and the quality bar every
module follows, plus an index into the topic docs that carry the detail.

---

## Project Structure

The build produces four CMake targets:

| Target | Kind | Contents |
|---|---|---|
| `Core` | Static library | All audio modules, `AudioEngine`, `PresetManager`, `AppUndoManager`, theme system, `LayoutUtil`. Headless-testable — no audio device or GUI window required. |
| `AppUI` | Static library | `MainComponent` plus everything under `Source/UI` not already in `Core` (`GraphEditor`, `ModuleComponent`, `AIChatComponent`, `SettingsWindow`, `ModMatrixComponent`, etc.). Links `Core` PUBLIC. Shared by `AgentSynth` and `AgentSynthPlugin` so the app and the plugin build **one** copy of the editor UI from **one** source list — a plugin whose UI drifts from the app's is the exact failure this library exists to prevent. Deliberately not folded into `Core`: `Core` is the headless-testable layer. `Tests` links `AppUI` too instead of compiling its sources a second time; its own `JUCE_MODAL_LOOPS_PERMITTED=1` covers only the test sources. |
| `AgentSynth` | JUCE GUI app | Links `AppUI` + `Core`. Adds only `Source/Main.cpp` — the standalone `JUCEApplication` entry point (and the generated `JuceHeader.h`), which has no meaning inside a plugin and so stays out of `AppUI`. |
| `AgentSynthPlugin` | Audio plugin — VST3 on every platform, + AU on macOS | Links `AppUI` + `Core`. Wraps the same `AudioEngine`/`MainComponent` in a `juce::AudioProcessor` (`Source/Plugin/`). Built when the `ENABLE_PLUGIN` CMake option is on (default `ON`). Standalone format is deliberately excluded from its `FORMATS` list — JUCE would emit a second "Agent Synth.app" that collides with the `AgentSynth` target's own release artifact. See [Plugin Layer](plugin-layer.md) below. |

**Keep central headers light.** A change to `MainComponent.h`, `GraphEditor.h`, `ModuleComponent.h` or `AudioEngine.h` recompiles every file that includes it, directly or through another header, so these headers forward-declare what they only hold by reference or pointer. For example, `AudioEngine` is forward-declared in all four UI headers, and the modulation-routing value types the graph UI stores live in `AudioEngine/ModulationRoutingTypes.h`. A `.cpp` that calls into `AudioEngine` includes `AudioEngine/AudioEngine.h` itself. To measure a header's reach on a configured Ninja build, touch it and count the `Building CXX` lines `ninja` prints.

`Assets` is a separate binary-data target that embeds font files and SVG icons; it is linked into `Core` (and therefore `AppUI`, `AgentSynth`, and `AgentSynthPlugin`) so every target resolves `BinaryData` symbols.

---

## Core Components

The detail for each component below lives in its own doc, linked from the anchor that names it:

- [AudioEngine](audio-engine.md#audioengine) — the realtime render graph, host modes, device callback
- [TransportService (the one clock)](audio-engine.md#transportservice-the-one-clock) — bounce/export, stem export, metronome, input monitoring, mixer solo gate
- [TimelineDoc (the timeline document model)](timeline.md#timelinedoc-the-timeline-document-model)
- [TimelineSnapshot (the audio thread's view of the timeline)](timeline.md#timelinesnapshot-the-audio-threads-view-of-the-timeline) — AutomationKernel, AutomationApplier, AutomationRecorder, UI reflection
- [ProjectBundle (.agsproj)](project-bundle.md#projectbundle-agsproj) — open/save, recent projects, dirty state, autosave, welcome screen
- [ModuleBase](module-base.md#modulebase) — ModuleType enum, node uuid mirror, ModulationTarget, VisualBuffer, logical-port API, extra state, bypass/mute contract, output level stage
- [GraphEditor](graph-editor.md#grapheditor) — the visual patching canvas and its collaborator classes
- [App wiring — who owns the timeline, and every hook that keeps it in step](app-wiring.md#app-wiring--who-owns-the-timeline-and-every-hook-that-keeps-it-in-step) — focus regions, audio recording, latency alignment, device changes, waveform peaks
- [AudioClipStreamer (disk-streaming clip playback)](app-wiring.md#audioclipstreamer-disk-streaming-clip-playback)
- [Asset management](app-wiring.md#asset-management)

## Plugin Layer

See [`plugin-layer.md`](plugin-layer.md) — host modes, ownership,
state format, and hosting third-party VST3/AU plugins inside our own graph (the backend seam,
async publish, out-of-process scanning, editor windows, automation lanes, latency compensation).

## Supporting Components

See [`module-base.md`](module-base.md#supporting-components) —
`LayoutUtil`, `ModuleComponent`, `AttenuverterModule`, `AppUndoManager`, `AppLookAndFeel` +
`ThemeManager`.

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
- **Oversampling**: Nonlinear effects support configurable oversampling (e.g. Distortion and Ring Modulator offer Off/2x/4x modes).
