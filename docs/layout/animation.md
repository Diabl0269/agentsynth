# Animation System

`Source/UI/Layout/UIAnimation.h` (namespace `synth::ui`) holds the shared animation infrastructure.
All motion in the app uses it. The `juce_animation` module is linked into both the `Core` and the
`AgentSynth` app targets. What is allowed to repaint at all is [rendering](rendering.md).

## Easing functions

Pure, stateless helpers, no component state required:

| Function | Signature | Curve |
|---|---|---|
| `easeOutCubic` | `float easeOutCubic(float t)` | Fast-out deceleration |
| `easeInCubic` | `float easeInCubic(float t)` | Slow start, accelerating away (leaving, returning) |
| `easeInOutCubic` | `float easeInOutCubic(float t)` | Smooth in and out |
| `easeOutBack` | `float easeOutBack(float t)` | Overshoots slightly, then settles |

All take `t` in `[0, 1]` and return a mapped `[0, 1]` value, which may exceed 1 briefly for
`easeOutBack`.

## AnimationDriver

A VBlank-driven, **time-bounded** tween: it animates a progress value from `0` to `1` over a
caller-specified duration, then auto-stops. It never ticks beyond `t = 1.0`.

```cpp
// Members in the owning component:
juce::VBlankAnimatorUpdater vblankUpdater_{ this };
synth::ui::AnimationDriver driver_;

// Start a 200 ms transition:
driver_.start(0.20);   // seconds

// In timerCallback / VBlank callback:
float t = driver_.getValue();
auto bounds = AnimationDriver::lerpBounds(startRect, endRect, t);
setBounds(bounds);
if (!driver_.isRunning())
    ; // settled — no more repaints fired
```

- Callers must hold both a `juce::VBlankAnimatorUpdater` and an `AnimationDriver` as members; the
  VBlank updater keeps the driver alive and ticking.
- `isRunning()` returns `false` once `t` reaches `1.0` — the driver stops itself and fires no
  further repaints.
- `AnimationDriver::lerpBounds(from, to, t)` is a static helper interpolating a
  `juce::Rectangle<int>` linearly between two positions.

## PanelSlide

A sliding panel does **not** animate by tweening its `Rectangle`. It owns a
`synth::ui::PanelSlide` — a `[0..1]` open fraction plus the from/to snapshot of the tween moving it
— and the owner's `resized()` derives the panel's *size* from that fraction:

```cpp
// Members:
synth::ui::PanelSlide librarySlide_, aiPanelSlide_, timelineSlide_;
synth::ui::AnimationDriver panelSlideAnim_;          // ONE driver for all three
juce::VBlankAnimatorUpdater vblankUpdater{ this };

void resized() override {
    const int libW = librarySlide_.sizeBetween(collapsedW, fullW);   // fraction -> px
    const int aiW  = aiPanelSlide_.sizeBetween(0, aiPanelWidth);
    ...                                                              // carve, then setBounds
}
```

Two properties follow, and both are the point:

- **A layout pass is correct whenever it runs.** A window resize, a theme change, a timeline height
  drag or a `setSize()` *in the middle of a slide* re-derives the same proportions instead of
  snapping the panel to an endpoint. `resized()` is the single geometry authority — there is no
  second "compute the target bounds" function to keep in step with it.
- **A toggle only moves the fraction.** It flips the logical flag, persists it, refreshes the
  toolbar and calls one seam. The alternative shape — flip the flag, call `resized()` (which snapped
  every panel to its FINAL bounds), *then* start a tween from the pre-toggle bounds — is what made a
  panel appear fully open for one frame and yank back before easing. That jump is a property of the
  call order, not of the easing.

`MainComponent::beginPanelSlide()` is that seam, and the shape to copy:

```cpp
void MainComponent::beginPanelSlide() {
    if (isLibraryVisible) moduleLibrary.setVisible(true);      // opening: visible before frame 0
    ...                                                        // closing: hidden in finish...()

    const bool canAnimate = isShowing();                       // no VBlank reaches an off-screen
    const bool a = librarySlide_ .retarget(isLibraryVisible  ? 1.0f : 0.0f, canAnimate);
    const bool b = aiPanelSlide_ .retarget(isAiPanelVisible  ? 1.0f : 0.0f, canAnimate);
    const bool c = timelineSlide_.retarget(isBottomDockVisible ? 1.0f : 0.0f, canAnimate);
    if (! (a || b || c)) { finishPanelSlide(); return; }        // landed synchronously

    resized();                                                 // frame 0, before the first VBlank
    panelSlideAnim_.start(vblankUpdater, kPanelSlideMs, synth::ui::easeInOutCubic,
                         [this](float t) { applyPanelSlideFrame(t); },
                         [this]          { finishPanelSlide(); });
}
```

