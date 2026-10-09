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
bezier lives in exactly one place, `synth::ui::makeCablePath()` (`Source/UI/Layout/CableCurve.h`),
which `GraphEditor::buildCablePath()` (paint and hit-testing) and `AppLookAndFeel::drawConnectionWire`'s
fallback curve both call. Cable geometry is canvas-space, so zoom and
pan alone can never move a cable — only a graph edit invalidates it.

**Direction rule.** `p1` is always the output (source) end and `p2` the input (destination) end, fixed
by the caller (the in-progress drag wire swaps its endpoints when the drag started on an input). The
cable leaves `p1` heading right and enters `p2` heading right, from the left. Forward cables use a
handle of half the run, at least 50 px (`kCableMinHandle`) but never more than half the vertical gap, so
anything 100 px or more apart is the plain half-run curve, a near-vertical cable still bulges out
sideways, and a short hop (a macro's interior jack to a member) doesn't overshoot into a kink. Backward
cables use a handle of the full backward run clamped to 50 to 300 px (`kCableMaxBackHandle`), which keeps
the loop out of the output visibly to the right of its card, so a cable never leaves over its own card or arrives from below.

`buildVisibleCables()` returns a **memoized `const&`**: it is rebuilt only when
`GraphEditor::repaintCanvas()` invalidates the memo or the routing set changes (see
[rendering](rendering.md#per-frame-work-does-not-grow-with-the-patch)), not on every call and not on every tick; the
tick refreshes cable activity in place. **The returned reference must never be stored across a `repaintCanvas()`, a `timerCallback()`
or a graph edit** — any of those can invalidate and rebuild the backing vector.

A collapsed macro re-anchors the cables crossing its boundary in a post-process pass at the end of
`rebuildVisibleCables()` — see [macro-cards](macro-cards.md#cables-re-anchor-around-a-collapsed-macro).
That pass runs AFTER the knob-landing pass below, so a cable that is both knob-bound and crosses a
collapsed macro's boundary ends up re-anchored to the macro card. The pass lives in
`GraphEditorMacroCableAnchors.cpp`.

### Nested macros

With nested macros (see [macro-cards](macro-cards.md#nested-macros)), every hidden node maps to its **outermost collapsed ancestor**: the collapsed
macro with no collapsed ancestor of its own, whose card is the only one on screen for it. The usual
rules then apply to that card:

- both ends under the same card (however deep each sits) → the cable is dropped;
- ends under two different cards → both ends anchor, each on its own card;
- one hidden end → it anchors on that card.

A port jack anchor is used only when the hidden end is a port of that outermost macro itself; a
nested child's port hidden under the card takes the card's directional edge anchor, like any other
interior member.

`buildVisibleCables()` itself, one step further out than `rebuildVisibleCables()`'s own post-passes,
overlays `MacroCrossingAnimator::applyTo()` on the freshly-rebuilt vector — the FRO41 cable-slide
tween a Cmd-drag that crosses a macro's hull can arm on finalize (see
[`docs/macros/menu-and-membership.md#cable-crawl-and-module-flash-fro41`](../macros/menu-and-membership.md#cable-crawl-and-module-flash-fro41)).
It lives at the memo-wrapper level, not inside `rebuildVisibleCables()`, purely so the tween can
overwrite a matched cable's endpoints without teaching that enumeration anything about macros; while
no tween is live it is a no-op and the memo's own live-graph anchors show through unchanged. It offsets
each matched endpoint from its LIVE anchor, so a slide started mid-drag stays attached to the moving card.

### Retracting removed cables

A cable that goes away does not vanish, and one that comes back does not blink in. `CableRetractAnimator`
(`Source/UI/Graph/CableRetractAnimator/`) is pure state, armed by `GraphEditor::retractCablesGoneSince(before,
growAdded)` with the cables drawn before a change (`snapshotCablesForRetract()`), and driven by one 220 ms tween.

- **Retract.** A cable no longer drawn pulls back into its source jack (`easeInOutCubic`), staying fully opaque
  until 60% of the tween and then fading to 0 linearly. Callers: `disconnectCable`, `disconnectPort`,
  `removeModulator` (and so the mod dot panel's remove button), `MacroGroupController::deleteMacroPortManually`
  (through the `GraphCanvasHost` seam) and `AppUndoManager::undo()`/`redo()`, so Cmd+Z taking a cable away
  animates too. The ghosts paint in `GraphContentComponent::paint` after the live cables.
- **Grow.** Only undo and redo pass `growAdded`: a cable in the graph now that was not drawn before grows from its
  source jack out to its destination (`easeOutCubic`) while its opacity rises over the first 40%. The cable is real
  already, so the normal cable pass skips its id and the growing wire is drawn instead (no flow dots or knob until
  it lands). More than 16 cables in one step skip the grow (a mass undo shows them at once). Undoing the mod dot
  panel's remove button grows the cable back this way.
- **Reduce Motion** is an 80 ms alpha fade, out or in, with no geometry change. **Animations: Off** shows the result
  at once.
- Each frame repaints only the area the moving wires were and are drawn in (`CableRetractAnimator::paintArea`).

New does not go through undo/redo and does not animate; a project opened on screen draws its cables out from their
source jacks instead, each once both its ends have appeared ([project load reveal](animation.md#project-load-reveal)).

## Knob landing

FRO312: a `ModCV` jack with a bound, VISIBLE knob (`ModuleComponent::sliderIndexForModTarget` /
`getModRingSliderIndex` >= 0) draws no gutter dot at all — see
[the drag-to-knob doc](../modules/modulation.md#drag-to-knob-modulation) for why the jack is hidden
and how the input column packs around the gap. A cable of ANY kind (`AttenuverterChain`, `DirectCV`,
or `ModRouting`/poly bus) whose destination is such a jack therefore has nowhere else to land: it
ends at the knob's ring-landing point instead, the same for every kind, because
`ModuleComponent::getPortCenter` itself returns that point for a knob-bound visible index — there is
no separate "gutter position" left to choose between. `GraphEditor::reanchorCablesToKnobTargets`
(`GraphEditorModHover.cpp`) still runs as a post-pass at the end of `rebuildVisibleCables()` and sets
`VisibleCable::landsOnKnob`, but by the time it runs `cable.p2` is normally already correct — the
pass exists to flag every kind uniformly rather than to move the point.

FRO313: the anchor (`ModuleComponent::getModTargetKnobAnchor`) sits just OUTSIDE the ring's own
drawn arc, not on it — same rotary-start angle (norm 0, lower-left) as before, but the radius is
pushed out by half the ring's stroke width (clears the arc itself) + half the landing dot's own
diameter + a 2px gap, so the dot never visually overlaps the arc it used to sit directly on. It
still shares `AppLookAndFeel::modRingAngleForNorm` (via `modRingPointForNorm` in
`ModuleComponentInternal.h`) with the ring drawn at [modulation rings on
knobs](../modules/modulation.md#modulation-rings-on-knobs), so the cable always lands at a fixed,
predictable offset from the ring, never a hand-rolled approximation.

`destNodeId`/`destChannel` carry the logical destination as a RAW channel (matching
`ModulationTarget::channelIndex`), set for every cable kind so they also serve as the
[hover-correlation](#hover) key.

**Fallback — the gutter jack, unchanged.** A knob on a hidden tab page (`getModTargetKnobAnchor`
returns `nullopt` the same way `getModRingSliderIndex` does) is not knob-bound for layout purposes
either — `ModuleComponent::isInputJackKnobBound` says false, so the jack stays drawn and a cable
still lands there, rather than painting a landing dot over empty card.

**The landing dot.** `paint()` draws cables BEFORE module cards paint as children, so a cable
re-anchored deep into a card's knob has its final stretch drawn underneath the opaque card.
`GraphContentComponent::paintOverChildren` draws a dot (`ModuleComponent::kKnobLandingDotDiameter`,
7px), in the cable's own resolved colour, at `p2` for every `landsOnKnob` cable — the one part of a
knob-landing cable guaranteed to be visible on top of the card. The same dot is also the pickup
target: since the gutter jack a cable used to be picked up/disconnected from no longer exists for a
knob-bound target, a click within a few px of the dot claims the same
begin/drag/endConnectionDrag gesture a real input jack starts — see
`ModuleComponent::wantsCablePickupGestureFor`/`handleCablePickupGesture`, wired onto the knob itself
(`CardKnobSlider::wantsCablePickupGesture`/`onCablePickupGesture`) rather than into
`getPortForPoint`, exactly like the ring-amount-drag gesture already sitting on the same knob.
When the knob has at least one attenuverter routing, a plain press on the dot is NOT a pickup any
more: it is the [mod-dot drag](../modules/modulation.md#the-mod-dot-drag-an-amount-from-the-landing-dot)
(the amount of the last-chosen source), and **Cmd+press** keeps the cable pickup. A dot with no
attenuverter routing (a direct or poly cable, or a bare knob's drop spot) still starts the cable drag
on a plain press.

**Invalidation.** The knob-landing pass runs on every `rebuildVisibleCables()`, so it follows a
moved/resized card automatically (both already call `repaintCanvas()`), and a knob moved inside its card (the on-card
layout editor) through `ModuleComponent::childBoundsChanged`. A Wavetable tab-page switch
changes which knob is visible without moving or resizing the card, so it explicitly calls
`GraphEditor::notifyModuleContentChanged()` (a public wrapper around the otherwise-private
`repaintCanvas()`) to invalidate the memo too.

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

**Knob correlation.** Hovering a cable that `landsOnKnob` also sets
`GraphEditor::setHoveredModTarget` to its destination — `ModuleComponent`'s ring paint reads it back
and highlights that knob's ring. The same state runs the other way: hovering the knob itself (its
`CardKnobSlider`'s `onHoverChanged`) sets the same target, which the cable's own hover treatment
checks. See [modulation rings on knobs](../modules/modulation.md#modulation-rings-on-knobs) for the
ring-side treatment and the hover chip.

## Port connections panel

A plain click on any jack (audio, CV or MIDI; a regular module, a macro boundary port node, Track In, Audio Output) opens a
small panel in a window beside the jack that lists what the jack is wired to. Collapsed macro cards are not covered yet.
It is built from the [mod dot menu](../modules/modulation.md#the-mod-dot-menu)'s parts: the same window (`ModDotPanelFrame`
through the shared `launchInFrame`, which grows out of the jack and shrinks back, closes on a click outside or Esc), width
(`ModDotPage::kWidth`), palette, row look and remove button.

- **Click.** A left press and release under 3 px apart that is the first click of its run. A drag from the jack still
  makes a cable and opens nothing; the right-click menu is unchanged. A second click on the same jack closes the panel.
- **Double-click.** With *Double-click port to disconnect* on, a jack with exactly one cable waits one double-click
  interval before opening, so the double-click that disconnects it never flashes the panel. A jack with none or several
  cables opens at once, and a double-click on one with several keeps the panel and disconnects nothing. The knob-bound
  CV jack's double-click is the mod dot's and is unchanged.
- **Content.** The title reads "Module · port (N connections)" ("1 connection", "No connections yet"). One row per
  cable: a swatch in the cable's colour (`GraphEditor::colourForCable`), "other module · other port" and a remove button.
  A macro port reads as its own name; a cable landing on a knob names the knob's parameter. "Disconnect all" shows under
  two or more. The rows come from `listPortConnections`, a filter over `buildVisibleCables()` by the jack's visible index
  (a cable names raw channels, the card and the labels speak in visible jacks), never from graph edges. Zero and one
  connection still show the panel; with none, "Add connection" is its main action.
- **Edits.** Remove is `GraphEditor::disconnectCable` (one undo step, the cable retracts; an attenuverter chain goes
  through `removeModulationChain`), "Disconnect all" is `GraphEditor::disconnectPort` (one undo step). The panel follows
  the graph on the editor's 30 Hz tick, so an undo brings a row back. Removing the last connection keeps the panel open.
- **Canvas highlight.** Hovering a row, or keyboard-focusing its remove button, draws that cable as hovered and dims
  every other cable to 35% (`PortPanelController::highlightedCable`, read by the canvas cable paint); it clears when the
  pointer or focus leaves the row, the row goes, or the panel closes.
- **Add connection.** Under the rows (above "Disconnect all") sits a split button in the mod dot's look
  (`SplitButton`): **Add connection** and **Pick on canvas**. Add connection swaps the list for a search page in the
  same panel, a cross-fade (the list's alpha 1 to 0, the page's 0 to 1) while the panel's height settles to the
  page's: 160 ms ease-out going in, 110 ms ease-in coming back; under Reduce Motion an 80 ms fade with the height
  changing at once. Focus lands in the search field. A **Back** link, Esc (after the search is cleared) or a pick
  returns to the list. The search page (`PortTargetSearchPage`, modelled on the mod dot's Add source list) lists what
  the jack could be cabled to with the cable drop's rules: an output lists input jacks and an input lists output
  jacks of other modules (never its own), MIDI only with MIDI. A target already wired to the jack is greyed with
  "Connected" and is not pickable. When the jack is a modulation output it also lists the other modules' knobs
  ("Filter 1 · Cutoff", from `getModulationTargets()`; a knob already modulated by it shows "Connected"). Modules are
  ordered nearest this jack's card first (canvas distance, title breaking a tie); a module with exactly one target is
  ONE row "Module · port", one with several is a foldable group with its targets underneath. Typing uses the
  [shared search](chrome.md#shared-search) (matched letters highlighted, aliases searched but not painted) and adds
  "New <module>" rows (the New tag) for authorable module types with a compatible jack, once per class. Down from the
  field enters the rows, Up/Down walk them, Return with nothing focused picks the best match (lowest `searchScore`,
  existing targets before New rows), Esc clears the query and then goes back. Hovering or focusing a row previews a
  dashed cable from the jack to its target on the canvas (`PortPanelController::setPreview`, painted over the cables;
  it clears when the pointer or focus leaves, or the panel closes).
- **Picking.** A pick is one undo step and goes through `PortConnector` (`Source/UI/Graph/PortPanel/`), a friend of
  `GraphEditor`: a jack uses `connectJacks`, the **same function the cable drag's drop calls**, so a poly fan, MIDI, a
  mono CV jack through its attenuverter, the macro-boundary auto ports and a Track In's auto channel behave exactly
  as dropping a cable there would; a knob uses `GraphEditor::connectModulationSource` at the mod dot's new-source
  depth; a "New <module>" row creates the module beside this jack's card (right of an output, left of an input; it
  joins this card's macro) and cables its first compatible jack, all in the same undo step. The new cable grows out of
  its source jack (the [cable grow-back](animation.md#motion-rules) armed after the connect), the panel swaps back to
  the list and the new row grows in.
- **Pick on canvas.** The mod dot's `ModDotCanvasPicker` in its jack mode: a layer over the canvas outlines the
  eligible jack (or knob) under the pointer, a press on it connects, Esc stops. The panel stays open while picking
  (`keepOpenOnOutsideClick`); the eligible set is the list of compatible targets above.
- **Keyboard.** Tab visits each row's remove button, the two halves of the split button (Left/Right hop between
  them), then "Disconnect all" (Up/Down move between them), Return/Space press, Esc steps back (stop picking, clear
  the search, return to the list, close). Every control has a name and a tooltip.

Code: `Source/UI/Graph/PortPanel/` (`PortPanelController`, `PortConnectionsPanel`, `PortConnectionRow`,
`PortConnectionList`, `PortTargetList`, `PortTargetSearchPage`, `PortConnector`); the card hands a jack release and a jack double-click to the controller from
`ModuleComponentInteraction.cpp`. Motion: [animation](animation.md#motion-rules) (Port connections panel row).

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
