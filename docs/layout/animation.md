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

Every reorderable list (the bottom dock's tab strip, the mixer's track columns, the mixer's zones rows and send rows, the timeline's track list and a track's automation lanes, the macro port dialog's rows and the plugin knob picker's rows) uses one behaviour, in `Source/UI/Layout/ReorderDrag/`:

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
  with the same geometry-final-at-once contract, 160 ms `easeOutCubic` and cable following as a forward move. A card
  matches its "before" by component, or by node id when the restore tore every card down and rebuilt it (any restore
  that frees a node does, e.g. the macro port a take-out created): so a module that moved between a macro and the root
  glides back and forth with its cables. A card for a node the restore creates grows in, and one it removes shrinks away
  ([delete and undo animation](#delete-and-undo-animation)); a project load never goes through `undo()` and so never glides.
- **Edit Layout controls.** While the on-card layout editor is open, the card an undo or redo rebuilds gives the
  editor's controls the same treatment: `CardLayoutOnCardEditor` remembers each cell's rect before the re-sync and
  glides every control whose rect changed from there to the restored place (`startGlide`, 160 ms `easeOutCubic`, the
  geometry final at once). A control the restore brings back grows in (`fadeInControl`) and one it takes away shrinks
  out (`startShrinkGhostOf`, from a picture of the card taken at the previous re-sync). Nothing glides under Reduce
  Motion or off screen.
- **Macro borders.** `undo()`/`redo()` also snapshot the painted macro borders before the restore and hand them to
  `GraphEditor::glideHullsFrom` after it, so the border a take-out or join moved glides (220 ms, `MacroHullGlide`) back
  to where it was, like it glided out.
- **Timeline track rows.** `AppUndoManager::isRestoring()` is true during the restore. When the timeline panel rebuilds
  its header rows while it is set and the rebuild is a pure reorder (same tracks, new order),
  `TimelinePanelComponent::glideTrackRowsFrom` starts the rows at their old slots and `release()`s the shared
  `ReorderDragAnimator` to the new ones (140 ms settle, frames only while it runs). No row is lifted. Any other rebuild
  lands at once.
- **A duplicated track's row.** Cmd+D / "Duplicate Track" arms `TimelinePanelComponent::armTrackDuplicateGlide`; the
  rebuild then starts the new row on its source's slot and glides it, and the rows below, to their places through the same
  `glideTrackRowsFrom` (140 ms). It lands at once under Reduce Motion (`prefersReducedMotion()`) or off-screen. Undoing it
  removes the row at once, like deleting a track.
- **Off-screen.** Same check as the forward move: the timeline glide runs only
  while the panel is showing (`ReorderDragAnimator`'s `animate` flag), so headless tests land at once unless a test
  forces it.

### Controls swapping in place

When a condition on a card changes which controls show in a cell (the ADSR's Sync flipping each stage between its
time and its note division, or Time look for Tempo look), the card keeps its size and the controls swap in two
steps that never overlap in a frame: the leaving controls shrink to their centre over 190 ms
(`control_motion::kSwapShrinkMs`, a picture of each shrinking as `ShrinkGhost`, `easeInCubic`), THEN the arriving
ones grow from their centre with the 8% bounce (`kGrowMs`, `growScale`). While the shrink runs the arriving
controls are held hidden (`CardBody::isHeldBySwap`, honoured by `applyVisibility`). `SwapMotion`
(`CardBodySwapMotion.cpp`) runs it from a VBlank pump on the card; `CardBody::refreshConditions` pictures what
leaves before the card re-lays out and starts it after, and skips it when the card changed height (a mode switch the
card makes whole). Under Reduce Motion, with Animations off, or while the card is not showing, the swap is
instant. A second flip lands the first at once. The on-card layout editor re-reads its outlines when the swap
starts and again when it has landed (`CardBody::onConditionsApplied`). Tests step the motion by hand
(`CardBody::setForceAnimateForTest`, `stepSwapMotionForTest`).

### Delete and undo animation

Deleting a canvas card shrinks it away and Cmd+Z grows it back. The model change (delete, undo, redo) is still
instant and one undo step; the animation is a paint-only layer in `CardGlideAnimator` (`CardGlideAnimatorGhosts.cpp`),
drawn by the same overlay as the glide. `Source/UI/Layout/ExitEnterTimeline.h` is the shared, pure phase timeline for
every surface that animates a delete:

- **Delete:** exit (the item shrinks toward its centre, 180 ms) THEN gap (the cards that moved close up, 200 ms).
- **Cmd+Z / redo:** gap (neighbours make room, 200 ms) THEN grow (the item grows back from its centre, 180 ms) THEN a
  1 px accent outline around it fades (400 ms). Redo of a delete exits again.
- The phases never overlap, so no frame shows two cards on top of each other.
- **Reduce Motion** (`prefersReducedMotion()`): a plain fade in place instead of the shrink or grow.
- **Opt-in snapshot:** the delete paths (`GraphEditor::deleteSelection`, `requestDeleteModule`) open a
  `CardGlideAnimator::Scope` and call `noteExit()` for each card before the removal. An undo/redo scope
  (`Scope(animator, restore=true)`) pictures nothing up front: a restore that frees a node tears every card down in
  `GraphEditor::detachAllModuleComponents()`, which first calls `noteExitsBeforeTeardown()`, and a card the restore only
  hid is pictured when the scope closes. A card that is gone afterwards exits and a card that is new enters; a card
  that survives under the same node id is never a ghost. So a parameter-only undo, or one that only moves cards,
  pictures no card at all.
- **Pictures come from the card's raster:** a ghost or glide snapshot is the card's own `ZoomFrozenCachedImage` raster
  (`lastRaster()`, shared, no paint) whenever it has one at the card's size; only a card never painted at that size is
  rendered. Rendering every on-screen card when an undo scope opened stalled each undo of a big project for the
  length of a full repaint of every card.
- **A frame repaints only what moves** (see [rendering](rendering.md#per-frame-work-does-not-grow-with-the-patch)):
  the ghosts and snapshots, the cables on gliding cards, and the retracting cable ghosts, never the whole canvas.
- **Off-screen:** ghosts are made only while the canvas is showing (`Hooks::canAnimate`), so headless tests see the
  final state synchronously; tests force it with `setForceAnimateForTest` and step `applyTimelineAtMs`.
- Not yet covered: timeline track rows, collapsed macro cards, and the mod panel's source rows (those still remove at
  once, or use their own collapse). They should reuse `ExitEnterTimeline`.

### Controls arriving and leaving a card

In the on-card layout editor ([editing a layout](module-card-layout.md#editing-a-layout)) a control that is added
or hidden moves the same way: the pure numbers and the leaving picture are `synth::ui::control_motion`
(`Source/UI/Layout/ControlMotion.h`), and `CardLayoutOnCardEditor` drives them with its `ReorderFramePump`s.

- **Add:** the layout is written at once and the new control grows in from its centre (a fader along its length
  only), 0 to 1.08 to 1 over 200 ms (`growScale`), its alpha rising with it. It is the real widget, scaled by a
  component transform, so every frame goes through the card's cached image like any other repaint.
- **Hide:** a picture of the control (`ShrinkGhost`, taken before anything changes) shrinks toward its centre over
  150 ms (`shrinkScale`) while the control itself is made invisible under it, and the card keeps its layout until
  then: nothing moves under the shrinking picture. When it has gone the hide is written and every control the
  rebuilt card moved glides from where it stood, 200 ms (`kCloseGapMs`, through `startGlide`). Another edit, Done,
  or the next Hide writes a hide still shrinking first (`flushNudge` runs `flushPendingHide`); Cancel drops it and the
  control is shown again. Parts of the card that are not outlined controls (the card's own buttons, its jacks) take
  their new place at once.
- **Reduce Motion:** an 80 ms fade in place instead of the grow or shrink.
- **Off-screen:** nothing is animated and the write is immediate; tests force the animated path with
  `setForceAnimateForTest` and step it with `setShrinkGhostProgressForTest` / `applyAddFrameForTest`
  (`OnCardControlMotionTests.cpp` paints the canvas at each step and checks the drawn size changes frame by frame).

Both are usually started from a call-out (the control's panel, the Add control panel) that closes at the same time.
That close must not use the platform's own window animation: see [Popup windows](#popup-windows).

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
| Opacity | 0 to 1, linear in time, 160 ms | 1 to 0, linear in time, 110 ms |
| Slide | starts 4 px toward the anchor, ends at rest (`easeOutCubic`) | 2 px back toward the anchor (`easeInCubic`) |
| Reduce motion | plain fade, 80 ms, no slide | plain fade, 80 ms, no slide |

The opacity is linear in time while the slide keeps its cubic ease (`popup_motion::alphaAt` takes the cube root of the
eased progress). With a cubic on the opacity too, a leaving menu was still 87% opaque halfway through and vanished in
its last third, and an arriving one was 87% there halfway: both read as a pop (measured on the real app as
the ghost window's opacity per frame).

"Anchor" is the pointer: the window slides away from `Desktop::getMousePosition()` along the axis
it sits furthest off it (`popup_motion::slideDirection`). A menu opened with its corner at the
pointer, a dropdown under its combo box and a popover under its button slide down; a menu flipped
above its anchor slides up; a submenu beside its parent row slides sideways; a window with the
pointer inside it, or a dialog opened from the keyboard, slides down. Reduce motion is
`synth::ui::prefersReducedMotion()` (`ReducedMotion.h`): the Animations preference (below), which follows macOS Reduce motion by default; other platforms do not read a system setting yet and answer false. It is read each time a popup shows or hides.

The confirmations with a **Don't ask again** box (removing an LFO, deleting a track with Cmd+Backspace) are one
window, `showConfirmDontAsk` (`Source/UI/Chrome/ConfirmDontAskDialog.{h,cpp}`), which attaches `PopupMotion`, so they fade
in and out like any alert. The deleted track's row then leaves at once, like the menu's Delete Track.

**Tooltips** are the exception to "popup windows": they are children of their app window, so they
do not go through `PopupMotion`. See [Tooltips](#tooltips).

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

**Leaving.** Every close the app decides itself (Escape, a Close or choice button, a pick in a popover,
a programmatic close) goes through `PopupMotion::dismiss(window, reallyClose)`, which fades the LIVE
window out (110 ms, 80 ms plain fade under Reduce motion; the table above) and runs `reallyClose` one
turn after the fade ends. Never close a popup directly: `closeHostingWindow` (`DialogKeyboard.h`, the
Escape path of every dialog and call-out), `PopupMotion::dismissCallOut(box)` and
`PopupMotion::dismissModal(window)` (an `exitModalState(0)` of a `LaunchOptions` dialog) are the three
forms. `reallyClose` runs at once, before `dismiss` returns, for a window with no native peer, one
that was never attached, one not showing, or with the engine disabled, so headless runs and the full
test suite see the final state immediately; the frame driver only runs while a fade runs. While the
fade runs the window ignores the mouse and a second `dismiss` does nothing, so a choice cannot fire
twice. Tests reach the animated path with `PopupMotion::setAnimateOffScreenForTest(true)`.

Closes JUCE performs itself (click-away on a menu or call-out, a menu row chosen, an alert button, a
dialog's title-bar button) cannot be deferred, so they leave on a picture of the window: a
click-through, shadowed window at the same place that fades and slides back, then deletes itself. A
plain window (menus, alerts, call-outs) is pictured with `createComponentSnapshot`; a window with a
native title bar (the app's dialogs) is pictured, title bar included, from its `NSView` on macOS. A
window deleted while still flagged visible leaves with the picture taken while it was open. A window
that `dismiss` faded gets no picture (it already faded). `PopupMotion::getNumLeavingGhosts()` counts the pictures on screen, for tests (`PopupMotionLeaving`).
The picture is always-on-top: when a menu closes, JUCE brings the app window to the front, and a picture at the
normal window level was buried under it at once (the menu seemed to just disappear).

A close that is itself asynchronous (a call-out's `dismiss()` only posts its hide) leaves the window faded out until
it has really gone: the window is made whole again (alpha 1, rest position) one message-loop turn after
`reallyClose`, never while it is still on screen. Restoring it straight after `reallyClose` flashed the call-out back
at full opacity for a frame.

On macOS the platform's own window animation is turned off for every attached window
(`NSWindow.animationBehavior = None`, `PopupMotionMac.mm`). AppKit otherwise animated a closing call-out out a second
time (a ~200 ms fade and shrink) after ours, and while it ran the app window's new frames did not reach the screen:
a control growing or shrinking on the card behind the call-out froze on one frame and then jumped to the end
(measured on the real app with a screen recording; the app itself painted every frame).

**Not covered.**
- Leaving, for a native-title window closed by JUCE itself (title-bar button) on Windows or Linux:
  nothing is drawn, it vanishes at once (the opening still animates, and an app-decided close fades
  the live window on every platform).
- Closes left direct on purpose: the help call-out of the module library
  (`ModuleLibraryComponent::closeHelpPopover` and its switch to the pinned panel, which hand the
  popup's content between owners in the same call), a `ModDotController` being destroyed (its popover
  must be gone with it), and `NonModalLabel`'s modal-state exit (not a window).
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
- **Toolbar.** A top-bar button eases its hover (110 ms in, 90 ms out), press (80 ms) and lit chip
  (160 ms in, 110 ms out); on hover its glyph lifts 1 px and does one small per-icon motion, never
  more than 2 icon units from rest. Under Reduce Motion nothing moves and only the colours change:
  [What moves, and how](#what-moves-and-how).
- **Swapping controls.** Controls that swap in a cell (a Sync flip) shrink out over 190 ms, then the arriving ones
  grow in with the 8% bounce; never both at once ([details](#controls-swapping-in-place)).
- **Tooltips.** A hover tooltip fades in over 160 ms and out over 110 ms, with no slide, and shows at once under
  Reduce Motion: [Tooltips](#tooltips).
- **Popups.** A menu, dropdown, popover, alert or dialog fades in while sliding 4 px away from its
  anchor and leaves the same way, 2 px back; Reduce motion makes it a plain 80 ms fade. Every close,
  Escape and click-away included, leaves this way (`PopupMotion::dismiss`, never a direct close), and
  lands at once when not on screen. One shared mechanism, never per call site:
  [Popup windows](#popup-windows).
- **Mini map.** The graph editor's mini map fades and slides 12 px out of / into its corner when
  toggled (160 ms in, 110 ms out, plain 80 ms fade under Reduce Motion): [mini map](minimap.md#show-and-hide-motion).
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

## Tooltips

One shared window, `synth::ui::AppTooltipWindow` (`Source/UI/Layout/AppTooltipWindow.{h,cpp}`), serves every app
window: `MainComponent` owns one and each `DetachedPanelWindow` owns one. It is a `juce::TooltipWindow` that fades
a tip in over 160 ms and out over 110 ms, linear in time, the numbers and curve from `popup_motion`, with no
slide. juce hides a tip synchronously, so the fade-out runs on a click-through ghost sibling that paints the same text; whether
to animate is asked of the window the tip lives in, because the tip itself is already hidden by then.
Under Reduce Motion (`prefersReducedMotion()`) a tip uses the popups' plain 80 ms fade in and out (no slide was ever
there), and a window that is not on screen never animates. Never create a bare `juce::TooltipWindow`.

**Info and helper tips.** Preferences > Panels & Windows > "Show info tooltips" (user setting `showInfoTooltips`,
default on) hides the *info* tooltips, the ones that explain a control. A *helper* tip tells you something the screen
does not show anywhere else and keeps showing: mark its component with `synth::ui::markHelperTooltip(component)`
(`Source/UI/Layout/HelperTooltip.h`). Today those are the mixer sources badge ("Plays into this channel: ...") and the
preference's own toggle, so it can always say how to bring tips back. The window reads the setting each time it is
asked for a tip (`AppTooltipWindow::getTipFor`), so a toggle applies at once in every window with no push. The mod-dot
drag readout is painted on the canvas, not a hover tooltip, and is unaffected. Tests:
`Tests/UI/Layout/AppTooltipWindowTests.cpp`.

A window can opt into a different look by passing a `popup_motion::Style` to `PopupMotion::attach`:
the slide distances, `overshoot` (the arrival eases with `easeOutBackSoft`, peaking 3% past rest
before settling) and an `anchor` the window grows out of instead of the pointer. The mod dot's
panel is the one user (`ModDotPanelFrame::motionStyle`). Leaving never overshoots.

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

Preferences > Panels & Windows > "Animations" (user setting `animationMode`, `synth::ui::AnimationMode` in
`Source/UI/Layout/ReducedMotion.h`) chooses how much the app animates. It applies at once, app-wide; the saved choice is
applied at launch by `MainComponent`.

| Choice (stored value) | What happens |
|---|---|
| Follow system (`follow`, default) | macOS Reduce Motion decides (`NSWorkspace.accessibilityDisplayShouldReduceMotion`, read each time in `ReducedMotionMac.mm`). Windows and Linux answer "full motion" for now -- their settings are not read yet. An unknown stored value reads as this. |
| Full (`full`) | Always full motion, even with macOS Reduce Motion on. |
| Reduced (`reduced`) | As if Reduce Motion were on: short plain fades, nothing slides or grows. |
| Off (`off`) | Nothing animates: popups, panels, glides and settles run for 0 ms and land on their final state; things still appear and disappear, just without motion. |

`synth::ui::prefersReducedMotion()` answers true for Reduced and Off, false for Full, and the OS's answer for Follow
system, so every animation that already has a Reduce Motion branch (most of the ones in this document) follows the
setting. `synth::ui::animationsOff()` is true only for Off. `synth::ui::motionMs(full, reduced)` is the helper for a
duration that branches on the mode (full, reduced, or 0 under Off). Off is instant at the shared seams:
`AnimationDriver::start` runs `onUpdate` with the final eased value and `onComplete` before it returns,
`popup_motion::durationMs` is 0 and `PopupMotion` shows, hides and `dismiss()`es windows with no fade or leaving picture
(`reallyClose` runs at once), and the self-clocked motions (the Card Layout Editor's fades and shrink ghost, the
automation marker fade) land at once. A new non-essential animation asks `prefersReducedMotion()` before it starts, uses
`AnimationDriver` or `motionMs` for its duration, and lands on its final state at once under Off.
`CalloutReveal` is the first consumer of the reduced answer: with it on, the popup is never hidden and nothing animates;
it also lands at once when the callout is not on screen (no VBlank reaches it). Tests: `setAnimationMode` sets the mode
(restore `followSystem` afterwards); `setReducedMotionForTest` forces the OS's answer under Follow system.

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
| **Automation lane reorder** | Dragging a lane header within its track: the lane's block (row plus modulator rows) lifts under the pointer, the other lanes glide aside (160 ms, `easeOutCubic`), settle on drop (140 ms), Esc returns it; Cmd+Alt+Up/Down glides the moved block into its slot; headers only (the curve editors follow on commit); at once when not on screen; see [Reorder drag](#reorder-drag) | `TimelineAutomationLanes` via `ReorderDragSession` |
| **Dock tab, mixer column and timeline track reorder** | Undo/redo of a timeline track reorder glides the rows too (140 ms; see [Undo and redo glide](#undo-and-redo-glide)); lifted item follows the pointer; neighbours glide aside (160 ms, `easeOutCubic`); settle on drop (140 ms); Esc returns it (140 ms, `easeInCubic`) — frames only while a tween runs; see [Reorder drag](#reorder-drag) | `BottomDockComponent`, `MixerPanelComponent`, `TimelinePanelComponent` via `ReorderDragAnimator` |
| **Side pane open/close** | The pane's width tweens 160 ms `easeOutCubic` in, 110 ms `easeInCubic` out from the current width; content keeps its full width and is revealed; lands at once when not on screen; see [side pane](side-pane.md) | `SidePane` |
| **Zones list row drag** | The Mixer side pane's channel rows and group headings make room for a dragged row (160 ms, `easeOutCubic`) on the shared vertical `ReorderDragAnimator`; the drop only assigns a group | `MixerZonesPane` via `ReorderDragAnimator` |
| **Library rows** | Hover highlight; grab / dragging-hand cursor on draggable rows; per-module descriptions via `descriptionFor(name)` surfaced as `setTooltip()`; search-query highlight on matching labels (shared `drawSearchHighlightedText`, static fill, no animation) | `ModuleLibraryComponent` |
| **Preset-load feedback** | Status bar text updated during load; no spinner | `MainComponent` into `StatusBarComponent` |
| **AI request Cancel and spinner** | Cancel button visible while a request is in flight; pulsing "thinking" spinner, time-bounded — stops on completion or cancel, confined to its region | `AIChatComponent` |
| **Timeline playhead** | 30 Hz vertical position line, **playing only**, repainting only the strip between its old and new x | `TimelinePlayheadOverlay` |
| **Cursor glide** | While Cmd+Left / Cmd+Right is held, a VBlank frame per refresh moves the transport cursor (ease-in speed, capped); on release a 140 ms `easeOutCubic` settle lands it on the grid. Both are `ReorderFramePump` runs, so frames stop with the key; see [`docs/timeline/transport.md`](../timeline/transport.md#gliding-the-cursor) | `TimelineCursorGlide` via `TimelinePanelComponent` |
| **Zoom settle debounce** | `zoomSettleAnim`: a DEBOUNCE `AnimationDriver` (140 ms, `kZoomSettleMs`) with a no-op `onUpdate` — zero repaints while running, all the work in `onComplete`, which thaws the frozen card rasters | `GraphEditor` |
| **Card make-room / return / auto-arrange glide** | Cards moved by a make-room push, a macro slid in from the canvas edge, a neighbour return or Auto Arrange slide from the old to the new spot (160 ms, `easeOutCubic`): geometry is final at once, the real card is hidden (alpha 0) and a snapshot glides on a canvas overlay (a card whose whole path is off the visible canvas gets no snapshot and is not hidden, see [rendering](rendering.md#per-frame-work-does-not-grow-with-the-patch)); cables touching it follow; hulls and port strips glide separately (see the macro border glide row); undo/redo glide the cards back and forth, including a module taken out of or put into a macro (see [Undo and redo glide](#undo-and-redo-glide)); loads land at once; a retarget starts from the drawn position; frames only while it runs; see [Making room](layout.md#making-room-when-something-grows) | `CardGlideAnimator` via `GraphEditor` |
| **Macro-crossing cable slide + module flash (FRO41)** | Also on a cable drop that mints a macro port: the new cables' port ends emerge from the release point (no flash). When a drag crosses an expanded macro's hull (applied live, mid-drag, or on the drop): a cable re-routed through an auto-created/removed port slides to its new anchor (220 ms, `easeOutCubic`; endpoints are offset from their live anchors, so a slide started mid-drag stays attached to the moving card), and the dragged module gets a fading ring — pure tween state in `MacroCrossingAnimator` (`Source/UI/Graph/MacroCrossingAnimator/`), driven by `macroCrossingDriverAnim_`; see [`docs/macros/menu-and-membership.md#cable-crawl-and-module-flash-fro41`](../macros/menu-and-membership.md#cable-crawl-and-module-flash-fro41) | `GraphEditor` |
| **Macro border glide** | A macro's border glides to its new bounds instead of snapping (220 ms, `easeOutCubic`) on a live membership change, a drag candidate change, and when a drag ends and the held border is released, and on undo and redo of a membership change. The dashed outline, port strips, chip and buttons and docked port widgets glide together (the driver re-docks the widgets each frame); each edge is offset from the live border, so a border that keeps moving still lands on it. Pure state in `MacroHullGlide` (`Source/UI/Graph/MacroHullGlide/`), applied in `GraphEditor::paintedMacroHullBounds`; see [`docs/macros/menu-and-membership.md#macro-borders-glide`](../macros/menu-and-membership.md#macro-borders-glide) | `GraphEditor` |
| **Cable retract** | A removed cable retracts into its source jack and fades (180 ms, linear progress with an `easeInCubic` pull) instead of vanishing: a cable cut, a jack disconnect, a removed modulator, and undo/redo taking a cable away. The ghost paints after the live cables; project load and New do not animate. Pure state in `CableRetractAnimator` (`Source/UI/Graph/CableRetractAnimator/`), armed by `GraphEditor::retractCablesGoneSince`; see [cables](cables.md#retracting-removed-cables) | `GraphEditor` |
| **Colour picker (popup reveal)** | The callout fades in 160 ms `easeOutCubic` and slides 8 px out of the side facing the dot, swatch or chip that opened it; at once under macOS Reduce Motion or when not on screen; entrance only; see [Popup reveal](#popup-reveal) | `CalloutReveal` via `ColourPickerPopup` |
| **Shortcut hint bubbles** | One tween value `t` scales (0.6 -> 1), moves (from the labelled button's centre, or 12 px below a hidden-panel pill) and fades every Cmd-hold key-cap bubble: 160 ms `easeOutCubic` in, 110 ms `easeInCubic` back out from the current `t`; pure geometry in `hint::animatedBubbleBounds`; one `AnimationDriver`, no timer besides the 500 ms show delay; see [`docs/control/shortcuts.md`](../control/shortcuts.md#shortcut-hints) | `ShortcutHintOverlay` |
| **Toolbar buttons** | Three tweens per button, each retargeting from its current value on one `AnimationDriver` apiece: hover 110 ms `easeOutCubic` in / 90 ms `easeInCubic` out (chip 16 -> 26 percent, ground, caption colour), press 80 ms (the chip and glyph squash to 0.92 x 0.86 about the chip's centre), lit 160 ms in / 110 ms out (the chip fills with the group colour while the glyph cross-fades to its ink art). On hover the glyph lifts 1 px and its moving part (the SVG's `mv` / `mv2` group, drawn as a separate `Drawable`) does one small thing: the cog and Light mode turn 30 degrees, Undo -22 and Redo +22 (the arrow swings about its elbow with an ease-out-back overshoot of about 10 percent while the hover arrives, a plain return on leaving), Save's shutter slides 1.5 down, Load's arrow drops 2, New's plus grows 1.2x, the feedback lines slide 1 right, Auto Arrange's right tiles part 1.5 up and down, the minimap view moves (2, 1), the matrix grows 1.12x, the panel rises 1.5, the library's leaning book tips 10 degrees further (distances in icon units). The AI button is the one looping glyph: a pulse runs along its cable and a spark blooms at the plug over one 2.8 s cycle (a linear `AnimationDriver` that restarts itself, the pulse placed with `Path::getPointAlongPath`), while the assistant is working (`setBusy`, from `AIChatComponent::onWaitingChanged`) or the button is hovered; its keyframes cross-fade with the rest glyph 140 ms in / 220 ms out and the cycle stops once they are gone, so an idle button schedules no frames. Under Reduce Motion (read when a hover starts, and when the button turns busy) nothing moves and only the colours change; lands at once when not on screen; nothing runs at rest; see [toolbar buttons](chrome.md#toolbar-buttons) | `ToolbarButton`, painted by `AppLookAndFeel::drawToolbarButton` |
| **Velocity strip readout** | The value beside a hovered or dragged stick fades 160 ms `easeOutCubic` in / 110 ms `easeInCubic` out from the current opacity and slides ~4 px from the stick head to its spot (`velocitylane::readoutSlidePx`); stick-to-stick moves and edits keep the current opacity; lands at once when not on screen; one `AnimationDriver` | `PianoRollVelocityLane` |
| **Mod-dot tooltip** | The "LFO 1 · +42%" readout above-right of a knob whose landing dot is dragged (or stepped by key) fades 160 ms `easeOutCubic` in / 110 ms `easeInCubic` out from the current opacity; a plain 80 ms linear fade under Reduce Motion; lands at once when not on screen; painted on the canvas over the cards; one `AnimationDriver`; see [mod dot](../modules/modulation.md#the-mod-dot-drag-an-amount-from-the-landing-dot) | `ModDotTooltip` via `ModDotController` |
| **Mod-dot panel** | The dot's panel fades in as a popup window (`PopupMotion`, 160 ms in / 110 ms out) with a `popup_motion::Style` of its own: it slides 12 px out of the dot (not the pointer) with a 3% overshoot (`easeOutBackSoft`), and leaves 6 px back toward it on Esc, click-away or a pick; Reduce Motion is the plain 80 ms fade; a second click on the dot while it fades out cuts the fade short and the new panel takes its place (`ModDotPanelFrame::finishClosingNow`). "Add source" unfolds the source list under the rows in the same panel: the panel's height grows 160 ms `easeOutCubic` and folds back 110 ms `easeInCubic` (one driver), and near the bottom of the screen the panel slides up with it, frame by frame, while its arrow stays level with the dot. A group's fold turns its arrow 90 degrees and opens or closes its rows over 160 ms `easeOutCubic`, the rows below sliding (never jumping), one driver for every group at once; a new source row grows in from zero height (160 ms `easeOutCubic`) and a removed one shrinks out (110 ms `easeInCubic`) with the rows below sliding up; row and button hover is a 100 ms fade. The Pick on canvas layer only outlines the hovered card (static, no animation). Everything lands at once when not on screen or under Reduce Motion; frames only while a tween runs; see [mod dot menu](../modules/modulation.md#the-mod-dot-menu) | `ModDotPopover`, `ModDotPanelFrame`, `ModDotSourcesPage`, `ModDotAddSourcePage` |
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
