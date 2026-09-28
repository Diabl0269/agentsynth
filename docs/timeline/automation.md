# Timeline Automation Strip

A horizontal strip docked at the BOTTOM of the lanes region (`gridLanesBounds_`) for the **global**
lanes — those on Automation-kind tracks, i.e. automation of modules no single track owns. A
track-owned lane is edited in its own row under its track instead
([track-automation](track-automation.md)). Toggled open by selecting a lane — from the lane picker
inside the strip itself, from an Automation track header's `A` button
([tracks](tracks.md#the-automation-button)), from the toolbar's global-automation button, or from a
knob's right-click menu when the knob's module is global.

While open it takes exactly `Metrics::timelineAutomationStripHeight` (72, code-only) off the bottom
of `gridLanesBounds_`, so `TimelineClipLaneArea` / `PianoRollComponent` — and the playhead overlay,
trimmed the same amount — shrink by that much. Never the other way around, and the ruler and
track-header column are untouched.

## Strip chrome

`TimelinePanelComponent`'s own members, laid out in `resized()`: a header row above
`synth::ui::AutomationLaneEditor`, the curve canvas. The header carries

- five tool `juce::TextButton`s (glyphs `P` / `✎` / `╱` / `⌫` / `~`, radio-grouped so exactly one is
  down; `kAutomationToolButtonWidth` is 28 px) — the fifth, Shape, shows a popup of
  `synth::ShapeKind` names instead of toggling on click (see [Tools](#tools) below),
- a lane-picker `juce::ComboBox` — every GLOBAL lane (on an Automation-kind track), labelled
  `"NodeName · Parameter name"` (`automationLaneLabel`, shared with the lane-row headers) via
  `TrackHeaderHost::getNodeDisplayName(lane.nodeUuid)` (falling back to the uuid's first 8
  characters when it does not resolve) and `TrackHeaderHost::getParameterDisplayName` (falling
  back to the raw `paramId`; a hosted plugin's paramIds are opaque, e.g. a VST3's are numbers).
  That is the SAME interface the track-header binding chip uses, so no second graph-aware seam
  was added. After every existing lane, the combo lists "Add lane..." entries from
  `TrackHeaderHost::getAvailablePluginLaneOptions()` — a hosted plugin's not-yet-automated instance
  parameters, and (FRO292) every ACTIVE, not-yet-automated `ChannelStripModule` send slot, labelled
  "Send to \<target\>" ([modulation.md](../modules/modulation.md#hosted-plugin-parameters-as-automation-lanes)) —
  choosing one calls `addPluginAutomationLane` (find-or-create through the placement seam) and
  reveals the result (`revealAutomationLane`: under its track when a track owns the module,
  otherwise here).
- a record-mode `juce::ComboBox` (Off/Read/Touch/Latch/Write, 1-based combo id = `LaneRecordMode` +
  1) bound to `TimelineDoc::setLaneRecordMode` through `AppUndoManager::recordTimelineChange` — a
  manual selector change IS a user gesture, unlike `AutomationRecorder`'s own programmatic
  Write-drops-to-Touch-on-stop call; see that setter's header comment.
- a close `✕` button.

Panel API: `showAutomationLane(LaneId)` / `closeAutomationStrip()` / `isAutomationStripVisible()` /
`toggleGlobalAutomationStrip()` (the toolbar button: closes an open strip, else opens it on a global
lane, else opens it EMPTY — no lane selected — on its "Add lane..." picker; an empty strip stays
open across doc changes, a strip whose shown lane was removed closes).
Headless hooks: `applyAutomationLaneMenuChoice(int)` / `applyAutomationRecordModeChoice(int)` /
`applyShapeToolChoice(int)` (menu id = 1 + the `synth::ShapeKind` enum value, Sine=1..Random=6),
since a `juce::PopupMenu` or `ComboBox` never runs in a test — the same headless-hook idiom every
other timeline sub-component's context menu follows.

The lane picker deliberately re-runs `collectAutomationLaneOptions` at click time rather than
resolving against a build-time snapshot: an automation lane is document data mutated only on the
message thread, so that is safe. Contrast the add-track Plugin submenu
([add-track](add-track.md#menu-options-resolve-against-a-build-time-snapshot)), whose backing list
a background thread mutates.

## The curve canvas

`Source/UI/Timeline/AutomationLaneEditor.h/.cpp` (`synth::ui::AutomationLaneEditor`) edits ONE
`synth::AutomationLane` at a time.

X is the SAME shared `TimelineViewState` the clip lanes use, so it lines up with the playhead
pixel-for-pixel; the piano roll is the one surface that maps beats through its own zoom and scroll
instead ([piano-roll](piano-roll.md#horizontal-mapping)). Y maps the lane's own `RangeSnapshot
[min..max]` linearly onto the component's height, top = max (`valueToY` / `yToValue`).

The curve is sampled every ~2 px by building a local `TimelineSnapshot::Point[]` from the lane's
breakpoints and calling `AutomationKernel::evaluate` with a fresh `AutomationCursor`.

**Why it is re-derived on every repaint rather than cached.** Paint is not hot, and this is the
SAME evaluator the audio thread uses, so the canvas can never show a shape real playback would not
produce.

## Tools

`AutomationLaneEditor::Tool`, set by the strip's header buttons:

| Tool | Gesture |
|---|---|
| Pointer | Drag a HANDLE moves it — beat snapped via the shared view-state snap, value clamped to the lane's range; tension and curve carry over untouched. Drag a SEGMENT (not a handle — hit-tested first) scrubs the segment's LEFT point's tension, ±0.01 per vertical pixel, clamped to `[-1, 1]`, following `AutomationKernel`'s own "shape comes from the LEFT point" contract. Double-click empty space adds a point at that (beat, value), Linear, tension 0 |
| Pencil | Freehand drag collects raw (beat, value) samples — no snapping, that is the point of freehand. On mouse-up they are thinned by `synth::AutomationRecorder::thinPoints` (reused, not re-implemented — its RDP helper is `public static` precisely so a second caller can reach it) at the SAME `kThinningEpsilonFraction` scaled to the lane's own range, and replace whatever existed inside the dragged beat span |
| Line | Drag previews a straight line from press to release; mouse-up replaces the dragged span with exactly the two snapped endpoints, Linear |
| Eraser | Drag removes every handle it touches — collected into a set as the pointer passes over them (dimmed in the preview), deleted on mouse-up |
| Shape | Press sets the start beat and one extreme value; drag sets the end beat (horizontal) and the other extreme (vertical). Period is the shared view-state's current snap division (`divisionBeatsRaw`; 1 beat when snap is Off). Previewed live via `synth::generateAutomationShape` (see [Shape generation](#shape-generation) below); mouse-up replaces every point inside the dragged span with the generated run, in ONE `editBreakpoints` call |

Right-click a SEGMENT shows Hold/Linear, ticking the current one, routed through the headless
`applySegmentCurveChoice(beat, curve)` hook. Right-click a HANDLE shows `{Delete point}`.

## Shape generation

`Source/Timeline/AutomationShapes.h` (`synth::generateAutomationShape`, Core target — pure math, no
UI dependency) is the Shape tool's waveform generator, and the one place `synth::ShapeKind` (Sine,
Triangle, Square, SawUp, SawDown, Random) is defined:

```cpp
std::vector<AutomationLane::Breakpoint> generateAutomationShape(
    ShapeKind kind, double startBeat, double endBeat, double periodBeats,
    double lowValue, double highValue, double phase = 0.0, std::uint32_t randomSeed = 1);
```

- **Sine** is densely sampled (16 `Linear` points per period) since `AutomationKernel` has no sine
  evaluator; **Triangle** places one `Linear` point per peak/trough (the kernel's own interpolation
  IS the ramp between them); **Square** and **Random** (deterministic sample-and-hold, keyed off
  `randomSeed`) place `Hold` points at each transition; **SawUp**/**SawDown** are a `Linear` ramp per
  cycle plus an instantaneous reset offset by a tiny epsilon beat (`1/960`), since a lane's beats
  must be unique. Never emits `BreakpointCurve::Bezier` (reserved, no evaluator).
- `phase` (radians) picks which extreme the shape starts at — 0 starts low, `PI` starts high — which
  is how `AutomationLaneEditor::buildShapePoints()` encodes "which of the press/drag values came
  first"; Random ignores it.
- Bounded by `TimelineDoc::kMaxBreakpointsPerLane` always: a period tiny next to the dragged span
  coarsens (a larger effective period) rather than growing the point count without limit. The LAST
  point's beat is always exactly `endBeat`, so the drawn shape ends there cleanly.
- `periodBeats <= 0` (or non-finite) is treated as one cycle spanning the whole span; `endBeat <=
  startBeat` returns an empty run.

`AutomationLaneEditor::setTool(Tool::Shape)` + `setShapeKind(ShapeKind)` select it; the strip's `~`
button's popup is the only current caller (`TimelinePanelComponent::applyShapeToolChoice`).

Escape clears in-flight tool-drag state and returns `true`; when idle it returns `false` so the key
falls through to `TimelinePanelComponent`'s own `keyPressed`, which closes the strip — the same
ancestor-chain fallthrough `TimelineClipLaneArea` / `PianoRollComponent`'s panel-scoped Delete and
Escape rely on, one level further up.

## One gesture, one mutation

Every preview above is strictly component-local (a handful of `preview*_` members, read back by
`paint()`) and NEVER touches the doc during `mouseDrag` — commit happens exactly once, on mouse-up.

The subtlety: `TimelineDoc`'s own single-point mutators (`addBreakpoint` / `removeBreakpoint`) each
bump the revision counter independently, so a gesture that touches several points — Pencil's
thin-and-replace, Line's remove-span-then-add-two-endpoints, a Pointer move that lands on a
different beat, Eraser's multi-point sweep — calling them in a loop would cost one revision bump,
i.e. one audio-thread republish, PER POINT instead of per gesture.

`TimelineDoc::editBreakpoints(laneId, removeBeats, addPoints)` is the batched primitive that fixes
it: it removes every existing point at a beat in `removeBeats`, then inserts every point in
`addPoints` (validated and clamped exactly like `addBreakpoint`), as ONE `applyMutation` call
however many points move either way. Every multi-point gesture routes through it; only the
genuinely single-point ones — tension scrub, curve toggle, double-click-add, record-mode select —
still call a plain single mutator, because those already cost exactly one bump on their own.

## The knob entry point

`ModuleComponent`'s generic auto-UI slider branches (`createControls()`'s float and int cases)
attach `this` as a `MouseListener` on the slider (`addMouseListener(this, false)` — safe because
`this` outlives every child slider, both being torn down together in `~ModuleComponent()`).

`ModuleComponent::mouseDown` checks `e.eventComponent != this` FIRST — a hit on a child fires the
SAME override, in the CHILD's local coordinate space, which the body-click geometry further down
must never see — and, on a right-click, shows `"Automate '<Param>'"` via a
`juce::Component::SafePointer<ModuleComponent>` (the popup's callback is async, so the module can
be gone by the time it fires) that calls `owner.onAutomateParameterRequested(nodeId, paramId)`. That
is a `GraphEditor` host seam mirroring `onSaveSnippetRequested` exactly: `GraphEditor` owns no
`TimelineDoc`, so it hands the pair back to the one component that owns both the doc and the graph.

`MainComponent::automateParameter(nodeId, paramId)` — public, and also the test's headless hook —
resolves the node's uuid (ensure-uuid, mirrored into the processor, the same idiom
`createTrackInNode()` and `AIStateMapper` use at every uuid writer site), picks the lane's track
through the placement seam `synth::findOrCreateLaneHostTrack` (the track that owns the module, else
the first `TrackKind::Automation` track, created when missing —
[track-automation](track-automation.md#the-placement-seam)), binds a lane with the parameter's real
`NormalisableRange` (`addLane` dedupes doc-wide — a repeat call for an already-automated parameter
is a no-op that returns the existing lane, wherever it lives), opens the timeline panel via the SAME
toggle-button click path `simulateToggleTimelineClick()` uses if it is hidden, and reveals the lane
(`TimelinePanelComponent::revealAutomationLane`): a track-owned lane expands its track, scrolls its
row into view and focuses it; a global lane opens the strip on it. A hosted-plugin card knob goes
through `addPluginAutomationLane`, which ends in the same reveal.

See [`modulation.md`](../modules/modulation.md) for the user-facing description of the right-click route.

## A lane follows its send through a reorder (FRO296)

`TimelineDoc::swapLaneParams(nodeUuid, paramA, paramB)` retargets whichever of the two `(nodeUuid,
paramId)` lanes exists to the OTHER paramId, in place — a rebind, never a delete+recreate, so a
lane's points and record mode survive untouched. It exists specifically for a mixer send-slot swap
(`synth::swapSends`/`synth::moveSendRow`,
[`docs/mixer/sends-and-buses.md#slots-are-sparse`](../mixer/sends-and-buses.md#slots-are-sparse)):
`MixerPanelComponent::moveSendRow` — the one place that owns the graph, the `TimelineDoc` AND the
macros together — replays the exact `(slotA, slotB)` sequence `synth::moveSendRow` applied to the
graph, calling `swapLaneParams` once for `sendALevel`/`sendBLevel` and once for
`sendAPan`/`sendBPan` per swap, all inside the SAME
`AppUndoManager::recordGraphTimelineAndMacroChange` transaction as the slot swap itself. A lane on
neither side of a given pair is left alone (no lane is created); a lane on exactly one side just
gets the other's paramId, same as `rebindLane` (the nodeUuid half of this shape) leaving an unbound
side untouched.

This is a NEW, narrow case rather than a use of `rebindLane`: `rebindLane` only ever changes a
lane's `nodeUuid`, and the doc-wide one-lane-per-parameter invariant it enforces (reject if some
OTHER lane already owns the target identity) would make a true SWAP between two lanes on the SAME
node impossible to express as two sequential `rebindLane` calls — the second call would always
collide with the first's own new identity. `swapLaneParams` mutates both sides in one
`applyMutation`, so the invariant never sees an intermediate, colliding state.

## Tests

Track lane rows, lane placement and "automation follows clips" have their own list in
[track-automation](track-automation.md#tests).

`Tests/UI/Timeline/AutomationEditorTests.cpp` — `AutomationLaneEditor` gesture and
publish-discipline coverage (mirroring the `TimelineClipLaneArea` / `PianoRollComponent`
hand-built-`juce::MouseEvent` idiom against a bare `TimelineDoc` + `AppUndoManager`, including the
Shape tool's one-mutation-per-drag/snap-period/replaces-inside-span/Escape-cancels coverage), the
panel's strip open/close, record-mode selector and Shape-tool popup headless hook
(`applyShapeToolChoice`), and a `MainComponent` integration test for the knob entry point.

`Tests/Timeline/AutomationShapesTests.cpp` — `synth::generateAutomationShape` in isolation: point
counts and the `kMaxBreakpointsPerLane` cap/coarsen under a huge-span/tiny-period stress case for
every `ShapeKind`, extremes clamped within range, unique sorted beats, the forced
exactly-`endBeat` landmark, period honoured (checked by evaluating generated points through the SAME
`AutomationKernel::evaluate` the audio thread and curve canvas use, at quarter-period beats),
Random's seed-determinism, and the zero/negative-span and zero/negative-period edge cases.
