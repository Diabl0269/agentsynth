# Timeline Panel

`Source/UI/Timeline/TimelinePanelComponent/` — `synth::ui::TimelinePanelComponent`, a bottom-docked
panel owned by `MainComponent`. It holds the arrangement: a ruler across the top, a scrolling
column of track headers down the left, and the clip lanes (or the piano roll) filling the rest.

This doc is the shell — the regions, the dock, the height and the slide. The rest of the area:

| Doc | Covers |
|---|---|
| [view](view.md) | `TimelineViewState`: beat↔pixel mapping, snap, the lanes grid, wheel/zoom/scroll |
| [ruler](ruler.md) | The ruler strip, its two gesture zones, the loop brace, markers |
| [tracks](tracks.md) | Track header rows: binding chips, M/S/R, focus, reordering |
| [add-track](add-track.md) | The `"+ Track"` menu and every flow it starts |
| [playhead](playhead.md) | The playhead overlay, latency compensation, follow-playhead |
| [transport](transport.md) | The transport bar, recording, metronome and count-in |
| [edit-tools](edit-tools.md) | The Cubase-style tool strip shared by the lanes and the roll |
| [clips](clips.md) | Clip lanes: selection, drag/trim, authoring, import |
| [automation](automation.md) | The automation strip and its curve canvas |
| [piano-roll](piano-roll.md) | The per-clip MIDI note editor |
| [scale-assist](scale-assist.md) | The Scale Assist panel, the scale engine, random generation |
| [focus](focus.md) | Which surface Cmd+C/V/D/X/R and Cmd+Shift+A act on |

Source layout — one file per concern; the class itself is declared in `TimelinePanelComponent.h`:

| Unit | Concern |
|------|---------|
| `TimelinePanelComponent.cpp` | Ctor/dtor, shortcut-manager wiring, transport/doc/undo-manager setters |
| `TimelinePanelStrips.cpp` | Edit-tool strip, piano-roll open/close, automation strip |
| `TimelinePanelClipClipboard.cpp` | Clip clipboard: copy/paste/cut/duplicate/repeat/select-all |
| `TimelinePanelShortcuts.cpp` | Panel-scoped keyboard shortcut dispatch (`matchesAction`/`keyPressed`) |
| `TimelinePanelTrackHeaders.cpp` | Add-track menu, `timelineChanged`, header sync/layout, focus movement, drag-to-reorder |
| `TimelinePanelLayout.cpp` | Preferences (snap/follow-playhead/scroll-invert), zoom/scroll helpers, `resized()`/`paint()`, the `ResizeHandle` child component |

## Regions

