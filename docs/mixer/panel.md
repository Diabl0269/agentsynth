# Mixer Panel

The mixer's own surface: what it shows, where it lives, how it detaches into a window, the column
widgets it is built from, the EQ thumbnail, and keyboard navigation. What a channel *is* lives in
[`docs/mixer/mixer.md`](mixer.md).

---

## What the mixer shows

Strips, buses, Direct and Master. **Nothing else — never an arbitrary module's output.** To put
something in the mixer, make it a channel. A bus IS a strip
([`docs/mixer/sends-and-buses.md`](sends-and-buses.md#a-bus-is-a-channel-strip)), so "buses" here
names a badge and a source line, not a fourth column widget.

`MixerPanelComponent` renders one `MixerColumnComponent` per strip in track order, then strips with no
track by node id, then Direct, then Master. Each column holds a `MixerFader`
([`docs/mixer/fader.md`](fader.md)), a pan control, M and S buttons, a peak meter
([`docs/mixer/meters.md`](meters.md)), an insert list
([`docs/mixer/mixer.md`](mixer.md#inserts-in-a-free-form-graph)) and, on a source column, a compact
send list. **Master's column has the fader, mute button, meter and an insert list, and no pan, solo or
sends** — its list is the post-fader chain between Master and the Rec Tap or Audio Output
([`docs/mixer/mixer.md`](mixer.md#master-inserts)), sized and placed under the header exactly like a
strip's, and `MixerMasterColumn::setColumn()` feeds it from the snapshot. Its column model — ordering,
kinds and queries — is headless, in `Source/Mixer/MixerModel/`.

**Solo always routes through the engine.** The S button calls
`AudioEngine::setChannelStripSoloed`, never the module directly
([`docs/mixer/mixer.md`](mixer.md#solo-is-a-render-time-gate)).

**Every fader, pan and M or S gesture is exactly one undo step.** `MixerFader` binds its slider 1:1 to
the strip's `gain` parameter via `juce::SliderParameterAttachment` and brackets the gesture through
`parameterGestureChanged` to `AppUndoManager::captureBeforeState`/`pushSnapshotFromCapture`.

`Cmd+Alt+M` (`toggleMixerPanel`) opens the bottom dock on the Mixer tab, or closes it on a second
press when the dock is already open on Mixer — mirroring the Toggle Timeline button's own open and
close symmetry.

### Renaming a channel

Double-click a column's header name to rename it in place — `MixerColumnHeader`'s `nameLabel_` reuses
the exact `juce::Label(false, true, false)` + `onTextChange` pattern
`TimelineTrackHeaderComponent::nameLabel_` already established for track renaming, so the app's two
"double-click a name to rename it" surfaces behave alike. Direct and Master have no name to rename
(Direct has no node; Master is a `MasterModule`, not a `ChannelStripModule`) and turn the gesture off
outright (`MixerColumnHeader::setRenameEnabled(false)`).

For a strip or bus column, what a commit actually renames depends on whether the column is already
boxed in a macro:

- **Boxed in a macro** — the column's name already comes from the macro
  ([`docs/macros/macros.md`](../macros/macros.md)), so the rename goes to the macro
  (`MacroGroupController::renameMacro`), the same path a macro card's own rename uses (and which
  itself keeps a linked track's name in sync in the same undo step,
  [`docs/timeline/tracks.md`](../timeline/tracks.md#the-channel-chip)). The strip's own name (below)
  is never also written — one name per column, not two competing ones.
- **Not boxed** — the rename writes `ChannelStripModule`'s own persisted name (FRO225,
  `setStripName`/`getStripName`), serialized in the strip's trusted extra state
  (`"name"`) so it survives save/load and presets exactly like `"shape"`/`"solo"`/`"isBus"` do. Empty
  (unset, the default, and every strip created before FRO225) falls back to today's column-name rule
  — the track-walk name, or `"Channel N"`/`"Bus N"` — unchanged. A track preset's own capture
  (`TrackPresetManager::extractTrackPreset`) scrubs `"name"` the same way it scrubs `"solo"`/`"isBus"`/
  `"sends"`: the SOURCE strip's identity must never rename whatever channel the preset is applied to.

The strip's own name is also what `docs/mixer/stem-export.md#stem-naming` prefers ahead of its
track-walk rule, so renaming a channel here renames its stem file too.

## Placement and detachable windows

Panel placement is a **Preferences setting** with three options: **Tab beside the Timeline**
(default), **Own panel**, or **Window**.

**Why a tab as the default.** A tab beside the Timeline puts the mixer where the bottom dock already
is, with no second dock to learn and no window to manage; the other two stay available as settings for
users who want them.

Whichever is chosen, detaching a panel into its own window uses **one mechanism shared by Timeline
and Mixer, not two** — detaching the Timeline and detaching the Mixer are the same code path applied
to different panels, not two separate features. The reference implementation is the existing
hosted-plugin pattern, `Source/Plugin/Hosting/HostedPluginEditorWindow.{h,cpp}`, including its
`addToDesktop=false` construction path, which is **the headless-testable seam** that lets a window be
built and inspected in a test with no real desktop window ever created.

The detach control is **icon-only**, with a tooltip reading "Open in window" when docked and "Dock
back" when detached.

**In the plugin build a detached window sets its OWN `LookAndFeel` instance; it never calls
`Desktop::setDefaultLookAndFeel`**, which is process-global inside the host and would repaint
everything else the host owns (root `CLAUDE.md` / `Source/UI/CLAUDE.md` invariant). Keyboard focus is
scoped per window.

### The three placements

The mechanism is `Source/UI/Layout/DetachablePanelHost/` (`DetachablePanelHost` plus
`DetachedPanelWindow`): a slot that moves a panel **by reference** — never copied or rebuilt —
between its dock and a `DetachedPanelWindow`. `MixerDockComponent` owns three hosts,
`timelineHost_`/`mixerHost_`/`midiRemoteHost_`, each wrapping the SAME `TimelinePanelComponent`/
`MixerPanelComponent`/`MidiRemotePanelComponent` instance it already held, so scroll, zoom and
selection survive a detach untouched. `midiRemoteHost_` (FRO131) is always Tab placement — no
Own-panel/Window placement variant like Mixer's own, so it has no third row in the table below.

`synth::ui::MixerPlacementController` (the one collaborator `MainComponent.h` adds for this) moves
`mixerHost_` between its three homes:

| Placement | Mixer lives | Timeline dock | Detach state |
|---|---|---|---|
| Tab (default) | `MixerDockComponent`'s own tab strip | unaffected | tab-strip button |
| Own panel | `MixerPlacementController` itself, a second independent bottom strip below the Timeline dock; slides, resizable | unaffected | its own header (embedded=false) |
| Window | a `DetachedPanelWindow`, opened on first reveal, never eagerly at launch | unaffected | `mixerHost_` stays parented and hidden inside the dock until revealed |

**In Tab placement neither host draws its own header** (`setEmbeddedHeader(true)`): the dock's
22 px tab strip (`MixerDockComponent::kTabStripHeight`) carries a single icon-only detach button that
acts on whichever tab is active, and the header — with the real button, now reading "Dock back" —
appears only on the DETACHED window itself. That is a deliberate simplification over reparenting
either host's own button through three different parents.

**The dock resizes from every tab** (FRO231). One `synth::ui::PanelResizeHandle` lives on the
dock's own top edge — not inside the Timeline panel — so the Mixer and MIDI Remote tabs resize the
dock exactly like the Timeline does. It overlaps the top 5 px of the tab strip (the strip stays
22 px; the tab, detach, `+ Bus` and Reset Meters buttons are laid out below it, so a grab never
lands on a button) and reports the total dock height through `MixerDockComponent::onResizeHeight` /
`onResizeHeightCommitted`. The rules (clamp, persistence, live relayout) are in
[`docs/timeline/timeline.md`](../timeline/timeline.md#panel-height); Own-panel placement is a
separate strip with its own handle, below.

**"Own panel" slides, is resizable and remembers its height** (FRO231), all inside
`MixerPlacementController` so `MainComponent` holds no extra member for it:

- **Slide.** Toggling (`revealOrToggle()`) moves the strip's own `PanelSlide` fraction with the same
  190 ms ease as the dock (`docs/layout/animation.md`). It is visible for the whole slide, including
  the closing one, so `isOwnPanelShowing()` (meters, focus region) is true throughout. Choosing the
  placement in Preferences, or launching in it, snaps it open with no animation.
- **Height.** `mixerOwnPanelHeight` (absent = `kOwnPanelMinHeight` = 220, which is also the
  minimum), clamped to `[220, max(220, 3/4 of the window - what an open dock keeps)]`
  (`MixerPlacementController::clampHeight`). The stored value is the user's wish and is re-clamped
  at read time, never rewritten by a layout pass, so it comes back when the window grows or the
  dock closes. Persisted once per drag, on mouse-up, and never for a click that did not move.
- **Handle.** A `PanelResizeHandle` (`ownPanelResizeHandle`) on the strip's own top edge; the hosted
  panel with its header starts 5 px below it. Dragging calls `setOwnPanelHeight()` and asks
  `MainComponent` to lay out through `onLayoutNeeded`.
- **Sharing the window with the dock.** `MainComponent::resized()` carves the Own panel first (it owns
  the bottom edge), then gives the dock `min(its height, max(its minimum, 3/4 of the window - the Own
  panel's carve))` — local to the layout pass, the dock's stored/persisted height is untouched.

**Preference changes apply live.** `MixerPlacementController::applyPlacementPreference()` runs once at
launch (`MainComponent::wireTimelinePanel`) and again on every settings-file write
(`MainComponent::changeListenerCallback`'s existing `ChangeListener` path, the same one
`applyNaturalScrollingPreference` uses). **It is idempotent against its own current state**, so a
`DetachedPanelWindow`'s bounds-persist-on-drag — which fires that same broadcast — never does real
work.

`MainComponent::isTimelineVisible` and the persisted `timelinePanelVisible` key open and close the
whole dock, any of its tabs, while `MixerDockComponent`'s own `bottomDockActiveTab` key persists
which tab is showing (`"timeline"` default, or `"mixer"`/`"midiRemote"`) — see
[`docs/timeline/timeline.md`](../timeline/timeline.md#docking-toggle-and-the-bottom-dock).

### Per window keyboard focus

`MainComponent::keyPressed` is the sole Tab dispatch point and is unreachable from a separate
top-level window, so **`DetachedPanelWindow` resolves Tab and Shift+Tab itself** against its OWN
one-region `FocusRegionRegistry`, via the shared `synth::ui::resolveFocusCycleKeyPress()`
(`Source/UI/Layout/FocusRegion.h`) — the same action-id-to-direction translation `MainComponent`'s own
command table uses. Two independent registries never cross-resolve.

`MainComponent`'s own registry **drops a region while its panel is detached**:
`registerFocusRegions()` is split into a one-time `addFocusChangeListener` call plus
`rebuildFocusRegions()` (clear and re-add, wrapping the `"timeline"` `addRegion` call in
`!mixerDock.getTimelineHost().isDetached()`), re-run via
`MixerDockComponent::onPanelDetachStateChanged` after every detach and redock.

### Creating the native window

`DetachablePanelHost::setCreatesNativeWindows(bool)` decides whether a detach produces a real window:
**false is the default, and every headless test's value**, leaving `setDetached(true)` a pure
reparent; **true** — set once, right after construction, by `Main.cpp`'s `MainWindow` and
`PluginEditor.cpp`'s `AgentSynthPluginEditor`, the app's and plugin's only real `MainComponent`
construction sites — makes `setDetached(true)` call `window_->addToDesktop()`, additionally gated on a
primary display existing for a genuinely headless runner, **before** `setVisible(true)`.

**Why the explicit `addToDesktop()` call is required.** JUCE creates a native peer from a
`TopLevelWindow` **only** via its own constructor's `addToDesktop=true`, via
`recreateDesktopWindow()`/`lookAndFeelChanged()` when a peer already exists, or from an explicit
`addToDesktop()` call — **never from `setVisible()` alone**. Calling only
`setVisible(true)` plus `toFront(true)` therefore removed the panel from the dock and put no window on
screen at all, while clicking again redocked correctly, because the redock path never touches the
desktop. `DetachedPanelWindow`'s already-restored or centred-default bounds survive the promotion
unchanged, since `Component::addToDesktop()` positions the peer from the component's current bounds,
never a native default.

The hosted-plugin editor window carries the identical rule:
`HostedPluginWindowManager::setCreatesNativeWindows(bool)`, false by default and in every headless
test, true from the same two real construction sites, making `openEditorFor()` call
`window->addToDesktop()` before `setVisible(true)`.

### Restored window bounds

**`DetachedPanelWindow::restoreBoundsOrDefault()` runs the parsed rect through
`isPlausibleRestoredBounds()` before trusting it**: reject, and fall back to the centred-default path,
when the rect is smaller than a plausible real window (under 320x240) or does not intersect any
currently connected display — skipped on a genuinely headless runner with zero displays, where there
is nothing to validate placement against.

**Why this guard exists.** A headless test that detached a panel against a REAL `MainComponent`, with
no `ApplicationProperties` override, wrote `persistBounds()`'s bounds straight into the real, on-disk
`Agent Synth.settings` file every shipped build reads. That let a 128x128 rect — JUCE's own
`ComponentBoundsConstrainer` default minimum, never a size a real drag produces — leak into
`mixerWindowBounds`/`timelineWindowBounds`, and the next real launch restored it verbatim: a Mixer or
Timeline window pinned to the screen edge at 128x128 instead of the centred default.

**The matching test-side rule: every test that builds a real `MainComponent` and detaches a real
panel wraps itself in a `PersistedKeysGuard` for the affected keys** — the same
snapshot-and-restore idiom `E2EWorkflowTests.cpp`/`FocusArbitrationTestFixture.h` already use for
other real-settings-file keys — so a test run can no longer change a developer's or CI's actual
persisted window geometry.

### A detached window follows the theme

`DetachedPanelWindow::lookAndFeelChanged()` calls
`setBackgroundColour(lf->getTheme().colors.surface)` whenever `getLookAndFeel()` resolves to a real
`synth::theme::AppLookAndFeel` (the same `dynamic_cast` idiom `DetachablePanelHost::applyIcon()`
uses) — the same `surface` token the mixer's own column and insert-list `paint()` overrides read.
`setLookAndFeel()` fires this synchronously, so it applies at construction, on any later theme swap,
and once more harmlessly as the destructor clears it. Passing `juce::Colours::darkgrey` to
`juce::DocumentWindow`'s background argument unconditionally instead showed flat stock-JUCE grey in
any area the hosted panel does not paint, such as an empty Mixer.

**`Content` needs no `paint()` override of its own**, because
`ResizableWindow::setBackgroundColour` fills the whole window behind `Content`, so a hosted panel's
unpainted area already falls through to the corrected colour.

## Unbinding before a graph change

**A UI object holding a raw pointer into one graph node must let go of it before that node's processor
is freed.** Every graph-replacing mutation funnels through
`GraphEditor::detachAllModuleComponents()`, which fires `onBeforeDetachAllModuleComponents` at its
top; `MainComponent` wires that to `MixerPanelComponent::unbindAllColumns()`, which unbinds every
strip column's and Master's fader, pan, mute, solo, meter and EQ thumbnail and clears their raw
pointers **without destroying anything** (`MixerColumnComponent::unbindFromGraph()` /
`MixerMasterColumn::unbindFromGraph()`, both idempotent and null-safe, like `MixerFader::unbind()`).
`~MainComponent()`'s own `detachAllModuleComponents()` call, already ordered before
`audioEngine.shutdown()`, covers the same teardown hazard for free.

**FRO133 (right-click MIDI Learn on the mixer, [`docs/control/midi-remote-ui.md`](../control/midi-remote-ui.md#right-click-midi-learn--coverage))
adds one more thing to this list.** Both `unbindFromGraph()` methods above also clear a small MIDI
Learn registry (`MixerColumnComponent`'s own `midiLearnableEntries_`, `MixerMasterColumn`'s own
`midiLearnableFaderParam_`) — each entry's `param` is the exact same kind of raw
`juce::RangedAudioParameter*` into a graph node that the fader/pan/mute bindings above exist to
protect, just read by the right-click menu and the mapped-badge paint instead of a
`SliderParameterAttachment`. Rebuilt by the next `rebindControls()`/`setNodeId()`, same lifecycle as
everything else this section covers.

**Why a pre-restore hook rather than relying on the rebuild.** A graph-structural undo or redo, New
Patch, Open, or an AI patch apply freezes the affected `ChannelStripModule`/`MasterModule` nodes'
parameters, and `MixerPanelComponent::rebuild()` is reached from the AFTER-restore hook
(`reconcileTimelineAfterGraphChange()`) — so the stale column was destroyed *after* the restore had
already freed what it pointed at, and `~MixerFader` to `unbind()` to
`AudioProcessorParameter::removeListener()` ran on freed memory. That surfaced as a Linux CI hang:
exit 124 plus SIGABRT, a deadlock inside `CriticalSection::enter` on freed memory, with macOS and
Windows passing only by luck.

**Every single-node removal path needs the same seam, or a liveness check.**
`GraphEditor::deleteSelection()` (a canvas Delete, `deleteMacroAndMembers`),
`requestDeleteModule()` (a card's own delete button and its "Delete Module" item) and
`replaceModule()` (the "Replace with..." submenu, offered for every module except the singleton Audio
Input and Output — so it reaches a `ChannelStripModule`, and a `MasterModule`, which is deliberately
kept out of every collapsed macro and is therefore always individually addressable) all fire
`onBeforeDetachAllModuleComponents` immediately before their `graph.removeNode()`.

**A live single-insert removal from the mixer's own row menu is a different path.**
`MixerInsertList::removeRow()` calls `graph.removeNode()` directly — synchronous, freeing the
processor immediately — and only *afterwards* does that mutation's `onMutated` bubble into
`MixerPanelComponent::rebuild()`, which is what would destroy the column's thumbnail: too late to
save it from a stale pointer, and `onBeforeDetachAllModuleComponents` never fires for this path at
all. `MixerInsertList::onBeforeNodeRemoved` (fired from `removeRow()`, with the node about to be
freed, before `graph.removeNode()`) is the caller-side hook, and `MixerColumnComponent` wires it to
unbind the thumbnail whenever the node being removed is the one it is bound to.

**Belt and braces, because chasing every present and future call site is not a strategy.**
`setEqModule()` also takes the owning graph and `NodeID` (`MixerColumnComponent` always has both), and
`detachListeners()` checks the node is still actually in the graph before touching the module at all —
if it is already gone, its parameters died with it and there is nothing left to call
`removeListener()` on. That check is what keeps the thumbnail safe on a call site nobody has hooked
yet.

**`MixerFader::parameterValueChanged` must not capture a raw `this` in a
`MessageManager::callAsync` lambda** — a queued callback can fire after the fader is destroyed by a
graph rebuild or a panel rebuild. It uses a `juce::Component::SafePointer`, this codebase's standing
convention for the pattern.

## The EQ curve thumbnail

When a strip's insert chain contains a Parametric EQ, its column shows a small frequency-response
curve (Cubase's top-mixer-row idiom) — **computed from the EQ's own parameters, never audio**, and
cached so a busy graph never pays for an unconditional per-tick repaint (root `CLAUDE.md`). A bypassed
EQ draws dimmed; a strip with no EQ insert shows no thumbnail; **a strip with two shows the thumbnail
for only the first one in signal order**, Cubase's own single-slot idiom and a deliberate trim.

- **Curve maths** — `synth::ui::EqResponseCurve::compute` (`Source/UI/Mixer/EqResponseCurve.h`), 48
  log-spaced points over plus and minus 18 dB, built the way `EQCurveComponent::recomputeMagnitudes()`
  does: snapshot the bands and output gain once, then sample `ParametricEQModule::responseDb`. **No
  second approximation of the DSP** — that header's own comment forbids one.
- **Threading** — `synth::ui::MixerEqThumbnail` registers as a
  `juce::AudioProcessorParameter::Listener` on every one of the bound EQ's parameters.
  `parameterValueChanged` can arrive on **any** thread (a CV-modulated bell writes its resolved value
  from the audio thread), so it only calls the allocation-free, coalescing `juce::AsyncUpdater`; the
  recompute and repaint happen once, on the message thread, in `handleAsyncUpdate()` — the same
  thread-hop `HostedPluginModule::InstanceListener` uses for `audioProcessorChanged`. `paint()` reads
  only the cached magnitudes and bypass flag, never the module, so "no unconditional per-tick repaint"
  holds by construction: there is no `Timer`.
- **Destruction and every rebind cancel the pending update and detach every listener** before the
  module pointer can go stale, the same ordering `MixerFader::unbind()` uses.
- **Finding the EQ** — `MixerColumnComponent::setColumn()` walks `column.inserts` (already signal
  ordered) and binds the first `ParametricEQModule` it finds.
- **Click** — forwarded through the column's existing `onEditOnCanvas` seam with the EQ node's own
  uuid, so `MixerPanelComponent::selectOnCanvas` resolves it exactly like a column header click or the
  insert list's own "Edit on canvas" link: macro id, then `findByMember`, else the bare node. No new
  canvas plumbing.

## Keyboard navigation and accessibility

The mixer panel is its own keyboard focus region, registered as the app's seventh region and sharing
the bottom dock with `"timeline"`. `MixerPanelComponent` is the region's focusable leaf
(`setWantsKeyboardFocus(true)`) and **every child control inside a column gives focus back up** — the
same focus-trap avoidance `TimelineTrackHeaderComponent` uses, one level higher, since a column hosts
several controls.

`MixerPanelKeyboard.cpp` (`Source/UI/Mixer/MixerPanelComponent/`) owns `keyPressed()`:

- **Left and Right** walk `focusedColumnIndex_` across strips, Direct and Master, clamped, never
  wrapping.
- **Up and Down** nudge the focused fader 1.0 dB, or 0.1 dB with Shift, as one undo step via
  `MixerFader::nudge()` (begin and end change gesture bracketing a single `setValueNotifyingHost`,
  like a real drag).
- **Enter** selects the focused column's macro or node on canvas.
- **M, S and R** resolve the SAME rebindable `timelineMuteFocusedTrack`,
  `timelineSoloFocusedTrack` and `timelineArmFocusedTrack` action ids the Timeline track-header row
  binds — **deliberately, because a new id would not inherit an existing rebind** — through
  `MixerColumnComponent::toggleMuted()`/`toggleSoloed()`/`MixerMasterColumn::toggleMuted()`,
  extracted from the M and S buttons' own `onClick` so a keypress and a real click can never diverge,
  plus an `onArmTrack` callback routed through `MainComponent::performTrackEdit`.

**Focus re-resolves after every `rebuild()` by the column's own identity** — a strip's uuid, Direct and
Master by kind — rather than by raw index, so an unrelated strip insert or removal elsewhere in the
column order never silently reattaches focus to the wrong column; a deleted focused strip clears focus
instead.

**The region registration is a free helper, not an inlined lambda.**
`registerMixerFocusRegion(FocusRegionRegistry&, MixerDockComponent&, std::function<bool()> dockOpen)`
(`MixerFocusRegion.h`, beside `MixerPanelComponent.h`) is called by
`MainComponent::registerFocusRegions()` with `dockOpen` reading `isTimelineVisible`, so a detached
mixer window can call the SAME helper against its own registry with a different `dockOpen` — for
example always-open — instead of re-deriving the logic. A null `dockOpen` is treated as always open.

**`AccessibilityHandler` support.** `MixerFader`'s `textFromValueFunction` speaks "-3.0 dB"; the pan
slider's speaks "50% left", "Center" or "50% right"; `MixerMeter::createAccessibilityHandler()`
reports a read-only `staticText` value (the displayed level as a percentage);
`MixerColumnComponent::createAccessibilityHandler()` returns a `group` role titled with the channel
name. **The M and S buttons are built with `setClickingTogglesState(false)` and mirror
`isMuted()`/`isSoloed()` into `setToggleState()`**, so both their own paint and the stock
toggle-button accessibility role track reality.

**The Left and Right walk moves REAL accessibility focus, not just the visual outline.** Each column
kind exposes `grabAccessibilityFocus()` — a strip's or Master's own fader slider; Direct has no fader,
so it targets the column itself with `setTitle("Direct")` — and
`MixerPanelComponent::setFocusedColumnIndex()` calls it via `AccessibilityHandler::grabFocus()`, on
**real user navigation only, never on `rebuild()`'s focus-preserving path**. That call only asks the
underlying `Component` for real keyboard focus when the component itself wants it, which every mixer
child deliberately does not, so a screen reader's cursor moves without stealing the panel's own real
keyboard focus.

The full key table, the region's open predicate and the accessibility handler details live in
[`docs/control/shortcuts.md#mixer-column-navigation`](../control/shortcuts.md#mixer-column-navigation).

## Related

- [`docs/mixer/mixer.md`](mixer.md) — what a channel is and how one is built.
- [`docs/mixer/meters.md`](meters.md) — the meters this panel polls.
- [`docs/mixer/fader.md`](fader.md) — the fader's taper and drag conventions.
- [`docs/mixer/sends-and-buses.md`](sends-and-buses.md) — the send list and bus columns.
- [`docs/timeline/timeline.md`](../timeline/timeline.md#docking-toggle-and-the-bottom-dock) — the
  bottom dock the Mixer tab shares.
- [`docs/control/midi-remote-ui.md#the-midi-remote-panel`](../control/midi-remote-ui.md#the-midi-remote-panel)
  — the dock's third tab (FRO131), same Tab-only placement as Mixer's own Tab row above.
- [`docs/layout/rendering.md`](../layout/rendering.md) — the no-unconditional-repaint rule.
- [`docs/layout/theming.md`](../layout/theming.md) — the theme tokens a detached window reads.
