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
and `CardLayoutEditorComponent` (`CardLayoutEditorComponentDrag.cpp`, the card layout editor and the plugin
picker built on it; group headers take part as fixed slots, so a row dropped under one joins that group,
[editing a layout](module-card-layout.md#editing-a-layout)). The pointer is converted into the list's own
coordinates on every event, and the commit goes out once, on release.

### Undo and redo glide

The rule: **anything that glides forward glides on undo and redo.** An undo that jumps looks like a different
app from the edit it reverses. The two entry points are `AppUndoManager::undo()` and `redo()` (the Cmd+Z / Cmd+Shift+Z
path); nothing else needs to know.

- **Graph cards.** Both open a `CardGlideAnimator::Scope` around the restore. The scope captures every card's bounds
  before it, and after the restore's `updateComponents()` arms the usual slide from those rects to the restored ones,
  with the same geometry-final-at-once contract, 160 ms `easeOutCubic` and cable following as a forward move. Cards the
  restore rebuilds (node removed or re-created) have no "before" and land at once; a project load never goes through
  `undo()` and so never glides.
- **Timeline track rows.** `AppUndoManager::isRestoring()` is true during the restore. When the timeline panel rebuilds
  its header rows while it is set and the rebuild is a pure reorder (same tracks, new order),
  `TimelinePanelComponent::glideTrackRowsFrom` starts the rows at their old slots and `release()`s the shared
  `ReorderDragAnimator` to the new ones (140 ms settle, frames only while it runs). No row is lifted. Any other rebuild
  lands at once.
- **Off-screen.** Same check as the forward move (the app has no Reduce Motion setting): the timeline glide runs only
  while the panel is showing (`ReorderDragAnimator`'s `animate` flag), so headless tests land at once unless a test
  forces it.

### Drag-and-drop cursor

Every place the user drags an item to move, reorder or drop it shows the grabbing-hand cursor,
consistently, through one header-only helper: `Source/UI/Layout/DragCursor.h`
(`synth::ui::dragGrabCursor()`, `dragCopyCursor()`, `showDragCursor(component, copying)`,
`endDragCursor(component, rest)`). No site sets `DraggingHandCursor` or `CopyingCursor` by hand.

- **Reorder handles** (a bottom-dock tab, a timeline track header row, a mixer column header) keep
  the normal arrow on hover and press. Each calls `followDragCursor(handle, dragging)` on every
  mouse drag, with whether its owner's `ReorderDragAnimator` is really dragging (past the 4 px
  threshold, not cancelled by Esc), and `endDragCursor(handle)` on release before any hook that can
  destroy it. So the dragging hand appears the moment the press becomes a drag, and the arrow
  returns on release, or on the first pointer move after Esc. A child that can start the drag (a
  track's name, a mixer column's name label) is a `CursorDelegatingLabel` showing
  its handle's cursor. Direct, Master and pinned mixer columns never drag, so never show the hand.
- **Small grab handles** (a thing whose only job is to be picked up: mixer zones row, list drag
  handles, MIDI-remote controller surface cell, module library row, timeline ruler marker flag)
  show `juce::MouseCursor::DraggingHandCursor` on hover AND during the drag.
- **Tool-driven canvases** (timeline clips, piano roll notes, module cards, macro cards) keep the
  tool cursor on hover (`ToolCursors.h` / `applyToolCursor` stay in charge). The grab cursor shows
  only once a move drag has actually started (past its threshold), the copy cursor shows while an
  Option/Alt copy-drag is active (toggling Option mid-drag switches between copy and grab), and
  the tool or normal cursor returns on release or Esc.
- **Out of scope:** cable drags between ports, value drags (knobs, faders, sliders, curve points),
  resize handles, marquee selection, pan/scroll, and OS file drops.

## Popup windows

Right-click menus and their submenus, ComboBox dropdowns, call-out popovers, alert boxes and
dialogs (Settings, Export, confirmations) all appear and disappear with the same soft motion. The
mechanism is `synth::ui::PopupMotion` (`Source/UI/Layout/PopupMotion.{h,cpp}`); the numbers and the
frame math are pure functions in `synth::ui::popup_motion`, unit-tested in
`Tests/UI/Layout/PopupMotionTests.cpp`. It reuses `AnimationDriver` and the easing helpers above.

| | In | Out |
|---|---|---|
| Opacity | 0 to 1, `easeOutCubic`, 160 ms | 1 to 0, `easeInCubic`, 110 ms |
| Slide | starts 4 px toward the anchor, ends at rest | 2 px back toward the anchor |
| Reduce motion | plain fade, 80 ms, no slide | plain fade, 80 ms, no slide |

"Anchor" is the pointer: the window slides away from `Desktop::getMousePosition()` along the axis
it sits furthest off it (`popup_motion::slideDirection`). A menu opened with its corner at the
pointer, a dropdown under its combo box and a popover under its button slide down; a menu flipped
above its anchor slides up; a submenu beside its parent row slides sideways; a window with the
pointer inside it, or a dialog opened from the keyboard, slides down. Reduce motion is
`synth::ui::prefersReducedMotion()` (`ReducedMotion.h`): macOS Reduce motion; other platforms do not read a setting yet and answer false. It is read each time a popup shows or hides.

**How windows are caught.** JUCE has no "window created" event, so each kind is caught at the
earliest look-and-feel call that receives it, before it is first shown
(`AppLookAndFeelWindowMotion.cpp`):

- menus, submenus and ComboBox dropdowns: `preparePopupMenuWindow`;
- alert boxes made by `AlertWindow::showAsync`/`showMessageBoxAsync`/`ThreadWithProgressWindow`:
  `createAlertWindow`;
- call-out boxes: `getCallOutBoxBorderSize`, which the call-out asks for in its constructor;
- dialogs (`DialogWindow::LaunchOptions`) and hand-built `AlertWindow`s have no such call: launch a
  dialog with `PopupMotion::launchDialog(options)` instead of `options.launchAsync()`, and call
  `PopupMotion::attach(*window)` before `enterModalState` on a `new juce::AlertWindow`. Every
  existing site already does.

`attach` installs one self-deleting listener per window. It starts the slide the moment the window
becomes visible (frame 0 is applied before the first VBlank, a watchdog timer finishes the move if
no VBlank ever comes, so a window can never stay invisible) and never delays input: the window is
fully clickable from its first frame, only its opacity and a few pixels move. It does nothing for a
window without a native peer (every headless test, a call-out given a parent component) or while
`PopupMotion::setEnabled(false)`.

**Leaving.** JUCE hides, or deletes, a popup the instant it is dismissed, so the leaving motion
runs on a picture of it: a click-through, shadowed window at the same place that fades and slides
back, then deletes itself. A plain window (menus, alerts, call-outs) is pictured with
`createComponentSnapshot`; a window with a native title bar (the app's dialogs) is pictured, title
bar included, from its `NSView` on macOS. A window deleted while still flagged visible (a menu
after a choice, an alert after a button) leaves with the picture taken while it was open.

**Not covered.**
- Leaving, for a native-title window on Windows or Linux: nothing is drawn, it vanishes at once
  (the opening still animates).
- The leaving picture is a snapshot, not the live window: a menu row's hover highlight or text typed
  into an alert after it opened is not in it, and a dialog closed within 160 ms of opening has none
  and just disappears.
- A tooltip (`TooltipWindow` is parented to the main component, so it has no window of its own to
  fade), a call-out given a parent component, the main window, hosted-plugin editor windows and
  detached panel windows are not animated.
- Fading uses the window's own opacity: Windows and macOS honour it, a Linux X11 session without a
  compositor shows the slide only.
- The choose-a-row accent flash is not done (the menu's item components belong to JUCE and are gone
  as the menu closes).

## Motion rules

Every animation in the app follows these; a new one that cannot is a design question, not a
shortcut.

- **Durations.** Hover state 80–120 ms. A small reveal or tooltip 160 ms in, 110 ms out. Menus,
  call-outs and dialogs: see [Popup windows](#popup-windows). Reorder
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
- **Popups.** A menu, dropdown, popover, alert or dialog fades in while sliding 4 px away from its
  anchor and leaves the same way, 2 px back; Reduce motion makes it a plain 80 ms fade. One shared
  mechanism, never per call site: [Popup windows](#popup-windows).
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

## Popup reveal

A popover opened in a `juce::CallOutBox` eases in with `CalloutReveal` (`Source/UI/Layout/CalloutReveal.{h,cpp}`),
owned by the popup content and started from its `parentHierarchyChanged()`: the callout is hidden (alpha 0) the moment
the content is parented, then, on the next message-loop turn once it is showing, fades in over 160 ms (`easeOutCubic`)
while sliding 8 px out of the side that faces the element that opened it (the side of the callout with the largest inset,
`directionToAnchor`; a popover under a button grows down from it), landing exactly on its final bounds. Only the entrance
is animated: a callout dismisses synchronously, so the 110 ms exit of the motion rules has nothing to run on. Today the
colour picker (`ColourPickerPopup`) uses it, which covers the Timeline track swatch, the ruler's marker colours, the macro
recolour and the mixer's colour dot.

### Reduced motion

`synth::ui::prefersReducedMotion()` (`Source/UI/Layout/ReducedMotion.h`) answers whether the OS asks apps to cut
non-essential motion: macOS Reduce Motion (`NSWorkspace.accessibilityDisplayShouldReduceMotion`, read each time in
`ReducedMotionMac.mm`). Windows and Linux answer false for now -- their settings are not read yet. `CalloutReveal` is the
first consumer: with the preference on, the popup is never hidden and nothing animates; it also lands at once when the
callout is not on screen (no VBlank reaches it). The other animations in this document are still unconditional; a new
non-essential one asks this before it starts. `setReducedMotionForTest` forces the answer.

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
| **Canvas frame resize** | The patch frame grows/shrinks right and down to keep 400 px past the outermost card (220 ms, `easeOutCubic`, time-bounded, retargets from the current rect); project open snaps. See [Canvas frame](layout.md#canvas-frame) | `GraphEditor` via `CanvasFrame` |
| **Mod-matrix show/hide** | Open-fraction tween, `easeInOutCubic` | `GraphEditor` |
| **Library sidebar show/hide** | `PanelSlide` fraction tween (190 ms, `easeInOutCubic`), shared driver | `MainComponent` |
| **Library section collapse/expand** | Band-height fold (150 ms), `easeInOutCubic` | `ModuleLibraryComponent` |
| **AI panel show/hide** | `PanelSlide` fraction tween (190 ms, `easeInOutCubic`), shared driver | `MainComponent` |
| **Timeline panel show/hide** | `PanelSlide` fraction tween (190 ms, `easeInOutCubic`), shared driver — same slide, bottom axis | `MainComponent` |
| **Mixer Own-panel show/hide** | `PanelSlide` fraction tween (190 ms, `easeInOutCubic`) on the controller's OWN driver — not the shared one | `MixerPlacementController` |
| **Macro port names zoom fade** | Alpha is a pure function of zoom (`easeInOutCubic` over 0.5 to 0.7), no driver or timer, so not a time-bounded-rule exception; the strip fill recedes; on an open macro the same factor also slides each port's interior jack onto its boundary jack and narrows the painted strip to a rail (layout widths fixed) | `GraphEditor` / `MacroCardComponent` |
| **Empty-canvas first-run hint** | Static drawn text, no animation — drawn only when `isCanvasEmpty(nodeCount)` returns `true` | `GraphEditor` |
| **Dock tab, mixer column and timeline track reorder** | Undo/redo of a timeline track reorder glides the rows too (140 ms; see [Undo and redo glide](#undo-and-redo-glide)); lifted item follows the pointer; neighbours glide aside (160 ms, `easeOutCubic`); settle on drop (140 ms); Esc returns it (140 ms, `easeInCubic`) — frames only while a tween runs; see [Reorder drag](#reorder-drag) | `BottomDockComponent`, `MixerPanelComponent`, `TimelinePanelComponent` via `ReorderDragAnimator` |
| **Side pane open/close** | The pane's width tweens 160 ms `easeOutCubic` in, 110 ms `easeInCubic` out from the current width; content keeps its full width and is revealed; lands at once when not on screen; see [side pane](side-pane.md) | `SidePane` |
| **Zones list row drag** | The Mixer side pane's channel rows and group headings make room for a dragged row (160 ms, `easeOutCubic`) on the shared vertical `ReorderDragAnimator`; the drop only assigns a group | `MixerZonesPane` via `ReorderDragAnimator` |
| **Library rows** | Hover highlight; grab / dragging-hand cursor on draggable rows; per-module descriptions via `descriptionFor(name)` surfaced as `setTooltip()`; search-query highlight on matching labels (shared `drawSearchHighlightedText`, static fill, no animation) | `ModuleLibraryComponent` |
| **Preset-load feedback** | Status bar text updated during load; no spinner | `MainComponent` into `StatusBarComponent` |
| **AI request Cancel and spinner** | Cancel button visible while a request is in flight; pulsing "thinking" spinner, time-bounded — stops on completion or cancel, confined to its region | `AIChatComponent` |
| **Timeline playhead** | 30 Hz vertical position line, **playing only**, repainting only the strip between its old and new x | `TimelinePlayheadOverlay` |
| **Cursor glide** | While Cmd+Left / Cmd+Right is held, a VBlank frame per refresh moves the transport cursor (ease-in speed, capped); on release a 140 ms `easeOutCubic` settle lands it on the grid. Both are `ReorderFramePump` runs, so frames stop with the key; see [`docs/timeline/transport.md`](../timeline/transport.md#gliding-the-cursor) | `TimelineCursorGlide` via `TimelinePanelComponent` |
| **Zoom settle debounce** | `zoomSettleAnim`: a DEBOUNCE `AnimationDriver` (140 ms, `kZoomSettleMs`) with a no-op `onUpdate` — zero repaints while running, all the work in `onComplete`, which thaws the frozen card rasters | `GraphEditor` |
| **Card make-room / return / auto-arrange glide** | Cards moved by a make-room push, a macro slid in from the canvas edge, a neighbour return or Auto Arrange slide from the old to the new spot (160 ms, `easeOutCubic`): geometry is final at once, the real card is hidden (alpha 0) and a snapshot glides on a canvas overlay; cables touching it follow; hulls and port strips glide separately (see the macro border glide row); undo/redo glide the cards back and forth (see [Undo and redo glide](#undo-and-redo-glide)); loads land at once; a retarget starts from the drawn position; frames only while it runs; see [Making room](layout.md#making-room-when-something-grows) | `CardGlideAnimator` via `GraphEditor` |
| **Macro-crossing cable slide + module flash (FRO41)** | Also on a cable drop that mints a macro port: the new cables' port ends emerge from the release point (no flash). When a drag crosses an expanded macro's hull (applied live, mid-drag, or on the drop): a cable re-routed through an auto-created/removed port slides to its new anchor (220 ms, `easeOutCubic`; endpoints are offset from their live anchors, so a slide started mid-drag stays attached to the moving card), and the dragged module gets a fading ring — pure tween state in `MacroCrossingAnimator` (`Source/UI/Graph/MacroCrossingAnimator/`), driven by `macroCrossingDriverAnim_`; see [`docs/macros/menu-and-membership.md#cable-crawl-and-module-flash-fro41`](../macros/menu-and-membership.md#cable-crawl-and-module-flash-fro41) | `GraphEditor` |
| **Macro border glide** | A macro's border glides to its new bounds instead of snapping (220 ms, `easeOutCubic`) on a live membership change, a drag candidate change, and when a drag ends and the held border is released. The dashed outline, port strips, chip and buttons and docked port widgets glide together (the driver re-docks the widgets each frame); each edge is offset from the live border, so a border that keeps moving still lands on it. Pure state in `MacroHullGlide` (`Source/UI/Graph/MacroHullGlide/`), applied in `GraphEditor::paintedMacroHullBounds`; see [`docs/macros/menu-and-membership.md#macro-borders-glide`](../macros/menu-and-membership.md#macro-borders-glide) | `GraphEditor` |
| **Cable retract** | A removed cable retracts into its source jack and fades (180 ms, linear progress with an `easeInCubic` pull) instead of vanishing: a cable cut, a jack disconnect, a removed modulator, and undo/redo taking a cable away. The ghost paints after the live cables; project load and New do not animate. Pure state in `CableRetractAnimator` (`Source/UI/Graph/CableRetractAnimator/`), armed by `GraphEditor::retractCablesGoneSince`; see [cables](cables.md#retracting-removed-cables) | `GraphEditor` |
| **Colour picker (popup reveal)** | The callout fades in 160 ms `easeOutCubic` and slides 8 px out of the side facing the dot, swatch or chip that opened it; at once under macOS Reduce Motion or when not on screen; entrance only; see [Popup reveal](#popup-reveal) | `CalloutReveal` via `ColourPickerPopup` |
| **Shortcut hint bubbles** | One tween value `t` scales (0.6 -> 1), moves (from the labelled button's centre, or 12 px below a hidden-panel pill) and fades every Cmd-hold key-cap bubble: 160 ms `easeOutCubic` in, 110 ms `easeInCubic` back out from the current `t`; pure geometry in `hint::animatedBubbleBounds`; one `AnimationDriver`, no timer besides the 500 ms show delay; see [`docs/control/shortcuts.md`](../control/shortcuts.md#shortcut-hints) | `ShortcutHintOverlay` |
| **Toolbar toggle pill** | Instant state change (accent pill when on), no timer or animation — driven by `applyToolbarIcons()`'s and `setLibraryVisible()`'s `setToggleState(dontSendNotification)` calls | `ToolbarComponent` |
| **Velocity strip readout** | The value beside a hovered or dragged stick fades 160 ms `easeOutCubic` in / 110 ms `easeInCubic` out from the current opacity and slides ~4 px from the stick head to its spot (`velocitylane::readoutSlidePx`); stick-to-stick moves and edits keep the current opacity; lands at once when not on screen; one `AnimationDriver` | `PianoRollVelocityLane` |
| **Mod-dot tooltip** | The "LFO 1 · +42%" readout above-right of a knob whose landing dot is dragged (or stepped by key) fades 160 ms `easeOutCubic` in / 110 ms `easeInCubic` out from the current opacity; a plain 80 ms linear fade under Reduce Motion; lands at once when not on screen; painted on the canvas over the cards; one `AnimationDriver`; see [mod dot](../modules/modulation.md#the-mod-dot-drag-an-amount-from-the-landing-dot) | `ModDotTooltip` via `ModDotController` |
| **Velocity strip show/hide** | The grid gives up `round(p * full)` px at the bottom while the strip keeps its full height and rises from the roll's bottom edge (clipped, never squashed); 200 ms (`kScalePanelAnimMs`) `easeInOutCubic`, same as the scale panel, retargets from the current progress, lands at once when not on screen or on a persisted restore | `PianoRollComponent` / `VelocityLaneSlide` |

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