Four rules live in there:

1. **`retarget()` starts from the fraction's CURRENT value.** Re-toggling mid-flight reverses from
   where the panel *is*, never from an extreme — the same contract
   `PianoRollComponent::setScalePanelVisible` states for the scale panel's slide.
2. **`canAnimate == false` lands immediately.** No VBlank reaches an off-screen component, so a
   headless toggle — and a persisted restore before the window exists, where `initialiseCommon()`
   snaps the fractions rather than sliding them — must be synchronous. That is the contract
   `Tests/UI/Layout/PanelAnimationAndLoadingTests.cpp` asserts with no message pump at all, and the
   reason the whole test suite sees final bounds the instant a `simulate*Click()` returns.
3. **Every slide is retargeted, not just the one whose flag moved.** The three panels share one
   `AnimationDriver` — they share a window, so they must share a clock — and restarting it has to
   carry any slide already in flight to *its* target. One animator per panel would leave whichever
   slide the next toggle did not mention frozen half-open.
4. **A close is a slide too.** The panel is hidden in `finishPanelSlide()`, not at toggle time;
   hiding it up front is what makes "close" not an animation at all.

`applyPanelSlideFrame(t)` advances all three fractions and calls `resized()`. It adds **no**
`repaint()` — moving a child's bounds already invalidates the region it left and the one it arrived
at, and a full-window repaint per frame is exactly what [rendering](rendering.md) forbids.
`finishPanelSlide()` stops the driver, `finish()`es each fraction on its exact endpoint (a driver's
last frame is not guaranteed to be `t == 1`), hides whatever finished closing, and lays out once
more.

`PanelSlide` holds no animator and no JUCE GUI state, so its math is unit-tested headlessly
(`Tests/UI/Layout/UIAnimationTests.cpp`) and it is reusable by any multi-panel surface.
`GraphEditor::toggleModMatrixVisibility` and `PianoRollComponent::setScalePanelVisible` implement
the same pattern by hand and are deliberately **not** ported to it: they are single-panel surfaces
and correct as they stand.

**The Mixer's Own panel is a fourth slide that does NOT share `MainComponent`'s driver** (FRO231).
`MixerPlacementController` owns its own `PanelSlide`, `AnimationDriver` and
`VBlankAnimatorUpdater` (190 ms, `easeInOutCubic`, `canAnimate` = the parent is showing) and follows
the same rules: opening is visible before the first frame, closing hides only in its finish, a
mid-flight reversal starts from the current fraction. It never touches the three `MainComponent`
fractions; each frame it calls `onLayoutNeeded` (wired to `MainComponent::resized()`), which reads
`getCarveHeight()`. Choosing the Own-panel placement (or launching in it) snaps open with no tween.

## Reorder drag

Every reorderable list (the bottom dock's tab strip, the mixer's track columns, the mixer's zones rows and send rows, the timeline's track list, the macro port dialog's rows and the plugin knob picker's rows) uses one behaviour, in `Source/UI/Layout/ReorderDrag/`:

- `ReorderDragAnimator` — pure logic along ONE axis (x for a strip, y for a list), no components and
  no painting, with an injectable clock so it is unit-tested headlessly
  (`Tests/UI/Layout/ReorderDrag/ReorderDragAnimatorTests.cpp`). Inputs: each item's start and extent,
  the dragged index, the grab offset and the pointer. Outputs: the insertion index, the dragged
  item's position, and every other item's animated position.
- `ReorderFramePump` — the VBlank glue (`VBlankAnimatorUpdater` + `AnimationDriver`): frames run only
  while a tween is in flight, then stop. A held drag with the pointer at rest, and a settled list,
  cost no timer and no repaint.
- `ReorderCancelKey` — Esc during the gesture (a mouse press does not move keyboard focus, so it
  listens on the top-level window for exactly the length of the drag).
- `ReorderDragSession` — the three above wired together for a list that owns one animator: the frame
  pump started only when the tween generation changes, the Esc listener armed at the first lifted
  drag event, the drag auto-repeat, and the "Esc already cancelled, so the release must not commit"
  flag. `end()` tells the release apart (`Click` / `Commit` / `Cancelled` / `Nothing`). The macro
  port dialog, the knob picker and the mixer send list use it; the older owners above hold the
  pieces themselves.
