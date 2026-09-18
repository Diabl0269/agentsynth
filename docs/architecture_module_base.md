# Module base & supporting components

`ModuleBase` — the contract every module implements — and the shared helpers built on top of it: `LayoutUtil`, `ModuleComponent`, `AppUndoManager`, `AppLookAndFeel`/`ThemeManager`.

Part of the architecture docs — start at [`architecture.md`](architecture.md) for the
layer map, signal flow and the index of the other topic docs.

## 6. ModuleBase

`Source/Modules/ModuleBase.h`

Abstract base class for every audio processing unit. Extends `juce::AudioProcessor`.

### ModuleType enum

Every concrete module implements `virtual ModuleType getModuleType() const = 0`. Current values:

```
Oscillator, Filter, VCA, ADSR, LFO, Sequencer, PolySequencer,
MidiKeyboard, PolyMidi, ExternalMidi, Attenuverter,
Delay, Distortion, Reverb, Chorus, Phaser, Compressor, Flanger, Limiter,
ParametricEQ, VoiceMixer, Bitcrusher, PitchShifter, RingModulator, Noise, Math, Sampler, Wavetable,
MacroControl, SampleHold, EnvelopeFollower, Comparator,
AudioInput, TimelineMidiSource, RecordTap, TimelineAudioSource, HostedPlugin,
MacroInlet, MacroOutlet, MacroMidiInlet, MacroMidiOutlet, ChannelStrip, Master
```

Everything from `AudioInput` on is an internal-only or singleton node: `AudioInput` (a patch singleton), and `TimelineMidiSource` / `RecordTap` / `TimelineAudioSource` / `HostedPlugin` / the four Macro port types / `ChannelStrip` / `Master` (P9-2, [`docs/mixer.md`](mixer.md)), which the module library never offers and a model may never author.

`ModuleType` is consumed by `LayoutUtil::getModuleWidthBucket` to classify modules into width buckets (Narrow / Single / Double) and by `ModuleComponent` for type-safe UI layout switching.

### Node uuid mirror (`setNodeUuid` / `getNodeUuid`)

A graph node's `"uuid"` property (see `AIStateMapper::graphToJSON`) is the app's long-lived node identity — timeline track bindings and automation lanes key on it. It lives in a `juce::NamedValueSet` of `juce::String`s, neither of which the audio thread may touch, so `ModuleBase` **mirrors** it into a fixed `char[64]` the moment it is assigned. `getNodeUuid()` is audio-safe (acquire-load a flag, return the buffer or `""`, never null, no allocation), which is what lets `TimelineMidiSourceModule` `strcmp` itself against a `TimelineSnapshot::TrackInfo::bindingUuid`.

- **Writers are exactly the three places `AIStateMapper` writes the property**: `adoptUuidIfTrusted` (trusted apply), `graphToJSON`'s lazy generation, and `applySnapshotPreservingNodes`. Each calls the shared `mirrorUuidIntoProcessor` helper immediately after `node->properties.set("uuid", ...)`. Add a fourth writer of the property and it must mirror too, or a Track In node silently stops matching its track.
- **The invariant the lock-free read relies on:** the uuid only ever transitions **empty → value**, and only while the node is not yet audio-visible (all three writers run on a node the caller just created, or under the graph callback lock). It is never rewritten to a different value and never cleared, so an audio-thread reader sees either `""` (and does nothing) or the final value — there is no torn intermediate state to defend against.

### ModulationTarget / ModulationCategory

- `ModulationTarget { juce::String name; int channelIndex; }` — describes a modulatable parameter and the audio-buffer channel that carries its CV signal. Returned by `virtual getModulationTargets()`.
- `ModulationCategory` enum: `Envelope, LFO, Oscillator, Sequencer, Filter, FX, Other`. Returned by `virtual getModulationCategory()`.

### VisualBuffer

Modules may opt in to a thread-safe `VisualBuffer` (circular buffer of `std::atomic<float>`) for scope visualization. Enabled/disabled via `enableVisualBuffer(bool)` and accessed via `getVisualBuffer()`.

### Logical-Port API

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

### isAutoPromotableModTarget

`virtual bool isAutoPromotableModTarget(int dstChannel) const`

