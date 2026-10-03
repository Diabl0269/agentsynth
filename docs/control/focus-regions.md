# Focus regions

How Tab/Shift+Tab move keyboard focus between the app's areas, and the rules each area follows once
it holds focus. The key bindings themselves are listed in [`shortcuts.md`](shortcuts.md#general).

A general-purpose keyboard focus-region framework (`Source/UI/Layout/FocusRegion.h`) provides the
base for arrow-key navigation within the module library and for Up/Down + M/S/R within timeline
track header rows ([`shortcuts.md`](shortcuts.md#library-keyboard-navigation), [Timeline](shortcuts.md#timeline)); both build on top of it without changing the registry itself. A
`synth::ui::FocusRegionRegistry` is a plain member of `MainComponent` (never a `Desktop`-global
singleton — a host process can run multiple plugin instances, and a future separate-window
mixer/timeline would need its own registry), populated with eleven regions once every root component
exists: **Toolbar** (always open — the top strip), **Library** (`isLibraryVisible`), **Canvas**
(always open — the `graphEditor`), **Dock tabs** (`isBottomDockVisible` with at least one docked tab; the
dock's tab strip, ahead of the panel it selects), **Timeline** (`isBottomDockVisible && !bottomDock.isMixerTabActive() && !bottomDock.isMidiRemoteTabActive()`),
**Routing pane** (`timelineRouting`: the Timeline showing with its side pane open) and **Scale pane** (`scaleAssist`: the Timeline showing with the piano roll's scale panel on screen), both straight after Timeline and with no `open` callback, **Mixer** (`isBottomDockVisible && bottomDock.isMixerTabActive()`, no `open` callback — like Mod
Matrix, no direct-focus shortcut targets it), **Controllers** (`isBottomDockVisible &&
bottomDock.isMidiRemoteTabActive()`, FRO131 — same dock-tab shape as Mixer, but does take a direct
`open` callback), **AI Panel** (`isAiPanelVisible`) and **Mod Matrix**
(`graphEditor.isModMatrixVisible()`). Timeline, Mixer and Controllers share one dock
(`BottomDockComponent`) with one tab visible at a time, so `isBottomDockVisible` alone (the dock's own
open/closed state) stopped being enough to say the Timeline region is on screen the moment a second
tab exists — each region's `isOpen` also checks which of the dock's tabs is active, and each
region's `open` re-selects its own tab before falling through to the same "open the dock if it's
closed" step every panel toggle already does. See [**Mixer column navigation**](shortcuts.md#mixer-column-navigation)
below for the Mixer region's own keyboard behaviour.

- **Tab / Shift+Tab cycle OPEN regions only** — a closed region is skipped entirely, never opened,
  by the cycle itself (`FocusRegionRegistry::cycleFocus`/`nextOpenRegionId`). Suppressed completely
  (both actions report inactive, so the key falls through unhandled) while the launch welcome screen
  overlay (`welcomeScreen_`) is on screen — every region it would cycle to is sitting behind it.
- **Cmd+Shift+T/L OPEN a closed target first, then focus it** — the opposite rule from Tab-cycling,
  and deliberate: a direct-focus shortcut is a request to go somewhere specific, so it is allowed to
  get you there even if that panel was closed; Tab-cycling only ever moves between what's already on
  screen. Neither is suppressed by the welcome screen.
- **Cmd+Shift+M is deliberately NOT used for a Library shortcut** — `Cmd+M` already owns "Toggle Mod
  Matrix", and the chord instead went to "Locate Master" (Graph category — see [**Locate Master
  **](shortcuts.md#locate-master)), the same "find the mix bus" need this reservation was
  originally held for, ahead of the eventual mixer panel.
- **Every region root explicitly wants keyboard focus** — each of the six roots calls
  `setWantsKeyboardFocus(true)` in its constructor, so `grabKeyboardFocus()` always lands
  deterministically on the root itself. Without this, JUCE would instead descend into whichever
  child happens to sort first by Y/X position (not by which child actually wants focus) — fragile to
  depend on for a container whose row layout can change, and liable to silently focus nothing at all
  if that positional chain ends at a non-focusable leaf. One consequence worth calling out: Cmd+Shift+L
  lands on the Library container itself, not the search box, and Cmd+Shift+T lands on the Timeline
  panel root rather than the clip lane area — `resolveEditSurface()` (below) still reports `Graph`
  immediately afterwards, so Cmd+C still acts on the canvas until the user clicks into the clip lanes
  specifically. A bare **Down** on the panel root DOES seed keyboard focus into the track-header
  column (the track-header focus work, below) — the one direct keyboard path out of the region root
  this adds; reaching the clip lanes themselves by keyboard alone stays out of scope (the locked
  "track headers only" decision). The **+ Track** button is the last stop of that column: Down on the last
  row moves focus to it (with no tracks, Down on the region root lands on it directly), Up returns to the
  last row, and Return/Space press it (`TimelinePanelComponent::handleAddTrackButtonKey`). It shows the
  standard button focus ring.
- **Mod Matrix nests inside Canvas** — `ModMatrixComponent` is a child component of `GraphEditor`,
  so the two focus regions nest rather than sit side by side. `FocusRegionRegistry::regionContaining`
  resolves this to the most specific match (Mod Matrix, not Canvas) whenever real focus sits inside
  it, so Tab-cycling and the outline both track the right one.
  Inside it the arrow keys move between its controls (Left/Right in reading order, Up/Down by column;
  the amount slider keeps Up/Down to nudge the amount), see
  [`layout/chrome.md`](../layout/chrome.md#mod-matrix-panel).
- **A detached window ([`docs/mixer/panel.md`](../mixer/panel.md)) cycles only its OWN regions** — the
  Timeline or Mixer panel, popped out into its own `DetachedPanelWindow`, owns a SEPARATE
  `FocusRegionRegistry` with exactly one region (its hosted panel); Tab/Shift+Tab there resolves via
  the same shared `synth::ui::resolveFocusCycleKeyPress()` translation this app's command table
  uses, so the SAME bound key does the same thing everywhere, but the cycle itself never crosses
  into MainComponent's own registry or vice versa. No new binding — Tab/Shift+Tab stay exactly what
  they already are. While a panel is detached, MainComponent's own registry drops that region
  entirely (re-added the moment it redocks), so Tab-cycling in the MAIN window never lands on
  something that isn't there.
- **Visual indicator** — a region's root component paints a translucent outline (55% alpha) in the
  theme's `accent` colour (`docs/layout/theming.md`), at the theme's normal border weight (1px in every
  built-in theme, `theme.metrics.borderWidth`) rather than an arbitrary heavier one, whenever it or a
  descendant holds keyboard focus (`hasKeyboardFocus(true)`) — deliberately softer than the same
  `accent`-outline treatment on a small control (ComboBox/TextEditor), since a hard-edged fully-opaque
  rect around an entire panel reads as much heavier than the identical treatment on a button. Painted
  via the shared `synth::ui::paintFocusRegionOutline` helper every one of the six roots calls from its
  own `paintOverChildren` override — never plain `paint()`, since each root's children (module images,
  the ruler/clip-lane/transport tiling, the chat message view, the mod-row viewport) fill wall-to-wall
  to the root's own edge and would otherwise paint over a border drawn earlier in `paint()` (the
  Toolbar has no children of its own — buttons are direct children of `MainComponent` — so this makes
  no practical difference there, but it uses the same override for consistency). GraphEditor also
  skips its own outline while the nested Mod Matrix has focus, so the two regions never double-paint.
  Nothing repaints on its own when focus moves (`Component::focusGained`/`focusLost` are no-op
  virtuals for most widgets), so `MainComponent` is a `juce::FocusChangeListener` and repaints every
  region root on `globalFocusChanged` — event-driven, never a per-tick timer.
  The Dock tabs region draws no outline: its indicator is the focus ring on the selected tab.
- **Arrow keys inside the Toolbar and the dock's tab strip** — both are plain focus-region stops with a
  small roving selection inside, surface-local like the mixer's column navigation (their own `keyPressed`,
  no `ShortcutManager` actions): in the **Toolbar**, Left/Right move the ring across the visible, enabled
  buttons (no wrap), Home/End jump to the ends, Space/Return press the ringed button; in the **Dock tabs**
  region, Left/Right select the previous/next tab (which switches the panel), Home/End the first/last, and
  Return moves focus into the selected panel, and Down does too and then takes the panel's first Down step (on the Timeline that lands on `"+ Track"`) (the key rule is shared with the Settings tabs, `tabStripKeyTarget`).
  Cmd+T, when it opens the pane, and Cmd+1/2/3, for a docked tab, focus this region; Cmd+T closing the pane
  around the focus focuses the Canvas region instead. Modified keys fall through, so Cmd+1/2/3 and Tab behave as
  before. Details: [`layout/chrome.md`](../layout/chrome.md#toolbar-keyboard-access) and
  [`layout/chrome.md`](../layout/chrome.md#tab-strip-keyboard-and-screen-reader-access).
- **The Timeline's two side panes** — Tab goes track list, routing pane, scale pane, each only while it is on screen
  (Shift+Tab reverses), like any region. Each pane root takes focus and draws the panel outline (the Timeline's own
  outline stands down while focus is in a pane). Inside a pane, Down from the root enters the first control and Up/Down
  step between controls in on-screen order (no wrap; Up off the first control returns to the root); a combo box or text
  field keeps its own Up/Down, and Escape from any control returns to the pane root. The rule lives in
  `Source/UI/Layout/FocusStepWithin.h` (`PaneKeys`). The track list's own Up/Down walk, lanes included, is
  in [`timeline/automation.md`](../timeline/automation.md#keyboard-order).
- **Arrow keys on the Canvas** — the canvas region moves between module cards with the arrows, moves the
  selected cards with Alt+arrows and steps into a card with Return. Unlike the Toolbar and the dock tabs these
  are rebindable Graph actions; see [**Canvas card keys**](shortcuts.md#canvas-card-keys) below.
- **The track-header row outline is a second, per-row instance of the SAME visual language, not a variant** —
  `TimelineTrackHeaderComponent::paintOverChildren` calls `paintFocusRegionOutline` on itself exactly
  the way each region root's own override does, just one nesting level deeper (a row is not a region
  root; the Timeline region root stays the panel). Both outlines CAN paint at once (the panel's own
  softer region border, plus the focused row's identical treatment around just that row) — deliberate,
  the same double-outline already ships for a focused row inside the Library region.
