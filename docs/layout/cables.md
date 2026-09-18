# Cables

One drawn wire on the patch canvas: what it is made of, how it is enumerated, hit-tested and
hovered, and how its colour is resolved.

## Cables are not graph edges

A **cable** is one wire as the user sees it, which is not the same thing as a
`juce::AudioProcessorGraph::Connection`:

| Drawn as | Backed by |
|---|---|
| Audio / MIDI wire | one graph edge |
| Attenuverter chain | two edges plus a hidden `AttenuverterModule` node |
| Poly bus | `voiceCount` parallel edges |

Anything that identifies, hit-tests, colours or removes a cable therefore keys on the logical view,
not on the raw edge. `GraphEditor::CableId` is that identity: `srcUid` / `srcPort` / `dstUid` /
`dstPort` plus `attenUid`, non-zero only for an attenuverter chain.

## One enumeration for paint and mouse

`GraphEditor::buildVisibleCables()` returns every drawn cable, in paint order, as `VisibleCable`
records carrying geometry, signal kind, source category, activity and bypass state. **Both**
`GraphContentComponent::paint()` and hit-testing consume that one list — they share the same literal
`std::vector`, not two independently-built ones.

This is load-bearing: computing the drawn curve and the clickable curve separately means they drift
apart the first time either is tweaked, and clicks silently miss the wire. For the same reason the
bezier lives in exactly one place, `GraphEditor::buildCablePath()`, which must stay identical to
`AppLookAndFeel::drawConnectionWire`'s default curve. Cable geometry is canvas-space, so zoom and
pan alone can never move a cable — only a graph edit invalidates it.

`buildVisibleCables()` returns a **memoized `const&`**: it is rebuilt only when
`GraphEditor::repaintCanvas()` invalidates the memo (see [rendering](rendering.md)), not on every
call. **The returned reference must never be stored across a `repaintCanvas()`, a `timerCallback()`
or a graph edit** — any of those can invalidate and rebuild the backing vector.