Guards poly-mode CV inputs from being auto-wrapped in an `AttenuverterModule` by the AI/routing layer. Default: returns `true` iff `dstChannel` is in `getModulationTargets()`. Modules override this to exclude poly-voice pitch/gate channels which should not receive an attenuverter.

### Extra (non-parameter) State

`virtual juce::var getExtraState() const` / `virtual void setExtraState(const juce::var&)`

For state that has to survive a graph rebuild but is not expressible as a `juce::AudioProcessorParameter` — today only `SamplerModule`'s loaded file path. `AIStateMapper::graphToJSON` writes whatever `getExtraState()` returns as the node's `"state"` property, and `applyJSONToGraph` feeds it back through `setExtraState()`. Return a **void** `var` when there is nothing to persist, so modules that do not use the hook add no JSON.

This matters because preset load — and any undo that has to fall back to a full rebuild — goes through `graphToJSON` → `applyJSONToGraph`, which rebuilds processors from scratch: anything not in that JSON is silently lost. (An ordinary undo restores by diffing and keeps the processor, so it re-applies `"state"` only when it actually changed — see [AppUndoManager](#appundomanager).)

`setExtraState` is only ever called on the **trusted** path (our own snapshots and presets). A module may legitimately read this as a filename, so honouring it for untrusted model output would turn a patch suggestion into an arbitrary file read.

### Port Labels

`virtual juce::String getInputPortLabel(int channelIndex) const` / `getOutputPortLabel(int channelIndex)` — overridden per-module to provide descriptive jack names (e.g. "CV In", "Audio L") shown in the UI.

### Bypass/Mute Contract

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

### Output Level Stage

`ModuleBase` offers an opt-in output-level parameter for modules whose output is audio — `addOutputLevelParameter()` in the ctor, `prepareOutputLevel(sampleRate)` in `prepareToPlay`, `applyOutputLevel(buffer, numAudioChannels)` at the end of the **normal** `processBlock` path. It is a no-op on modules that never opt in.

Two constraints follow from the contract above: `applyOutputLevel` must sit **after** both early returns (a bypassed module passes dry audio through at full level; a muted one is already cleared), and `numAudioChannels` must exclude CV channels. Full rules, including why this is opt-in rather than universal and why it must be the last parameter added, live in [`fx_modules.md § Output Level`](fx_modules.md#output-level-shared-stage).

Related: look parameters up with `findParameterByID(processor, "paramID")` rather than `getParameters()[n]`. Parameter order is not part of a module's contract, and positional lookups silently repoint when a parameter is added. `ModuleBase`'s constructor now adds two parameters of its own — `bypassed` at index 0 and, for a stereo-shaped module, `dualIO` at index 1 ([`fx_modules.md § The toggle is inherited, not registered`](fx_modules.md#the-toggle-is-inherited-not-registered)) — so every module's own parameters start at an index the base owns. Saved state is unaffected either way: both `ModuleBase::getStateInformation` and `AIStateMapper` key parameters by `paramID`.

## Supporting Components

## LayoutUtil

`Source/UI/Layout/LayoutUtil.h/.cpp`

Stateless grid-layout helpers (`snap`, `intersectsAny`, `findFreeSlot`, `computeAutoArrange`). No JUCE GUI dependencies — fully headless-testable. See [`docs/layout/layout.md`](layout/layout.md#layoututil-api) for the full API reference.

## ModuleComponent

`Source/UI/Graph/ModuleComponent/` — one class (declared in `ModuleComponent.h`) split across per-concern
translation units (FRO65), none over 1,000 lines, plus a private `ModuleComponentInternal.h` for
constants/helpers shared by two or more of them. Source layout:

- `ModuleComponent.cpp` — construction/teardown, header/theme helpers, the auto-UI control builder (`createControls`)
- `ModuleComponentEQCard.cpp` — the Parametric EQ card's layout geometry, knob placement, pop-out window, undo-gesture wiring
- `ModuleComponentAudioDrop.cpp` — Sampler control creation and audio-file drag-and-drop for the Sampler and Wavetable
- `ModuleComponentWavetable.cpp` — the Wavetable oscillator's control creation and its tabbed page strip
- `ModuleComponentLayout.cpp` — the generic auto-layout pass (`updateLayout`, `layoutDefaultContent`, macro-port widget sizing)
- `ModuleComponentPaint.cpp` — `paint()`, port geometry/hit-testing, `resized()`'s per-module-type dispatch
- `ModuleComponentInteraction.cpp` — parameter callback reflection, macro/poly/dual-IO state, context menus, mouse handling, title rename

Auto-generates parameter UI from `ModuleBase` metadata using type-safe `ModuleType` switching. Modulation rings read `AudioEngine::getModulationRoutings()`. Uses `setBufferedToImage(true)` and gates its 15 Hz timer repaint so the `GraphEditor`'s 30 Hz connection animation composites cached module images without re-running JUCE text layout every frame.

## AttenuverterModule

`Source/Modules/AttenuverterModule.h`

Intermediary inserted between a modulation source and its destination to scale CV signals. Exposes `lastOutputPeak` / `lastModValue` atomics for UI metering. Constructor default `Amount = 0.0`; set to `1.0` by `addModRouting`, left at `0.0` by `addEmptyModRouting`. See [`docs/modulation.md`](modulation.md) for the full modulation routing model.

## AppUndoManager

`Source/AppUndoManager.h/.cpp`

Snapshot-based undo/redo wrapping `juce::UndoManager`. Structural graph changes (add/remove module, connect/disconnect) are captured as JSON before/after snapshots via `SnapshotAction`. Parameter and position changes have dedicated action types. Safe detach/reattach lifecycle — `setGraphEditor(nullptr)` before graph teardown.

### Restoring a snapshot is a diff, not a rebuild

`SnapshotAction` restores through `AIStateMapper::applySnapshotPreservingNodes`, which compares the target snapshot against the live graph and touches only what differs. It does **not** replay the snapshot through `applyJSONToGraph`, which reaches the same end state by destroying and re-creating every node.

- **Identity is the per-node `uuid`**, not the integer `id` (merge mode renumbers ids). A live node whose uuid appears in the snapshot is **kept** and updated in place — parameters via the trusted param path, position from `"position"`, and `"state"` re-applied through `setExtraState` **only when it changed** (it reloads a sample/wavetable off disk, so an unconditional re-apply would hit the filesystem on every Cmd+Z). A live node whose uuid is absent from the snapshot is removed; a snapshot node with no live match is created, re-adopting both its uuid and its original id when that id is free.
- **A parameter-only undo performs zero topology operations.** No node and no connection is added or removed, so JUCE never rebuilds its render sequence and the audio callback never blocks. Mixed restores batch their topology ops (`UpdateKind::none`) and rebuild exactly once at the end. No graph callback lock is taken at any point.
- **Module runtime state survives.** Sequencer position, envelope stage and sounding voices belong to the processor instance, which is no longer thrown away. This is also what makes hosted plugins and timeline-driven MIDI sources viable: neither can afford to be re-instantiated on every Cmd+Z.
- **Any doubt falls back to the old full rebuild.** `applySnapshotPreservingNodes` plans the whole restore before mutating anything and returns `false` — graph untouched — on a live or snapshot node with no uuid, a duplicate uuid or id, a uuid whose module type no longer matches, an unknown type, a connection naming an undefined node, or a merge delta (`"remove"` / `"removeModulations"`) rather than a full snapshot. `SnapshotAction` then runs `applyJSONToGraph(..., clearExisting=true, trusted=true)`, which is always correct. Correctness beats preservation.
- **UI teardown is now conditional.** The action's `preRestore` hook (`GraphEditor::detachAllModuleComponents`, and the AI service's `aiPatchAboutToApply`) exists to detach UI from processors that are about to be freed, so it fires only when a node is actually being removed — or on the fallback, which frees everything. When nothing is freed there is nothing to detach, and `updateComponents()` (the `postRestore` hook) reconciles additively, so a parameter-only undo no longer destroys and re-creates every `ModuleComponent` either.

### Timeline undo

`TimelineDoc` edits (tracks, clips, notes, automation lanes) go through a separate `TimelineSnapshotAction`, not the graph's `SnapshotAction` — folding timeline JSON into every graph snapshot would inflate the size of every existing undo step (a parameter tweak, a module drag) that never touches the timeline at all. Unlike `SnapshotAction`'s diffing restore, `TimelineSnapshotAction::perform()`/`undo()` call `TimelineDoc::fromVar` directly: `fromVar` is already the doc's own all-or-nothing load path, so there is nothing to diff against, and a `var` this action holds always came from this doc's own `toVar()`, so `fromVar` failing would mean the doc's round-trip contract itself broke (`jassert`ed, not silently swallowed). Both action types push onto the **same** `juce::UndoManager`, so the app has one undo stack and Cmd+Z stays chronological whichever domain each step came from.

- `recordTimelineChange(doc, mutation)` snapshots `toVar()` before and after the mutation; if the two serialisations are identical (the doc rejected the edit, or it was a genuine no-op) nothing is pushed and it returns `false` — a no-op must not create an undo step.
- `recordCombinedChange(graph, doc, mutation)` is for edits that touch both domains in one gesture — the canonical case is deleting a module a timeline lane is bound to. It opens ONE transaction, captures graph + timeline "before", runs the single mutation, then pushes a graph `SnapshotAction` and/or a `TimelineSnapshotAction` — only for whichever domain(s) actually changed — inside that same transaction, so one `undo()`/`redo()` reverts or re-applies both together, never half the edit. It reuses the exact same pre/post-restore lambda plumbing (`detachAllModuleComponents` / `updateComponents`) `recordStructuralChange` gives the graph half, factored into a private `createGraphSnapshotAction` helper rather than duplicated.
- `recordGraphAndMacroChange(graph, macros, mutation)` is `recordCombinedChange`'s twin for the graph + `synth::MacroSet` pair instead of graph + timeline (see [`docs/macros.md`](macros.md) for its callers). A graph+macro push (`GraphAndMacroSnapshotAction` when both changed, else the lone graph or macro action) is factored into a private `pushGraphAndMacroActions` helper.
- `recordGraphTimelineAndMacroChange(graph, doc, macros, mutation)` (T173a) covers all THREE domains in one gesture — today's one caller is `MainComponent::addAudioTrack`, which creates a graph channel, a timeline track and a macro grouping it in a single "+ Track -> Audio Track" click. Same shape as the other two: one transaction, all three "before" states captured up front, the mutation runs once, all three "after" states captured, then the graph+macro half is pushed through the SAME `pushGraphAndMacroActions` helper `recordGraphAndMacroChange` uses (so a graph+macro combination that needs `GraphAndMacroSnapshotAction` gets it here too), followed by a `TimelineSnapshotAction` if the timeline changed — no `beginNewTransaction` between the two pushes, so one `undo()`/`redo()` covers whichever of the three domains actually changed.
- **Lifetime rule, extended:** exactly like `SnapshotAction` holding the graph, a pushed `TimelineSnapshotAction` holds a reference to the `TimelineDoc` — the doc must outlive the `AppUndoManager`, or `clearUndoHistory()` must run before the doc is destroyed. `MainComponent` satisfies this by declaration order (see §8 above).

### Restore hooks

`setRestoreHooks(beforeRestore, afterRestore)` installs one pair of callbacks fired around **every** restore this manager performs — the graph's `SnapshotAction` and the timeline's `TimelineSnapshotAction` alike, on undo and on redo. They are deliberately *not* the same thing as `SnapshotAction`'s existing `preRestore`/`postRestore` pair, which is the GraphEditor's component lifecycle and whose "pre" half fires **lazily** (only when a node is actually being freed). These always fire, which is what the two current users need:

- `beforeRestore` opens an `AutomationRecorder::ScopedProgrammaticApply`. A parameter-only undo frees nothing — and is exactly the case that writes parameter values — so hanging the guard off the lazy hook would miss it entirely.
- `afterRestore` re-runs the timeline's binding reconciliation and publish. A **graph** restore can strand a track/lane binding; a **timeline** restore comes back out of `TimelineDoc::fromVar` with every `orphaned` flag reset to `false` (it is runtime-derived state, never serialised). Both need the same pass, and a combined step performs both restores, so the hook fires once per restore rather than once per transaction.

Actions capture the manager, not the callbacks, so hooks installed after an action was pushed still apply to it.

## AppLookAndFeel + ThemeManager

Central `LookAndFeel_V4` subclass and theme registry. Owns all stock-widget re-skins, treatment draw helpers, and the SVG `IconLibrary`. See [`docs/layout/theming.md`](layout/theming.md) for the full token reference, and [`docs/layout/theme-authoring.md`](layout/theme-authoring.md) for the JSON schema.

---
