# Timeline Automation Strip

A horizontal strip docked at the BOTTOM of the lanes region (`gridLanesBounds_`), toggled open by
selecting a lane — from the lane picker inside the strip itself, from a track header's `A` button
([tracks](tracks.md#the-automation-button)), or from ANY generic auto-UI knob's right-click menu.

While open it takes exactly `Metrics::timelineAutomationStripHeight` (72, code-only) off the bottom
of `gridLanesBounds_`, so `TimelineClipLaneArea` / `PianoRollComponent` — and the playhead overlay,
trimmed the same amount — shrink by that much. Never the other way around, and the ruler and
track-header column are untouched.

## Strip chrome

`TimelinePanelComponent`'s own members, laid out in `resized()`: a header row above
`synth::ui::AutomationLaneEditor`, the curve canvas. The header carries

- four tool `juce::TextButton`s (glyphs `P` / `✎` / `╱` / `⌫`, radio-grouped so exactly one is
  down; `kAutomationToolButtonWidth` is 28 px),
- a lane-picker `juce::ComboBox` — every doc lane, labelled `"NodeName · Parameter name"` via
  `TrackHeaderHost::getNodeDisplayName(lane.nodeUuid)` (falling back to the uuid's first 8
  characters when it does not resolve) and `TrackHeaderHost::getParameterDisplayName` (falling
  back to the raw `paramId`; a hosted plugin's paramIds are opaque, e.g. a VST3's are numbers).
  That is the SAME interface the track-header binding chip uses, so no second graph-aware seam
  was added.
- a record-mode `juce::ComboBox` (Off/Read/Touch/Latch/Write, 1-based combo id = `LaneRecordMode` +
  1) bound to `TimelineDoc::setLaneRecordMode` through `AppUndoManager::recordTimelineChange` — a
  manual selector change IS a user gesture, unlike `AutomationRecorder`'s own programmatic
  Write-drops-to-Touch-on-stop call; see that setter's header comment.
- a close `✕` button.

Panel API: `showAutomationLane(LaneId)` / `closeAutomationStrip()` / `isAutomationStripVisible()`.
Headless hooks: `applyAutomationLaneMenuChoice(int)` / `applyAutomationRecordModeChoice(int)`,
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

Right-click a SEGMENT shows Hold/Linear, ticking the current one, routed through the headless
`applySegmentCurveChoice(beat, curve)` hook. Right-click a HANDLE shows `{Delete point}`.

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
`createTrackInNode()` and `AIStateMapper` use at every uuid writer site), finds the first
`TrackKind::Automation` track or creates one, binds a lane with the parameter's real
`NormalisableRange` (`addLane` dedupes doc-wide — a repeat call for an already-automated parameter
is a no-op that returns the existing lane), opens the timeline panel via the SAME toggle-button
click path `simulateToggleTimelineClick()` uses if it is hidden, and opens the strip on that lane.

See [`modulation.md`](../modules/modulation.md) for the user-facing description of the right-click route.

## Tests

`Tests/UI/Timeline/AutomationEditorTests.cpp` — `AutomationLaneEditor` gesture and
publish-discipline coverage (mirroring the `TimelineClipLaneArea` / `PianoRollComponent`
hand-built-`juce::MouseEvent` idiom against a bare `TimelineDoc` + `AppUndoManager`), the panel's
strip open/close and record-mode selector, and a `MainComponent` integration test for the knob
entry point.