The low-rate transport poll ([playhead](playhead.md#two-timers)) aside, everything here is pure
layout-plus-paint with no timer or animation of its own:

```
+====================================================================+  <- resize grab strip
| Transport bar strip  (play/stop/record/loop, BPM, time-sig, ruler   |  transport
| readout, metronome/count-in .......................... snap combo) |   (+ view's snap selector)
+---------------------+----------------------------------------------+
| "+ Track"            | Ruler  (bar/beat ticks, loop brace)          |  add-track (+ ruler)
| Track header column  +----------------------------------------------+
| (name/colour/M/S/R/  | Clip lanes  <-or->  Piano roll               |  tracks / clips <-> roll
|  binding chip),      | (one of the two, same rect, playhead overlay |
|  scrolls              | drawn on top of either)                     |  playhead
|                       +----------------------------------------------+
|                       | Automation strip (opens by shrinking the     |  automation (docked,
|                       |  region above by its own fixed height)       |   optional)
+---------------------+----------------------------------------------+
```

A themed background (`theme.colors.bg0`-family, the same fallback pattern as the toolbar,
status-bar and sidebar panels) and a thin top border separate the panel from the graph editor
above. `resized()` lays out three child regions:

- **Transport bar** (top strip, `Metrics::timelineTransportBarHeight`)
- **Track-header column** (left, `Metrics::timelineTrackHeaderWidth`)
- **Lanes/ruler area** (remainder)

All three are exposed as public rect getters (`getTransportBarBounds()`, `getTrackHeaderBounds()`,
`getLanesBounds()`) so every consumer and its tests build on the same arithmetic instead of
re-deriving it. The component owns no timer and no animation of its own, apart from the playhead
overlay's and the automation strip's knob entry point.

Keyboard focus is orthogonal to this diagram, not another region: whichever of the graph editor,
clip lanes or piano roll the user last clicked owns Cmd+C/V/D — see [focus](focus.md).

## Docking, toggle and the bottom dock

`MainComponent` carves the panel full-width, directly above the status bar: `resized()` — the one
geometry authority, see [`layout_visuals_animation.md` §3](../layout_visuals_animation.md)'s
`PanelSlide` subsection — removes it from the bottom AFTER the status bar and BEFORE the
AI-panel/library removals, so it spans the whole window width regardless of which side panels are
open. The height it removes is `timelineSlide_.sizeBetween(0, timelinePanelHeight_)`, i.e. the
user's height scaled by the panel's open fraction, so the same carve serves both the docked panel
and every frame of its slide.

A toolbar toggle (`ToolbarComponent::Slot::ToggleTimeline`, right-hand group, immediately before
`ToggleTheme`) and the **Cmd+T** shortcut (action id `toggleTimelinePanel`; see
[`shortcuts.md`](../shortcuts.md)) both flip `MainComponent::isTimelineVisible`. Visibility persists
under the `timelinePanelVisible` key in `juce::ApplicationProperties`, default `false`.

**The key and the flag gate the whole bottom dock, not just this panel.**
`TimelinePanelComponent` is nested inside `MixerDockComponent` (the Timeline/Mixer tab strip; see
[`mixer_implementation.md`](../mixer_implementation.md) §8), and
`mixerDock.setVisible(isTimelineVisible)` is what the toggle, the shortcut and the persisted key
actually drive. `isTimelineVisible` / `timelinePanelVisible` mean "the dock is open", regardless of
which tab is active; which of the two panels is *showing* inside an open dock is the separate,
independently persisted `bottomDockActiveTab` key (`MixerDockComponent::kActiveTabKey`, default
`"timeline"` — see `mixer_implementation.md` §8).

**Why the names stayed.** They match settings files already on disk. The cost is that a
component-local `TimelinePanelComponent::isVisible()` check does not tell you whether the dock is
open — it only reflects "the Timeline tab is selected" — so a caller that needs "is the panel
actually on screen" composes `timelinePanel.isVisible() && mixerDock.isVisible()`.
`MainComponent::timerCallback()`'s 10 Hz poll gate is the reference call site.

## Panel height

The panel's height is **not** fixed. `Metrics::timelinePanelHeight` (220) is the **default and the
minimum**:

- **`MainComponent` owns the value** (`timelinePanelHeight_`), and it is the only thing that lays
  the panel out. `resized()` — and therefore **every frame of the show/hide slide**, which is just
  `resized()` at a moving fraction — reads the member, never the metric directly. A height drag
  landing mid-slide needs no special case for the same reason.
- **Clamp rule**, applied in `MainComponent::clampTimelinePanelHeight()` on every layout pass, not
  only when the user drags: `[Metrics::timelinePanelHeight, max(metric, 75% of the window height)]`.
  Re-clamping per pass is what stops a height saved on a large window from swallowing a smaller
  window's canvas; on a window so short that 75% falls under the metric, the floor wins. Before the
  first layout (window height still 0) only the floor applies — otherwise construction would clamp
  a persisted height away against a window that does not exist yet.
- **Persistence**: the `timelinePanelHeight` int key (same name as the metric) in
  `juce::ApplicationProperties`, absent by default — absence is what makes the metric the default.
  Written **once per gesture**, on drag end, never per pixel.
- **The value is the total dock-carve height**, i.e. it includes `MixerDockComponent::
  kTabStripHeight` (22 px) on top of the timeline panel's own content height.
  `TimelinePanelComponent::ResizeHandle::desiredHeightFor()` stays agnostic of whatever chrome it
  sits inside and reports only its own desired *content* height; the `onResizeHeight` /
  `onResizeHeightCommitted` wiring in `MainComponentSetupTimeline.cpp` is the one seam that knows
  about both and adds the tab-strip height before calling `setTimelinePanelHeight()`. A height
  persisted before the dock existed is therefore honoured as a total-carve value unchanged.
- **The grab strip** (`TimelinePanelComponent::kResizeHandleHeight = 5`,
  `MouseCursor::UpDownResizeCursor`) spans the panel's full width along its top edge, *overlapping*
  the transport bar strip: `getTransportBarBounds()` still starts at `y == 0` (the three regions
  tile exactly), but the transport controls inside it are laid out below the strip, so a resize
  grab never lands on a transport button. Idle it paints exactly the hairline the panel already
  drew there; hovered or dragging it brightens to the accent colour with a faint wash, and it
  repaints **only on a hover-state change** (`docs/layout_visuals_animation.md` §2–3's repaint
  discipline).
- **The panel never resizes itself.** Dragging reports the *desired* height — measured absolutely,
  from the panel's pinned bottom edge in screen coordinates, so the owner moving the top edge under
  the cursor cannot make the gesture chase itself — through `onResizeHeight` (every drag step,
  unclamped) and `onResizeHeightCommitted` (once, on mouse-up: the cue to persist). The owner
  clamps, stores and re-runs its layout on each step, which is what makes the drag live. That is
  one user-driven layout pass per mouse event, not a free-running animation.

## The show/hide slide

The slide in and out is the **same** `PanelSlide` plus the one shared `AnimationDriver` the library
and AI panels use (`MainComponent::beginPanelSlide()`, ~190 ms ease-in-out-cubic, one shared
`VBlankAnimatorUpdater`) — no animator or timer of its own, and no timeline-specific animation code
at all: only the axis differs, and that lives in `resized()`'s carve order. `timelineSlide_`'s
fraction drives the height against a pinned bottom edge, so the panel grows upward into place and
shrinks back down the same way; `setVisible(false)` happens in `finishPanelSlide()`, once the slide
is actually done, same as the sibling panels. See `docs/layout_visuals_animation.md` §3 for the
full contract: mid-flight reversal, the synchronous off-screen path, and why one driver serves all
three panels.

## Always compiled, never gated

Everything past the always-present `TimelinePanelComponent` member and the `resized()` carve — the
toolbar button and the `toggleTimelinePanel` command included — is ordinary, always-compiled,
always-active code. There is no build configuration and no runtime preference that omits the
button, the shortcut or the panel carve, and Cmd+T and Space are always available. A stale
`timelineFeatureEnabled` key left in an existing install's `ApplicationProperties` by an earlier
kill switch is ignored.