A collapsed macro re-anchors the cables crossing its boundary in a post-process pass at the end of
`rebuildVisibleCables()` — see [macro-cards](macro-cards.md#cables-re-anchor-around-a-collapsed-macro).

## Hit-testing

`GraphEditor::getCableAt(canvasPos, tolerance)` returns the topmost cable within `tolerance` canvas
px (default `kCableHitTolerance` = 7 px, wider than the wire so thin cables stay grabbable).
Distance is the perpendicular distance to the bezier via `juce::Path::getNearestPoint` — note that
method *returns* the distance **along** the path and writes the nearest point out by reference, so
the perpendicular distance is `pos.getDistanceFrom(nearestPoint)`.

Ties go to the later cable, matching paint order: mod wires draw over audio wires.

## Hover

`mouseMove` resolves the cable under the cursor and stores only its `CableId`. The canvas repaints
**only when the hovered cable changes**, never on every mouse move. Hovered cables are drawn
brighter and one pixel wider by `AppLookAndFeel::drawConnectionWire`'s existing `hovered` parameter,
and the cursor becomes a pointing hand.

This does not violate the no-continuous-repaint rule: `GraphEditor` already runs a 30 Hz timer
calling `content.repaint()` for the wire-flow animation, so a hover change only marks the next frame
dirty rather than adding a new repaint source.

Only the `CableId` is retained between frames. Geometry is rebuilt each paint anyway, and holding a
stale `VisibleCable` across a graph edit would dangle conceptually — ports move, nodes disappear.

## Right-click menu

Right-clicking a cable opens a menu with **Disconnect Cable**. Before this the only way to remove a
connection was to right-click one of its *ports*, which is not where users aim.

`GraphEditor::disconnectCable()` removes every graph edge behind the cable as **one** undoable
action — the whole attenuverter chain via `AudioEngine::removeModRouting()`, or all `voiceCount`
parallel edges of a poly bus. One visible wire, one undo step.

## Colour resolution

Wires are coloured through a single resolver, `synth::ui::resolveCableColour()` in
`Source/UI/Graph/CableColour.h`. Nothing in the paint path picks a wire colour directly:
`GraphEditor::colourForCable()` is the only caller, and the Appearance settings swatches resolve
through the same function, so a swatch can never show a colour the canvas does not actually use.
`GraphEditor` renders whatever mode and overrides it is handed via `setCableColourMode()` /
`setCableColourOverrides()`; it never reads `ApplicationProperties` itself.

| Mode | Persisted id | Behaviour |
|---|---|---|
| **By signal type** (default) | `signalType` | Colour encodes what the cable carries: Audio, MIDI, Mod CV, Poly Bus, Pitch, Gate. Preserves signal semantics — a gate fan reads differently from a pitch fan at a glance. |
| **By source module** | `sourceCategory` | Colour follows the module the cable leaves from, grouped into the eight `cableCategory` buckets. |

The mode is a canvas-wide setting under **Settings -> Appearance -> Cables**, persisted as
`cableColour.mode`.

**Why colouring by source is per category, not per module type.** 22 module types would mean 22
colour pickers, which nobody configures, and a newly added module would render uncoloured until
someone updated a table. With categories, a new module inherits its group's colour for free. The
eight category ids and their default colours are in [theming](theming.md#cablecategory).

In the default *By signal type* mode each routing maps to one colour token: `AttenuverterChain` and
mono `DirectCV` to `modWire`; `PolyBus` to `polyBusWire`; port role `Pitch` to `pitchWire`; role
`Gate` to `gateWire`. **Role wins over kind**, so a poly *pitch* fan is pitch-coloured, not
poly-bus-coloured. Plain audio edges use `audioWire` and MIDI edges `midiWire`.

## Every built-in theme must set every wire token

Every built-in theme (`Source/UI/Theme/BuiltInThemes.cpp`, and by extension any future
theme-construction helper) MUST explicitly set all six wire tokens (`audioWire`, `midiWire`,
`modWire`, `pitchWire`, `gateWire`, `polyBusWire`) and the full 8-entry `cableCategory` array.

**A field the constructor does not set silently falls back to `Theme.h`'s Obsidian (dark)
defaults** — exactly the bug that shipped in `makeDaylight()`: it set every wire token except
`midiWire` and never set `cableCategory`, so both fell back to Obsidian's dark-tuned values, giving
a lavender MIDI wire that washes out on Daylight's near-white background. Guarded by
`Tests/UI/Graph/CableColourTests.cpp`'s
`EveryBuiltInThemeWireIsLegibleAgainstItsOwnCanvas` (a WCAG contrast floor against both `bg1` and
`surface`, for every built-in theme) and `DaylightMidiAndCategoryColoursAreFixedNotInherited`.

## Activity and hover treatment is theme-polarity aware

The core stroke's activity and hover treatment lives in `synth::ui::wireCoreColour`
(`Source/UI/Graph/CableColour.h`), called from `AppLookAndFeel::drawConnectionWire` — **never inline
a brightness ramp at a paint site**.

Dark themes keep the idle-dim law: 50% brightness at idle, the token colour at full activity, hover
is `brighter(0.3)`. Light themes draw an idle wire at its exact token colour and mark activity by
darkening a touch, hover is `darker(0.3)`.

**Why the two differ.** The dark-theme dim law darkened every hue toward black on a light canvas —
indigo read navy, violet read near-black — destroying the palette's identity. Pinned by
`LightThemeIdleWireKeepsTokenColourIdentity`, `DarkThemeIdleWireStillDimsTowardCanvas` and
`HoverEmphasisFollowsThemePolarity` in `Tests/UI/Graph/CableColourTests.cpp`.

A bypassed modulation cable is drawn at 30% alpha: `resolveCableColour()` applies
`kBypassedCableAlpha` (0.3) after mode and overrides. `resolveCableBaseColour()` is the same
resolution *without* the bypass alpha — that is what the settings swatches render, so a pinned colour
is shown at full strength.

## User overrides

Theme tokens supply the defaults; the settings panel writes a **sparse override layer** on top — the
same relationship a code editor has between a colour theme and its colour customisations.

- An **unset** override means "follow the theme", so a theme switch moves any colour the user has
  not explicitly pinned.
- Clicking a swatch pins it (`cableColour.signal.<id>` / `cableColour.category.<id>`, stored as
  `#AARRGGBB`). Pinned swatches are drawn with a brighter ring.
- Right-clicking a swatch, or **Reset Cable Colours**, *removes* the key rather than writing the
  theme's current value in — that is what lets it follow the theme again.

Overrides are stored **globally, not per theme**. Someone who picks green cables wants green cables,
not green-until-the-theme-changes; Reset covers the case where a pinned colour clashes with a newly
selected theme.

`MainComponent::initialiseCommon()` restores the mode and overrides at launch. This matters:
`AppearanceSettingsTab` is built lazily when the Settings window opens, so leaving the restore to the
tab would mean the canvas ignored the user's saved colours until they went looking for them.
