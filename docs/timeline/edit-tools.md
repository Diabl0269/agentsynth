# Timeline Edit Tools

`Source/UI/Timeline/EditTool.h` declares the Cubase-style tool row shared by the clip lanes
([clips](clips.md#edit-tools)) and the piano roll ([piano-roll](piano-roll.md#edit-tools)):

```cpp
enum class EditTool { Select, Range, Split, Glue, Erase, Mute, Draw };
```

plus `kAllEditTools` (an `std::array<EditTool, 7>` in that order), `editToolKeyDigit(tool)` and
`editToolName(tool)`. It is deliberately JUCE-free — gesture routing in both editors and the
strip's button wiring all switch on it, and `editToolForKeyChar(int keyChar)` is what
`TimelinePanelComponent::keyPressed()` consults for the number-key mapping, so tool switching is
testable with no UI at all.

## One active tool, owned by the panel

The clip lanes and the piano roll share the same lanes rect and only one is ever visible, so a tool
row that changed meaning depending on which editor happened to be showing would be a trap.

`TimelinePanelComponent::setActiveTool(EditTool)` pushes the tool into **both**
`TimelineClipLaneArea::setActiveTool` and `PianoRollComponent::setActiveTool` unconditionally, and
lights the matching strip button. Strip buttons are set with `dontSendNotification` explicitly
rather than via the radio group, since this method is also reached from a number key or from a test
— no button was necessarily clicked. Number keys and clicking a button are the only two ways a user
reaches it.

Switching tools **cancels** whatever gesture or preview is already in flight in either editor
rather than trying to reinterpret it under the new tool: a half-finished drag has no meaning under
a different tool.

## Numbering

Numbering follows Cubase, so the muscle memory transfers: **1** Select, **2** Range, **3** Split,
**4** Glue, **5** Erase, **7** Mute, **8** Draw.

**6** (Zoom) and **9** (Play/Scrub) are Cubase tools this app does not ship, and the gaps are
reserved **on purpose**: `editToolForKeyChar` returns `std::nullopt` for them rather than clamping
to a shipped tool, so `TimelinePanelComponent::keyPressed()` leaves those two digits unconsumed and
whatever they mean elsewhere is untouched. Shipping one later costs no rebind — the digit is already
reserved for exactly that tool, which is how **Range** took its **2** without moving any other key.
The enumerator order is the strip's order and indexes per-tool arrays; nothing persists it, so
inserting `Range` after `Select` renumbered nothing on disk.

**The piano roll takes Range as Select.** A range is a beat span across tracks and the roll edits
one clip, so `PianoRollComponent::setActiveTool` maps `Range` to `Select` (its `getActiveTool()`
then reports `Select` while the panel's says `Range`) — opening a clip with Range active leaves its
notes editable rather than silently inert. What Range does in the lanes is in
[clips](clips.md#range-tool).

## Rebinding

**All seven tool digits are rebindable**, unlike the reserved 6/9 gaps above, which are not
bindings at all. Each digit is a `ShortcutManager` action (`timelineToolSelect`,
`timelineToolSplit`, …, Timeline category) resolved directly by
`TimelinePanelComponent::keyPressed()` — a *surface* action, never dispatched through
`ApplicationCommandManager`.

So with a manager installed, an unbound tool digit has no key at all, and a rebind takes effect
immediately with no risk of colliding with the Ctrl+Shift+digit grid-set commands
([view](view.md#keyboard-zoom-and-grid-commands)): the two live in the same category, but modifier
equality is exact, so a bare digit can never match a Ctrl+Shift one. Only a build with NO manager
installed — headless tests, an embedding with no settings store — falls back to the hardcoded
digits above via `editToolForKeyChar`. See
[`shortcuts.md`](../control/shortcuts.md#command-vs-surface-actions) for the full command-vs-surface split
and the tripwire test that guards it.

The piano roll deliberately does not handle the digits at all: that binding belongs to the panel,
so the roll and the panel can never disagree about which tool is active.

## The strip

Seven `juce::DrawableButton`s (`ImageOnButtonBackground`), built unconditionally in
`TimelinePanelComponent`'s constructor — a headless build simply has no icon to draw in them, and
`getToolButton(tool)` is never null once the panel exists. One shared radio group id means clicking
one un-toggles the rest, and each carries a tooltip with the digit (`"Split (3)"`, from
`editToolName(tool) + " (" + editToolKeyDigit(tool) + ")"`). `kEditToolButtonWidth` is 28 px.

Laid out left-to-right in `kAllEditTools` order (1, 2, 3, 4, 5, 7, 8) immediately left of the snap
combo and toggle in the transport bar: both are "how the next edit behaves" chrome, so they read as
one group without pushing the transport controls off their left-aligned home.

`applyToolStripTheme()` (constructor plus `lookAndFeelChanged()`) re-applies each icon from
`AppLookAndFeel::getIcon` and sets the active-tool highlight as a **background colour**
(`colors.toolActive`), not a different icon tint — the glyph reads the same lit or not; see
[`layout/icons.md`](../layout/icons.md). Null-guarded on both a headless LnF and a headless icon
library.

## The curve selector

A compact `LaneGlyphButton` (`Source/UI/Timeline/TrackAutomationLanes/`) immediately right of the
Draw button, shown only while `EditTool::Draw` is active (its slot is reserved either way, so
switching tools never shifts the buttons around it). It paints the current curve as a drawn
`juce::Path` glyph; a click opens a popup of all eight — Freehand, Line, Sine, Triangle, Square, Saw
Up, Saw Down, Random — each with its own glyph image and a tick on the current one
(`TrackAutomationLanes::applyCurveMenuChoice(menuId)`, menu id = 1 + index, is the headless hook).

It shapes what Draw does on **track automation lane rows** only
([track-automation](track-automation.md#tools)): Freehand → the lane editor's Pencil, Line → Line,
a waveform → Shape of that kind. Clips are unaffected, and the bottom automation strip keeps its own
tool row. Beside it sit the toolbar's other two automation controls: the global-automation strip
toggle (with a lane-count badge) and "automation follows clips"
([track-automation](track-automation.md#automation-follows-events)).

## Tool cursors

`Source/UI/Timeline/ToolCursors.h`'s `makeToolCursor(EditTool, const juce::Drawable*)` renders the
SAME already-tinted `Icon::Tool*` drawable the strip button paints into a 24×24 `juce::Image` and
wraps it in a `juce::MouseCursor`, so the cursor can never drift out of sync with whatever theme is
active — there is no separate cursor-only asset or tint step to go stale.

Hotspots are not uniform. **Select** hotspots at the arrow's tip `(4, 2)` and **Draw** at the
pencil's tip `(3, 21)`, because both icons have an obvious off-centre working point, the way every
DAW places a click point there. **Range/Split/Glue/Erase/Mute** hotspot at the icon's geometric
centre `(12, 12)`: a scissors' cut happens where the blades cross, a range starts, and glue, erase
and mute act, at whatever is directly under the pointer, so there is no other candidate point.

Headless-safe by construction: a null icon (asset library not linked in) falls back to a stock
cursor per tool — `NormalCursor` for Select, `IBeamCursor` for Range (a span select, like selecting
text), `CrosshairCursor` for Draw and for the remaining four,
since no single stock cursor reads as "split" or "mute" and the crosshair at least telegraphs "a
non-Select tool is active".

Both `TimelineClipLaneArea` and `PianoRollComponent` cache one cursor per tool
(`rebuildToolCursors()`), rebuilt only on a theme change — never per mouse-move, since building one
renders an icon into an `Image`.

Tests: `Tests/UI/Timeline/TimelinePanel/TimelinePanelToolStripTests.cpp`.