- `ReorderLiftLook.h` — `paintReorderLift()`, the lifted row's look (soft shadow, raised
  `surfaceHi` fill, 1 px `accent` border, all scaled by the animator's lift). Zones rows, the
  port dialog, the knob picker and the send list all call it.

The rules it implements:

1. **The owner captures the grab once.** At press it hands over the items' extents, the pointer and
   the grab offset (pointer minus the item's start), all measured in the OWNER's coordinates.
   Never re-derive the offset from `MouseEvent::getMouseDownPosition()`: JUCE recomputes it in the
   event component's *current* local space, and the pressed component moves as neighbours shift, so
   the lifted item would jump by a slot per change.
2. **Threshold.** Nothing happens until the pointer has travelled 4 px along the axis, so a click keeps
   working; `dragTo()` returns false until then.
3. **The dragged item stays under the grab point** (`pointer - grab offset`, held inside the strip) and
   is drawn lifted; its slot shows a dashed outline that glides to the insertion gap.
4. **Insertion by centre.** The insertion index counts the neighbours whose midpoint the dragged
   item's centre has passed. Neighbour midpoints never move during the drag, so unequal extents
   cannot ping-pong; pressed against either end of the strip the item takes that end.
5. **Neighbours make room.** When the insertion index changes each affected item retargets from its
   CURRENT animated position (never from rest) over 160 ms, `easeOutCubic`.
6. **The order is committed on release, not during the drag.** The owner reads `getNewOrder()`,
   commits it through its normal persistence path, lays out statically, and passes the final starts
   to `release()`; the dragged item then settles from its drop position (140 ms, `easeOutCubic`) and
   every neighbour finishes from wherever its glide had got to. A list that rebuilds its rows on
   commit releases the animator BEFORE sending the commit and matches the new rows to the old keys
   by identity, so the settle survives the rebuild.
7. **Esc cancels.** `abort()` sends the dragged item back to where it was picked up (140 ms,
   `easeInCubic`) and the neighbours back to their places (160 ms); nothing is committed and no
   undo step is created.
8. **Off-screen owners land instantly** (`animate == false`): no VBlank reaches a component that is
   not showing, so a headless drag needs no message pump.

Owners: `BottomDockComponent` (`beginTabDrag`/`dragTab`/`commitTabDrag`, [tab strip](../mixer/panel.md#the-tab-strip))
`MixerPanelComponent` (`MixerPanelColumnDrag.cpp`, [reordering columns](../mixer/panel.md#reordering-columns))
`MixerZonesPane` (`MixerZonesPaneDrag.cpp`, [Zones pane](../mixer/panel.md#side-pane-zones-and-visibility), vertical axis)
and `TimelinePanelComponent` (`TimelinePanelTrackDrag.cpp`, [drag to reorder](../timeline/tracks.md#drag-to-reorder), vertical axis).

Owners on `ReorderDragSession`, all vertical: `MixerSendList` (`MixerSendList.cpp`, a send row is dragged by its target
name; the rebuild its commit causes gets the settle handed over via `settleInto`, [reordering sends](../mixer/sends-and-buses.md#reordering-sends)), `MacroPortConfigDialog`
(`MacroPortConfigDialogRowOrdering.cpp`, [renaming and reordering ports](../macros/configure-io.md#renaming-and-reordering-ports)),
and `PluginKnobPickerComponent` (`PluginKnobPickerComponentDrag.cpp`,
[choosing knobs](../control/plugin-card-layout.md#choosing-knobs-as-built-fro132)). The pointer is converted into the list's own
coordinates on every event, and the commit goes out once, on release.

### Drag-and-drop cursor

Every place the user drags an item to move, reorder or drop it shows the grabbing-hand cursor,
consistently, through one header-only helper: `Source/UI/Layout/DragCursor.h`
(`synth::ui::dragGrabCursor()`, `dragCopyCursor()`, `showDragCursor(component, copying)`,
`endDragCursor(component, rest)`). No site sets `DraggingHandCursor` or `CopyingCursor` by hand.

- **Dedicated grab handles** (a thing whose only job is to be picked up: bottom-dock tab, timeline
  track header background, mixer column header, mixer zones row, list drag handles, MIDI-remote
  controller surface cell, module library row, timeline ruler marker flag) show
  `juce::MouseCursor::DraggingHandCursor` on hover AND during the drag. A mixer column header
  shows it only while it has reorder hooks set (Direct, Master and pinned columns stay normal); its name
  label and the column's source line delegate their cursor to the header (`CursorDelegatingLabel`), so
  the whole top area reads as the handle.
- **Tool-driven canvases** (timeline clips, piano roll notes, module cards, macro cards) keep the
  tool cursor on hover (`ToolCursors.h` / `applyToolCursor` stay in charge). The grab cursor shows
  only once a move drag has actually started (past its threshold), the copy cursor shows while an
  Option/Alt copy-drag is active (toggling Option mid-drag switches between copy and grab), and
  the tool or normal cursor returns on release or Esc.
- **Out of scope:** cable drags between ports, value drags (knobs, faders, sliders, curve points),
  resize handles, marquee selection, pan/scroll, and OS file drops.

## Motion rules

Every animation in the app follows these; a new one that cannot is a design question, not a
shortcut.

- **Durations.** Hover state 80–120 ms. A small reveal or tooltip 160 ms in, 110 ms out. Reorder
  make-room 160 ms, settle 140 ms. Panel slides are unchanged (190 ms, `easeInOutCubic`).
- **Easing.** `easeOutCubic` for anything arriving or moving into place; `easeInCubic` for anything
  leaving or being sent back. `easeInOutCubic` stays for the panel slides, where a thing both starts
  and stops at rest.
- **Origin.** A thing appears out of the element that caused it and returns into it; the direction
  follows where it sits (a popover under a button grows down from it, a bottom panel rises from its
  edge).
- **Reorder.** The dragged item stays under the grab point, its neighbours make room, and nothing
  teleports. One shared helper (`ReorderDragAnimator`) serves every reorderable list — never a
  second implementation per list. See [Reorder drag](#reorder-drag).
- **Drag and drop.** Every drag-to-move shows the grab cursor (copy cursor for a copy), the moving
  item or a ghost of it follows the pointer, and a reorderable list makes room with
  `ReorderDragAnimator` — see [Drag-and-drop cursor](#drag-and-drop-cursor) and
  [Reorder drag](#reorder-drag).
- **Interruption.** A retargeted animation starts from the CURRENT value, never from its start: a
  re-toggle, a second insertion change or a drop mid-glide continues from where the thing is.
- **Time-bounded.** Nothing repaints once it has settled: frames run for a finite duration and
  stop (see [the time-bounded animation rule](#the-time-bounded-animation-rule)).
- **Drag-copy (Option-drag, used by the piano roll).** The original stays put at full opacity. A
  copy ghost — raised surface, 1 px `accent` border, soft shadow, about 0.85 opacity — follows the
  pointer at the grab offset, with the copy cursor. On drop it settles 140 ms `easeOutCubic` and
  becomes the real item. Toggling Option mid-drag switches between copy and move without restarting
  the drag.
- **Cancel (Esc, or a release over no valid target).** The dragged item or ghost returns into its
  origin in 140 ms `easeInCubic` (a ghost also fades), its neighbours glide back in 160 ms, and
  nothing is committed — no undo step.

## formatShortcutHint

```cpp
juce::String formatShortcutHint(const juce::String& base,
                                const juce::String& shortcutDisplay);
```

Composes tooltip text with an optional keyboard shortcut hint appended in `[brackets]`. An empty
`shortcutDisplay` omits the hint. Used by feature controls to produce consistent `setTooltip()`
strings.

## What moves, and how

| Feature | Animation | Owner |
|---|---|---|
| **Module drop landing** | Eased tween from drop position to the snapped, anti-overlapped final position (`easeOutBack`); `computeDropFinalPosition` is a pure helper | `GraphEditor` |
| **Mod-matrix show/hide** | Open-fraction tween, `easeInOutCubic` | `GraphEditor` |
| **Library sidebar show/hide** | `PanelSlide` fraction tween (190 ms, `easeInOutCubic`), shared driver | `MainComponent` |
| **Library section collapse/expand** | Band-height fold (150 ms), `easeInOutCubic` | `ModuleLibraryComponent` |
| **AI panel show/hide** | `PanelSlide` fraction tween (190 ms, `easeInOutCubic`), shared driver | `MainComponent` |
| **Timeline panel show/hide** | `PanelSlide` fraction tween (190 ms, `easeInOutCubic`), shared driver — same slide, bottom axis | `MainComponent` |
| **Mixer Own-panel show/hide** | `PanelSlide` fraction tween (190 ms, `easeInOutCubic`) on the controller's OWN driver — not the shared one | `MixerPlacementController` |
| **Macro port names zoom fade** | Alpha is a pure function of zoom (`easeInOutCubic` over 0.5 to 0.7), no driver or timer, so not a time-bounded-rule exception; the strip fill recedes; on an open macro the same factor also slides each port's interior jack onto its boundary jack and narrows the painted strip to a rail (layout widths fixed) | `GraphEditor` / `MacroCardComponent` |
| **Empty-canvas first-run hint** | Static drawn text, no animation — drawn only when `isCanvasEmpty(nodeCount)` returns `true` | `GraphEditor` |
| **Dock tab, mixer column and timeline track reorder** | Lifted item follows the pointer; neighbours glide aside (160 ms, `easeOutCubic`); settle on drop (140 ms); Esc returns it (140 ms, `easeInCubic`) — frames only while a tween runs; see [Reorder drag](#reorder-drag) | `BottomDockComponent`, `MixerPanelComponent`, `TimelinePanelComponent` via `ReorderDragAnimator` |
| **Side pane open/close** | The pane's width tweens 160 ms `easeOutCubic` in, 110 ms `easeInCubic` out from the current width; content keeps its full width and is revealed; lands at once when not on screen; see [side pane](side-pane.md) | `SidePane` |
| **Zones list row drag** | The Mixer side pane's channel rows and group headings make room for a dragged row (160 ms, `easeOutCubic`) on the shared vertical `ReorderDragAnimator`; the drop only assigns a group | `MixerZonesPane` via `ReorderDragAnimator` |
| **Library rows** | Hover highlight; grab / dragging-hand cursor on draggable rows; per-module descriptions via `descriptionFor(name)` surfaced as `setTooltip()`; search-query substring highlight on matching labels | `ModuleLibraryComponent` |
| **Preset-load feedback** | Status bar text updated during load; no spinner | `MainComponent` into `StatusBarComponent` |
| **AI request Cancel and spinner** | Cancel button visible while a request is in flight; pulsing "thinking" spinner, time-bounded — stops on completion or cancel, confined to its region | `AIChatComponent` |
| **Timeline playhead** | 30 Hz vertical position line, **playing only**, repainting only the strip between its old and new x | `TimelinePlayheadOverlay` |
| **Zoom settle debounce** | `zoomSettleAnim`: a DEBOUNCE `AnimationDriver` (140 ms, `kZoomSettleMs`) with a no-op `onUpdate` — zero repaints while running, all the work in `onComplete`, which thaws the frozen card rasters | `GraphEditor` |
| **Card make-room / return / auto-arrange glide** | Cards moved by a make-room push, a neighbour return or Auto Arrange slide from the old to the new spot (160 ms, `easeOutCubic`): geometry is final at once, the real card is hidden (alpha 0) and a snapshot glides on a canvas overlay; cables touching it follow; hulls and port strips land at once; undo/redo and loads land at once; a retarget starts from the drawn position; frames only while it runs; see [Making room](layout.md#making-room-when-something-grows) | `CardGlideAnimator` via `GraphEditor` |
| **Macro-crossing cable slide + module flash (FRO41)** | On a Cmd-drag finalize that actually crosses an expanded macro's hull: a cable re-routed through an auto-created/removed port slides to its new anchor (220 ms, `easeOutCubic`), and the dragged module gets a fading ring — pure tween state in `MacroCrossingAnimator` (`Source/UI/Graph/MacroCrossingAnimator/`), driven by `macroCrossingDriverAnim_`; see [`docs/macros/menu-and-membership.md#cable-crawl-and-module-flash-fro41`](../macros/menu-and-membership.md#cable-crawl-and-module-flash-fro41) | `GraphEditor` |
| **Shortcut hint bubbles** | One tween value `t` scales (0.6 -> 1), moves (from the labelled button's centre, or 12 px below a hidden-panel pill) and fades every Cmd-hold key-cap bubble: 160 ms `easeOutCubic` in, 110 ms `easeInCubic` back out from the current `t`; pure geometry in `hint::animatedBubbleBounds`; one `AnimationDriver`, no timer besides the 500 ms show delay; see [`docs/control/shortcuts.md`](../control/shortcuts.md#shortcut-hints) | `ShortcutHintOverlay` |
| **Toolbar toggle pill** | Instant state change (accent pill when on), no timer or animation — driven by `applyToolbarIcons()`'s and `setLibraryVisible()`'s `setToggleState(dontSendNotification)` calls | `ToolbarComponent` |
| **Velocity strip readout** | The value beside a hovered or dragged stick fades 160 ms `easeOutCubic` in / 110 ms `easeInCubic` out from the current opacity and slides ~4 px from the stick head to its spot (`velocitylane::readoutSlidePx`); stick-to-stick moves and edits keep the current opacity; lands at once when not on screen; one `AnimationDriver` | `PianoRollVelocityLane` |

## The time-bounded animation rule

**All animations MUST be time-bounded.** An `AnimationDriver` runs for a finite duration and stops
at `t = 1.0`.

There are exactly **two** permitted exceptions, and both are bounded by an *activity* rather than by
a duration:

| Exception | Runs while | Confined to | Stops |
|---|---|---|---|
| AI thinking spinner (`AIChatComponent::SpinnerDot`) | a network request is in flight | its own 8x8 component | on completion or cancel |
| Timeline playhead (`TimelinePlayheadOverlay`) | the transport is PLAYING | a strip a few px wide, spanning the panel's ruler and lanes | on stop or pause, with one final strip |

`zoomSettleAnim` is **not** a third exception: it is a normal time-bounded `AnimationDriver` that
restarts on every zoom event — the restart-on-event IS the debounce — and its `onUpdate` is a no-op,
so it fires zero repaints of its own. Only `onComplete` does one-time work, thawing the raster
freeze.

Never add:

- Continuous `timerCallback` repaints on `ModuleComponent` or its children outside the existing
  gated 15 Hz gate.
- A free-running `AnimationDriver` — no duration, or a duration far longer than the visible
  transition.
- Per-frame `repaint()` calls in any path that is always active rather than gated to an active
  transition.

### The playhead's confinement contract

A third exception is not granted just because the second was. The playhead earned it by satisfying
three clauses, all enforced in code and asserted in `Tests/UI/Timeline/TimelinePlayheadTests.cpp`:

1. **Playing only.** The 30 Hz `juce::Timer` is started on the play transition and stopped on the
   stop or pause transition — it never runs while the transport is stopped, so an idle app repaints
   nothing (`ZeroRepaintsOver100IdleFrames`).
2. **Strip only.** A frame never repaints the component. It repaints the *union of the old and new
   line strips* (`kStripHalfWidth` px either side of the line), clipped to the bounds; a frame whose
   rounded x did not move requests nothing at all (`PlayingRequestsConfinedStrips`).
3. **Explicit stop.** Stopping emits exactly **one** final strip, so the line settles on the
   position playback ended at, and then goes silent (`StopEmitsOneFinalStripThenSilence`).

### The paint-count pattern

`TimelinePlayheadOverlay` routes **every** repaint it asks for through one protected virtual:

```cpp
protected:
    virtual void requestRepaintStrip (juce::Rectangle<int> strip);   // default: repaint (strip)
```

A test subclasses the component and overrides that seam to count calls and record rects, which turns
"how many repaints does an idle app cost?" into an ordinary headless assertion — no peer, no message
loop, no screenshot diffing:

```cpp
struct CountingPlayhead : synth::ui::TimelinePlayheadOverlay {
    using TimelinePlayheadOverlay::TimelinePlayheadOverlay;
    int requests = 0;
    void requestRepaintStrip (juce::Rectangle<int> r) override {
        ++requests;
        TimelinePlayheadOverlay::requestRepaintStrip (r);
    }
    void tick() { timerCallback(); }   // the protected timer callback, driven by hand
};
```

**Reuse this pattern for any future timed repaint.** A repaint budget that cannot be asserted is a
repaint budget that will regress.

`PianoRollComponent` is the first reuse, and it is a *delegation*, not a fourth exception. The roll
maps beats through its own zoom and scroll, so while it is open the overlay confines itself to
`getSharedRegion()` — empty while the roll is open, since the ruler rows follow the roll's mapping
override too, see [piano-roll](../timeline/piano-roll.md#the-ruler-above-the-roll) — and hands the
roll the drawn beat through `TimelinePlayheadOverlay::LocalPlayheadClient`. The roll draws the line
at its own x under an identical `requestRepaintStrip` seam and the identical no-move-no-repaint
gate: still one timer for the whole panel, still zero repaints while stopped. See
[piano-roll](../timeline/piano-roll.md#playhead-delegation).
