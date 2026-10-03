# Module Card Geometry

`ModuleComponent` is the card one module draws as. It outgrew one file and is split by concern
under `Source/UI/Graph/ModuleComponent/`, the same `<Class>/<Class><Concern>.cpp` pattern as
`Source/UI/Graph/GraphEditor/` and `Source/MainComponent/` — the unit-by-unit table is in
[architecture/module-base.md](../architecture/module-base.md#modulecomponent).

Where a card LANDS on the canvas is [layout](layout.md); what it draws inside itself is here.

## Width buckets

Card widths are standardised into three named buckets, defined in `LayoutUtil.h`:

| Bucket | Constant | Width | Modules |
|---|---|---|---|
| `Narrow` | `kNarrowWidth` | 40 px | AttenuverterModule |
| `Single` | `kSingleWidth` | 280 px | Oscillator, Filter, VCA, ADSR, LFO, FX modules, VoiceMixer, PolyMidi, all others |
| `Double` | `kDoubleWidth` | 560 px | Sequencer, PolySequencer, MidiKeyboard |

`kDoubleWidth == 2 * kSingleWidth`, so a Double module occupies exactly two standard column slots.
Attenuverter modules are excluded from the visible card grid entirely — they draw no
`ModuleComponent` and are skipped by auto-arrange.

## Body layout

A built-in module's parameter widgets are built, bound and laid out by its **card body**, `CardBody`
(`Source/UI/Graph/CardBody/`), from a plan of the module's parameters and its resolved layout
([module-card-layout.md](module-card-layout.md#rendering)). No module has a code default yet, so
every card draws the **automatic layout**, which is the generic card exactly: all combos, then all
toggles, then the Threshold view, then a 3-column knob grid, each group in parameter declaration
order. Widgets are built in declaration order whatever the layout, so child, Tab and screen-reader
order never depend on placement. The bespoke cards (Sequencer, Poly Sequencer, Macros, Parametric
EQ, Attenuverter) build their widgets through the same card body but place them themselves; MIDI
Keyboard, External MIDI and a hosted plugin build their own.

**Section headers and renamed controls.** A section with a title (one the user added or renamed in the
layout editor) starts with a header row, `kSectionHeaderHeight` tall across the content width: a small
bold label in the title's own case (UI text is never all caps), created after every widget so it never
changes Tab or screen-reader order, and measured by the same walk, so measure == apply holds. An
untitled section (every automatic layout) has none, which keeps those cards pixel for pixel as before.
A control the layout renames shows the new name as its caption (a toggle's text), with the full
parameter name as the caption's tooltip; its component ID stays the parameter's own name.

Around the body, `layoutDefaultContent(bool apply)` adds the card's chrome (Sampler and Wavetable
rows above it; the LFO wave editor, Show Envelope Graph / Response / Spectrum / Scope rows below it;
the More row last). It runs twice per size change: once with `apply = false` to measure the
height, once with `apply = true` from `resized()` to position the children, and the card body's
own `layout(apply)` follows the same rule.

**Why one function runs twice instead of two functions.** The measured height and the real positions
cannot drift apart when they come from the same code. They previously did: two hand-maintained
copies of the geometry disagreed, and body content was drawn on top of the lowest port labels.

| Constant | Value | Meaning |
|---|---|---|
| `kKnobColumns` | 3 | knobs per row |
| `kContentMargin` | 12 | left/right gutter for body content |
| `kNarrowContentWidth` | 200 | combos / toggles / the Sampler load row, centred |
| `kLabelHeight` | 18 | label above a knob or combo |
| `kRowHeight` | 24 | combo box, toggle, button |
| `kKnobHeight` | 58 | rotary plus its text box |
| `kKnobLargeHeight` | 80 | a large knob: 60 px dial plus its text box |
| `kFaderVHeight` | 96 | a vertical fader's travel plus its text box (40 px wide, centred in its cell) |
| `kFaderHHeight` | 28 | a horizontal fader: cap, modulation bar and value box |
| `kWaveformHeight` | 72 | Sampler waveform overview |
| `kSectionHeaderHeight` | 18 | a titled section's header row |
| `kTabStripGap` | 8 | above a tab strip, and between it and its selected tab's controls |
| `kPortLabelClearance` | 15 | gap below the lowest jack before body content starts |

Generic bool-parameter toggles, and the `freqResponse` / `spectrum` / `scope` show-hide toggles, lay
out at full `contentX` / `contentW`, flush-left with the knob grid, rather than the centred
`narrowX` / `narrowW` band — a toggle's label reads better flush-left than centred in a narrow
column.

**Body content always starts below every jack.** `getContentTopY()` derives that y from
`getPortCenter()` — the same function that anchors wires — rather than recomputing the port
geometry, so content can never land on a port label. Because the body sits below the ports it does
not need the narrow gutters that used to keep it clear of them, which is what makes three knobs per
row fit inside the 280 px card.

The header-to-first-port gap is 9 px (base header offset constant 38 in both `getContentTopY()` and
`getPortCenter()`), giving the MIDI In/Out row breathing room under the header hairline.

Measured heights. `GraphEditor::estimateModuleSize()` gives the library drag ghost and drop
placement a card's size before the card exists. A card drawn from layout data is **measured** from
its card-body plan (`CardBodyMeasure.cpp`: the same plan and layout walk, built from the type's code
default with its conditions read at the fresh module's values, the knob-bound jack rule from the plan,
and the chrome rows, footer row and More row around the body); only the bespoke cards read a small
table. `ModuleComponentTest.EstimatedModuleSizesMatchTheRealComponents` builds every library type and
fails if either path drifts from the real card, and `CardBodyGolden` pins every child's bounds of
every card against one file per type, `Tests/fixtures/card-body/<Type>.golden`. Fresh cards, single
width (280) unless noted, one table per module family, so the work on one family's default layouts
edits only its own table:

| Sources | Height (px) |
|---|---|
| Oscillator | 471 |
| Noise | 245 |
| Sampler | 565 |
| LFO | 391 |
| Wavetable | 578 (double width, 560) |

| Envelopes and utilities | Height (px) |
|---|---|
| ADSR (Amp Env, Filter Env) | 597 |
| VCA | 193 |
| Envelope Follower | 245 |
| Sample & Hold | 427 (Clock External: the trigger meter opens above the knobs) |
| Math | 249 |
| Voice Mixer | 277 |
| Poly MIDI | 169 |

| Filter and dynamics | Height (px) |
|---|---|
| Filter | 477 |
| Compressor | 365 |
| Gate | 293 |
| Limiter | 309 |

| Effects | Height (px) |
|---|---|
| Delay | 343 |
| Reverb | 423 |
| Chorus / Phaser / Flanger | 309 |
| Distortion | 375 |
| Bitcrusher | 229 |
| Ring Modulator | 249 |
| Pitch Shifter | 419 |

| Other | Height (px) |
|---|---|
| Comparator | 185 |
| Channel Strip | 409 |
| Master | 221 |
| Rec Tap / Track Audio | 131 |
| Track In | 100 |

From the table (bespoke): Sequencer and Poly Sequencer 560x406, MIDI Keyboard 560x160 (an Octave stepper row above the keys), Macros (tracks
its `Knobs` count, 458 at the default), Attenuverter (square, `kNarrowWidth`),
Parametric EQ 560x592, External MIDI 146, Hosted Plugin 135, and Audio Input / Audio Output on a
100 px floor.

## The More row

A layout lists what it hides; every parameter it hides or does not place renders in a folded **More**
row at the very bottom of the card, under the Scope row (or the footer row). The row exists only when something is hidden,
so no card shows one today (the automatic layout hides nothing); it appears through a stored layout
(the node's `cardLayout` override, [module-card-layout.md](module-card-layout.md#where-a-layout-comes-from)).

- **Folded** (the default, also after a load): one full-width button, "More controls (N)", a Tab stop
  after the body's controls with the accent focus ring; a click, Return or Space toggles it. The
  hidden parameters' widgets exist and stay bound (value, attachment, MIDI Learn, automation), but are
  out of view, so a hidden knob's CV jack draws in the port gutter again.
- **Unfolded**: the hidden controls lay out under the button in the body's runs (combos, toggles, a
  3-column knob grid), and a knob-bound jack leaves the gutter as usual.
- **Height.** Folding and unfolding change the card's height: the card re-measures and calls
  `GraphEditor::handleModuleResized`, the same make-room path every growing card takes. A value drag
  never changes a card's height (`CardBodyLayout.CardHeightNeverChangesWhileAKnobIsDragged`).
- **Cables.** A modulation cable dragged over the folded row unfolds it, so a hidden parameter's knob
  can still take a new cable (`GraphEditor::dragConnection` asks each card under the cable).

## The footer row

A layout's section with the reserved id `footer` (`CardSection::kFooterId`) is drawn as one compact
row at the bottom of the body, under the card's chrome panels and above the More row, wherever the
layout lists it (`CardBodyFooter.cpp`). No card has one by default yet; a code default or a stored
layout adds it.

- **What goes in it.** A toggle is the **small toggle pill** (below); anything continuous is a
  horizontal fader (`faderH`, whatever widget the item names); a choice, switch or stepper is its own
  widget. A fader, combo, switch or stepper carries its caption inline on its left, in the pill's
  12 px text; the pills keep their width and the rest share what is left. Items that do not fit wrap
  onto another row, 28 px tall each.
- **Chrome toggles join it.** On a card whose layout has a footer, Show Envelope Graph (the ADSR),
  Show Response, Show Spectrum (while the response view is open) and Show Scope are pills at the end
  of the row instead of full-width
  rows; the panels they open sit above the row. **Poly** is card chrome too: a footer card puts the
  `poly` toggle in the footer unless the layout places it elsewhere or hides it, so a default layout
  never lists it. A card without a footer section keeps today's chrome rows exactly.
- **Bindings are the widgets' own.** A pill is the parameter's ordinary toggle (attachment, MIDI
  Learn, Tab stop, title, tooltip), drawn differently; a footer fader is a `CardFader` with every knob
  gesture, so a cable lands on it and a knob-bound jack stays off the gutter.
- **The pill.** `synth::ui::setTogglePillStyle` (`CardWidgets/CardTogglePill.h`) marks a
  `juce::ToggleButton`, and `AppLookAndFeel::paintTogglePill` draws it: a 20 px rounded pill, filled
  with the tick colour and the text in the background colour when on, an outlined surface when off,
  the accent focus ring round the pill. Width is the text plus 10 px each side
  (`AppLookAndFeel::togglePillWidth`, which the size estimate uses too). Pill and footer-caption
  text is measured and painted in the embedded Inter (`AppLookAndFeel::uiTextWidth` / `uiFont`), never
  the system typeface, so every width is the same on every platform.

## Tab strips

A run of consecutive `presentation: tab` sections in a layout is drawn as one **tab strip** with one
tab's controls under it (`CardBodyTabs.cpp`, laid out by `CardBodyLayout.cpp`;
[module-card-layout.md](module-card-layout.md#rendering)). The Wavetable's code default is the only one
with tabs today; a stored layout keeps them.

- **Geometry.** The strip spans the content width, `kRowHeight` tall, `kTabStripGap` below where the
  run's first section stands; the selected tab's section follows after `kTabStripGap`, laid out like any section (a tab draws
  no header row: its title is on the tab). The group is as tall as its **tallest** tab whichever is
  selected, measured by the same walk (measure == apply on every tab), so a tab switch never resizes the
  card or moves anything outside the group.
- **Hidden tabs.** The other tabs' widgets are hidden but keep their bounds, bindings, MIDI Learn and
  automation; a hidden knob paints no modulation ring and takes no cable drop (it is off the card for
  `CardBodyPlan::isOnCard`).
- **Jacks.** A knob on a tab never binds its CV jack, selected or not (`getModTargetKnobAnchor`; the
  size estimate applies the same rule): the jack stays in the gutter, so the jack layout and the
  content top are the same on every tab. A cable dropped on the visible knob still connects to its jack.
- **The strip.** A `CardSegmentedSwitch`, componentID `cardTabStrip`: joined buttons with connected
  edges, the look the Wavetable's own strip had. One Tab stop between the controls above it and the
  tab's controls; Left/Right step through the tabs and stop at the ends, Home/End jump to the first and
  last; the accent focus ring; titled "Control tabs" with a tooltip naming the keys; to a screen reader,
  a group of radio buttons titled with the tab names, each with a tooltip. A switch re-lays the card out
  and repaints the canvas so cables re-anchor (`CardBody::selectTab`).
- **State.** The selected tab is per card and never saved; a new or rebuilt card opens on its first tab.

## Conditions: swap and dim

A layout item's `when` and a section's `visibleWhen` are read live (`CardBodyConditions.cpp`): the
card listens to the parameters its conditions read, and a change (from a click, automation, undo or
a preset) re-reads them on the message thread and re-lays out the card only when a result changed.

- **Dim.** `effect: dim` greys the control out unless the condition holds; a code default may add
  numeric dim rules a stored layout cannot express (Detune while Unison is 1). A dimmed control is not
  disabled (that would take it out of Tab order and, through JUCE's focus traverser, out of the
  accessibility tree): it and its caption carry `AppLookAndFeel::kDimmedProperty`, which every card
  painter honours (knob, fader, toggle, pill, combo, segmented switch, stepper, caption) by painting at
  `AppLookAndFeel::kDisabledControlAlpha`, the focus ring staying at full strength. It keeps its cell,
  its Tab stop, its right-click menu and its bindings, and stays operable; its accessible description
  reads "Inactive in this mode" and its tooltip ends "(inactive in this mode)", both removed when the
  dim lifts. A genuinely disabled knob or toggle anywhere in the app paints at the same alpha.
- **Swap.** Consecutive `effect: show` items testing the same parameter form a **swap group** (an
  item repeating a condition already in the group starts the next group): one
  cell, laid out in the run of its tallest member, showing the member whose condition holds (the cell
  stays, empty, when none does). Every member takes that cell at its own height, so a swap moves
  nothing; a swapped-out knob keeps its knob-bound jack in the cell (`CardBody::isSwappedOut`, the same
  rule as the ADSR's BPM swap), so the gutter never changes either. A swapped-out control is not
  hidden: it never appears in the More row.
- **Sections.** A section whose `visibleWhen` does not hold takes no space, with its header. Showing or
  hiding it changes the card's height, through `GraphEditor::handleModuleResized` like any other growth:
  neighbours are pushed clear and come back when it shrinks.

## Faders, switches and steppers

A stored layout can draw a parameter as a widget other than the automatic one (`ParamItem.widget`,
[module-card-layout.md](module-card-layout.md#widgets)); no card does by default, so every card still
draws the automatic layout above. The plan honours the widget only where it suits the parameter and
otherwise falls back to the automatic kind (`cardBodyKindFor` in `CardBodyPlan.cpp`). Each kind lays
out as its own run, by the same measure-and-apply walk:

| Widget | Widget class (`Source/UI/Graph/CardWidgets/`) | For | Run | Height under its 18 px caption |
|---|---|---|---|---|
| `knobLarge` | `CardKnobSlider` | a float or int | a grid, one per cell | 80 |
| `faderV` | `CardFader` (vertical) | a float or int | a grid, one per cell, 40 px wide | 96 |
| `faderH` | `CardFader` (horizontal) | a float or int | one per row, full width | 28 |
| `segmented` | `CardSegmentedSwitch` | a choice with 2 to 6 values of at most 10 characters | one per row, full width | 24 |
| `stepper` | `CardStepper` | an int spanning at most 24 steps | one per row, centred band | 24 |

- **Faders.** `CardFader` is a linear `juce::Slider` painted by `AppLookAndFeel`'s fader painter
  (`AppLookAndFeelFader.cpp`, the design system's Fader): a vertical one at 40 px wide gets the small
  fader (4 px slot, 18x7 cap), a horizontal one the medium fader (4 px slot, 10x18 cap); the painter
  also draws its focus ring round the cap. A vertical fader's value box is 40 px, so it shows the parameter's text without spaces ("1.00s") at the theme's label size (`CardFader::useCompactValueText`) rather than truncating. It carries every knob gesture through
  `CardControlGestures` (the modulation-amount drag, the cable pickup on its landing dot, the hover
  that highlights the landing cable, the keyboard steps) and is in the card's slider list, so
  Automate, MIDI Learn, value reflection and the modulation-target lookup treat it as a knob. Its own
  drag is the mixer fader's: Shift drags at an eighth of the rate (re-anchored where the drag is, so
  the value never jumps), Cmd-click and double-click reset to the parameter's default, each one change
  gesture. Modulation shows as a 3 px bar beside the slot (right of a vertical cap, under a horizontal
  one) from the base value to base + CV; a cable lands on a dot just past the bar's zero end.
- **A choice beside a fader.** A choice in a swap group with a vertical fader (the ADSR's note division
  taking its time's place in Tempo mode) is drawn as a vertical *stepped* fader too (`promoteChoicesBesideFaders`
  in `CardBodyPlan.cpp`): a combo does not fit a 40 px wide cell, but the fader's value box shows the step's
  text whole ("1/16", "1/4.", "1/8T"). It takes the same cell as the fader it replaces, so a swap moves
  nothing; the slider steps over the choice's integer values, so the arrow keys, Page Up/Down and Home/End walk
  the divisions, and it keeps the knob gestures, MIDI Learn and its accessible title (the parameter's name).
- **Segmented switch.** Joined `TextButton` segments (the look the ADSR's old MS|BPM switch had); a
  bool parameter draws as two segments, its off and on texts (the ADSR's Tempo Sync reads "Time" and
  "Tempo"); the switch
  is the one Tab stop and the segments take no clicks themselves (the switch hit-tests them), so a
  right click reaches the card as the switch's.
- **Stepper.** "-" value "+": two buttons, each a Tab stop; the card listens to the stepper and its
  buttons, and a right click on a button resolves to the stepper's registered parameter.
- **Height.** Switching a control between a knob and a fader changes the card's height, as a discrete
  layout edit; a value change on any widget never does
  (`CardBodyWidgetKind.NoValueChangeOnAnyNewWidgetResizesTheCard`).

### Hide or show from the right-click menu

Every control on a card drawn from layout data adds to its right-click menu, after Automate and the
MIDI Learn block: **Hide from card** (or **Show on card** for a control in the More row), **Show as
fader** / **Show as knob** for a float or int on the card, and **Edit Layout...**, which opens the
layout editor beside the card ([module-card-layout.md](module-card-layout.md#editing-a-layout)); the
module menu carries the same item. The bespoke cards offer none of these. Each click is one write of the node's `cardLayout` override, one undo step,
starting from the layout the card draws now (the automatic layout written out as explicit items,
which builds the same card): Hide adds the parameter to `hidden` and leaves its item where it is, so
Show on card puts it back exactly there; a parameter the layout never placed goes to the end of the
last section; Show as fader picks `faderV`. A card builds its body once, so `updateComponents`
rebuilds a card whose override no longer matches the one it was built from: the quick path writes,
rebuilds and makes room inside one undo record, and undo and redo rebuild the same way
(`CardLayoutQuickEdit.cpp`, `GraphEditorCanvas.cpp`).

## Modules that resize at runtime

Widths are static; a few modules' **heights** are not (the More row above also folds a card's height). The LFO (FRO114) stays `Single` width even with
its Custom-waveform section open — the section adds `24 + 2` px (the Grid/Shapes/Tools toolbar
row) `+ 150` px (the curve editor, `kLfoWaveGraphHeight`) `+ 8` px (bottom breathing room), a total
of 184 px, inserted right after the envelope graph section in `layoutDefaultContent` — and ONLY
while `shape == Custom` (`LFOModule::kCustomShapeIndex`); every other shape measures exactly the
369 px in the table above, unchanged. See `ModuleComponentLfoCard.cpp`'s
`layoutLfoCustomWaveSection` and [lfo.md](../modules/lfo.md)'s "Card UI" entry.

The Macro bank (`MacroControlModule`) grows and shrinks with its `Knobs` parameter. Its geometry lives in `LayoutUtil.h` so the component layout,
the output-jack hit test and `estimateModuleSize` all read the same numbers:

```cpp
kMacroHeaderH  = 94   // title bar + Knobs / Bipolar row
kMacroRowH     = 44   // one macro knob and its output jack
kMacroBottomPad = 12

macroBankHeight(count) == kMacroHeaderH + count * kMacroRowH + kMacroBottomPad
macroRowCentreY(index) == kMacroHeaderH + index * kMacroRowH + kMacroRowH / 2
```

**Growth is anchored at the top-left and pushes neighbours, never itself.** Moving the module the
user is currently interacting with would teleport it out from under the cursor, so
`GraphEditor::handleModuleResized` hands the card to `MacroGroupController::reflowForResizedModule`, for a loose card
and a macro member alike. It first offers every neighbour this card pushed earlier its way back, then pushes whatever
the new rect covers by the macro rules (the shortest way, cascade, canvas wall, pinned output dock, 160 ms glide). The
pushes are remembered per card (transient, never saved) so a shrink returns them: a neighbour comes home only if the
user has not moved it since and its old spot is clear (a spot the card still covers stays blocked and keeps its record).
Undo/redo, Auto Arrange, a project load and deleting the card drop the records. See
[layout.md](layout.md#making-room-when-something-grows).

**Which in-place size changes reach `handleModuleResized`:**

| Change | Entry point |
|---|---|
| Macros bank `Knobs` count | `applyMacroCountChange` |
| Poly toggle | `applyPolyStateChange` |
| Dual I/O toggle (and the stereo-pair sweep) | `applyDualIOLayoutChange` |
| Audio Input device channel count, Hosted Plugin port re-measure | `refreshPortLayout` |
| LFO shape set to Custom (Draw section) | `ModuleComponentLfoCard.cpp` |
| Show Scope / Show Response toggles | `setScopeShown` / `setResponseShown` |
| A layout section shown or hidden by its condition (`visibleWhen`) | `CardBody::refreshConditions` |
| ADSR Show Envelope Graph (opens or closes the card body's `envelope` view) | the toggle's `onClick`, `CardBody::setViewOpen` |

Not wired because the card height does not change: the Spectrum toggle (a backdrop only), a layout's swap group or dim,
the ADSR's Time|Tempo switch (a swap group: each stage fader for its division in the same cell) and a tab switch (a tab group is as tall as its tallest tab). A card that
is still being constructed never makes room (it is not on the canvas yet).

## Header buttons

The header area holds `DrawableButton` instances, not `TextButton`s, positioned in `resized()`
(`ModuleComponentPaint.cpp`):

| Button | Bounds | Action |
|---|---|---|
| `deleteButton` | `(w-26, 2, 22, 20)` | Calls `owner.requestDeleteModule(nodeId)` — tooltip "Delete module" |
| `bypassButton` | `(w-50, 2, 22, 20)` | Toggles bypass state — tooltip "Bypass" |
| `muteButton` | `(w-74, 2, 22, 20)` | Toggles mute state — tooltip "Mute" |
| `dualIOButton` | `(w-98, 2, 22, 20)` | Stereo-capable modules only — every type in `AIStateMapper::dualIOCapableModuleTypes()` (the FX, Voice Mixer and Ring Modulator outputs, and the split-block voice modules). Splits or merges the stereo jack pair; the tooltip names Dual I/O. |

`requestDeleteModule(NodeID)` is the canonical delete entry point; `deleteButton.onClick` delegates
to it.

### Deleting a module: reconnect the chain (FRO23)

Every user-facing delete path funnels through `GraphEditor::deleteSelection()` or
`::requestDeleteModule()`/`::deleteModule()` — the Delete key, this header's own `deleteButton`,
and the canvas/card context menus' "Delete"/"Delete Module"/"Delete N Selected Modules" items all
end up here. When the "Reconnect the chain when deleting a module" preference is on (the default —
Settings → Preferences, `reconnectChainOnDelete`, `PreferencesSettingsTab::isReconnectChainOnDeleteEnabled`)
and a deleted module has EXACTLY one incoming and one outgoing audio cable — counted at the visible
logical-cable level, so a stereo pair sharing both jacks is one leg, not two — the module's surviving
upstream and downstream neighbours are wired directly to each other, with the same L->L/R->R mapping
a user-drawn cable gets (`GraphEditor::resolvePolyLink`). A module with more audio legs on either
side (a mixer, a splitter, two audio inputs) deletes exactly as before, with no reconnection.

A split-block module (Oscillator, Filter, VCA, Wavetable) with Dual I/O switched ON presents Left
and Right as two SEPARATE visible jacks — the default patch's own shape, Oscillator -> Filter ->
VCA, all three shipping Dual I/O ON by construction. That looks like two incoming and two outgoing
legs, which the 1-in/1-out rule would otherwise reject outright — the most common single "delete an
effect" case there is, so `mergeDualIoStereoPairLegs` (`GraphEditorDeleteHeal.cpp`) treats a
matching Left/Right pair as ONE logical leg when BOTH the deleted module AND its peer are genuinely
Dual I/O split (`ModuleBase::isDualIO()`, checked on both ends — not just "jack index 0 and 1 exist"
coincidentally, which would wrongly fold a real two-input mixer's independent jacks into a fake
stereo pair). Healing then wires Left->Left and Right->Right as two EXPLICIT jack pairs, one
`connectPorts` call each, validated together as all-or-nothing before either is wired —
`resolvePolyLink` has no notion of "two separate jacks" within a single `PolyLink`, unlike a
collapsed stereo jack's own multi-voice fan, which is why this needs its own two-call path rather
than reusing the fan. A "mixed" shape — the module split but its peer collapsed, or its two raw legs
landing on two different peer nodes — is deliberately left unmerged and follows the ordinary rules
(no heal on that side): the peer's own two raw legs then land on a single jack index of its own, so
the "same peer AND same deleted-module jack" pairing test never matches, rather than the heal trying
to guess a width mismatch. Collapsed (Dual I/O off), a split-block
module's one "Audio" jack is genuinely mono — its Right leg lives on a separate far block, not raw
ch1 — unlike an auto-derived FX shape's collapsed jack (Chorus, Reverb, Delay, Distortion...),
which owns both raw legs of a real stereo pair under that one jack; either way it is still exactly
one audio leg, so the module remains heal-eligible. A bare graph I/O node (Audio Input/Output) has
no jack-collapsing of its own for painting or hit-testing purposes — GraphEditorCables.cpp's
`rebuildVisibleCables` still treats each of its raw channels as its own distinct visible cable — but
this heal's OWN eligibility count is more specific: two legs landing on a bare I/O node's matching
raw channel pair (0/1) off the SAME source jack of the deleted module are still one logical cable,
not two (`mergeBareIoChannelPairs` in `GraphEditorDeleteHeal.cpp`). This is the ticket's own
motivating case — deleting the last effect before Audio Output (Osc → Filter → Reverb → Audio
Output, delete Reverb) — which now heals: the surviving Filter is wired directly into both of
Output's legs. Two legs that land on the SAME raw pair but come off two DIFFERENT source jacks (a
splitter feeding Left and Right separately) are not merged and remain two legs, unhealed, as do two
legs that land on two DIFFERENT destination nodes even on matching channel numbers — the merge keys
on the peer node's identity, not just the raw channel pair. When the surviving upstream module is
itself mono (Filter's own collapsed jack, confirmed genuinely mono above) rather than a real stereo
pair, the healed cable still reaches both of Audio Output's legs: `resolvePolyLink`'s null-destination
branch broadcasts a mono Audio source onto both raw legs of Output's Left slot, the same as a
manual mono cable dropped there would, "so a mono chain does not go silent in one ear" — unless the
source module is itself Dual I/O SPLIT (a genuine, separately-wireable Right jack of its own), in
which case that Right jack is left for its own cable rather than broadcast over. Modulation/CV
cables on the deleted module are simply dropped either way — they are never candidates for the
heal.

Deleting several modules in one selection heals across the whole deleted RUN, from the nearest
surviving upstream node to the nearest surviving downstream one, as long as every module along that
run is itself exactly one audio leg in / one out; a run that includes a branching module (more than
one leg on a side) is left unhealed past that point, same as a single ineligible deletion. The
healed connection is re-validated after the deletion actually happens with
`AudioProcessorGraph::isAnInputTo` (rejects a cycle — `canConnect` alone does not check for one)
and `::canConnect` (per-leg legality: valid channel, not already connected) — the same checks a
manual cable drag relies on; an invalid result skips the heal rather than forcing a bad edge. The
heal runs BEFORE the macro-port/attenuverter auto-delete sweeps
([`../macros/auto-ports.md`](../macros/auto-ports.md)), so a macro port that a heal just gave a
fresh cable to is no longer orphaned by the time those run, and it composes with a macro port or
hidden Attenuverter sitting on either end of the deleted run — the heal just wires the two
surviving endpoints, whatever kind of node they are. It never runs for a deleted macro port node
itself, and a port anywhere in the middle of a deleted run leaves the run unhealed past it — deleting
a port is a separate feature with its own preference (`spliceCableOnMacroPortDelete`, default OFF:
the cable is dropped), and this generic, default-ON heal must not silently override that dedicated
default (see [`../macros/auto-ports.md`](../macros/auto-ports.md) for the full ordering story). The
whole delete (plus any heal it triggers)
is one undo step: Cmd+Z restores the deleted module(s) and their original cables and removes the
healed cable in the same stroke; redo re-applies both together. Audio Output is never removed by
any of this — deleting it is unaffected, and it is never eligible to be healed away.
Implementation lives in its own unit, `Source/UI/Graph/GraphEditor/GraphEditorDeleteHeal.cpp`
(`GraphEditor::captureHealSplices`/`::healDeletedChain`), called from both delete entry points.

`applyHeaderButtonIcons()` retints the header buttons from the active `AppLookAndFeel`. It is
null-guarded: when the look-and-feel cast fails (headless tests) the function returns early and the
buttons stay imageless but functional. `lookAndFeelChanged()` calls it, so icons update on a theme
switch.

The header title text is drawn by `AppLookAndFeel::drawModulePanel()` with an asymmetric inset,
`header.withTrimmedLeft(22.0f)`, so the activity LED (`fillEllipse(6, 8, 8, 8)`, x from 6 to 14)
never overlaps the title regardless of whether the LED is lit.

## Knob modulation-amount gesture

Every rotary knob a card body builds (`CardBody::createKnob`, for the float and int parameters) is
a `synth::ui::CardKnobSlider` (`CardKnobSlider.h`), not a plain `juce::Slider` — it can redirect its
own mouseDown/drag/up to `ModuleComponent::handleModAmountGesture()` instead of moving the knob,
when `wantsModAmountGestureFor()` says the click should adjust a routed AttenuverterChain's amount
(Alt-drag, or a drag starting on the ring's own annulus). A card fader takes the same gesture (Alt-drag,
or a drag starting on its modulation bar); both route their mouse through `CardControlGestures` and are
wired by one call, `ModuleComponent::wireCardControlGestures`. See
[`modules/modulation.md#drag-the-ring-to-adjust-a-routings-amount-without-touching-the-knob`](../modules/modulation.md#drag-the-ring-to-adjust-a-routings-amount-without-touching-the-knob)
for the gesture itself and the depth band it moves; hosted-plugin card knobs
(`ModuleComponentHostedPluginCard.cpp`) stay plain `juce::Slider`s — they are never modulation
targets.

## Automated marker

A knob or fader whose parameter has an automation lane on the timeline shows a small marker, so you can tell at a
glance what is automated: a short slanted line with a dot at each end (about 8 by 6 px, 2 px dots, a 1.5 px stroke),
at the **top-left** of the control's cell in the theme's `textPrimary` at 70 percent. The violet MIDI-mapped dot keeps the
top-right, and the two never overlap (`automatedMarkerRect` against `midilearn::midiMappedDotRect`). It is only a marker:
no click, no focus stop. It fades in over 160 ms and out over 110 ms (a plain 80 ms fade under Reduce Motion), driven by
one time-bounded `AutomatedMarkerTicker` per card, made the first time a marker changes; nothing repaints once it has
settled.

The card asks the host once per gated 15 Hz tick, like the MIDI badge: `GraphEditor::onQueryAutomatedParamsForNode(nodeId)`
returns the paramIDs with a lane (`MainComponent` answers from `TimelineDoc::automatedParamIds`), and
`ModuleComponent::refreshAutomatedMarkers()` (`ModuleComponentAutomationMarker.cpp`) moves each registered control towards
the answer, so adding, removing or undoing a lane is followed within a tick. The control's tooltip gains an
"Automated: <parameter>" line and its accessible description says "Automated" (a screen reader reads "Cutoff, 1.2 kHz,
automated") for as long as the lane exists. The glyph and fade live in `Source/UI/Layout/AutomatedMarker.{h,cpp}`; the
mixer uses the same ones ([`mixer/panel.md#automation-markers`](../mixer/panel.md#automation-markers)). Tests:
`AutomatedMarkerTests.cpp`, `ModuleComponentAutomationMarkerTests.cpp`, `AutomationMarkerMainTests.cpp`.

## Modulation dot

A knob or fader a modulation cable lands on also carries a transparent `synth::ui::ModDotButton`
(`UI/Graph/ModDot/ModDotButton.h`), centred on the landing dot (about 16 px square). It is the dot's
keyboard and screen-reader face: its own Tab stop right after its knob (`getKeyboardControls` sorts it
with the knob it belongs to), named "<Param> modulation, N sources" with the tooltip "Modulation
sources for <Param>", the solid accent focus ring around the dot, Up/Down for the last-chosen
source's amount (Shift: ten times the step) and Return/Space, which open the dot's panel like a plain click on the dot (a
press that moves under 3 px; see
[`modules/modulation.md#the-mod-dot-menu`](../modules/modulation.md#the-mod-dot-menu)). While the panel is open the
button draws the accent ring around the dot even without keyboard focus. It exists only
while the knob has an attenuverter routing and is shown, so it is hidden with a knob on another tab
page or one the layout hides; `ModuleComponent::syncModDotButtons` rebuilds it on every layout and
whenever the GraphEditor tick sees the routing set change. The mouse passes through it to the knob. See
[`modules/modulation.md#the-mod-dot-drag-an-amount-from-the-landing-dot`](../modules/modulation.md#the-mod-dot-drag-an-amount-from-the-landing-dot).

## Custom card titles

Double-clicking a card's **header band** (`ModuleComponent::kHeaderHeight`, 24 px) opens an inline
`juce::TextEditor` over the title. Return or a click away commits, Escape cancels, and the editor is
seeded with whatever the header currently shows, so a first rename starts from `Chorus 2` rather
than an empty box. It is a **child component**, so there is no window seam to stub out for a
display-less test run, and it is themed for free — `AppLookAndFeel` already overrides
`fillTextEditorBackground` and `drawTextEditorOutline`.

**The custom title is NOT the processor's name.** `ModuleBase::getName()` is the auto-numbered
`"Chorus 2"`, and `AudioEngine::updateModuleNames()` recomputes every one of those **wholesale on
every graph change**: it strips a trailing number off each name and renumbers per base type. A
custom title written there would be clobbered by the next node the user adds, and worse, `"My
Chorus 2"` would have its `2` stripped and be renumbered into `"My Chorus 3"`. So the custom title
lives in the node property **`displayName`**, and the numbered processor name stays the fallback:
`GraphEditor::getModuleTitle()` returns the custom one when set, else `processor->getName()`. Card
paint goes through `ModuleComponent::cardTitle()` and never reads the processor name directly.

**A lone module of a base type gets no number at all** (FRO181): `updateModuleNames()` counts every
base type FIRST, then only appends `" <n>"` to a type that has 2+ live instances — the single
`"Oscillator"` on a fresh patch stays bare, never `"Oscillator 1"`. This matters because the pass
runs incidentally, not only when the user visibly adds/removes a module: `ModMatrixComponent`'s
own node-count watchdog (and `GraphEditor::replaceModule`'s "Refresh UI" step) call it whenever the
graph's total node count changes for ANY reason, including a macro's own inlet/outlet port
splicing itself in and back out again on group/ungroup/undo/redo — none of which is a module the
user thinks of as added or removed. Before the fix, that incidental call gave every bare,
never-yet-numbered module a spurious `" 1"` the first time it ever ran, and it stuck from then on
(the pass is otherwise idempotent, so nothing later reverted it). Pinned end-to-end by
`Tests/Macros/MacroAutoPort/MacroAutoPortNameTests.cpp` (through the real
`MacroGroupController::groupSelectionIntoMacro` entry point) and at the unit level by
`ModMatrixTests.cpp`'s `UpdateModuleNames*` cases.

Consequences worth knowing:

- Renaming one instance does not disturb any other instance's number, and a later instance still
  auto-numbers correctly — a renamed `Chorus 1` does not free up the name, because the count is per
  base type over all nodes. Pinned by `AutoNumberingStillAppliesAlongsideCustomTitles`.
- Blank or whitespace-only reverts to the numbered default rather than showing an empty header.
- Typing the numbered name back in stores **blank**, so the card keeps following the numbering
  instead of freezing today's number as a literal custom title.
- `displayName` is message-thread-only and display-only, so it is deliberately **not** mirrored into
  the processor the way `uuid` is. Nothing on the audio thread reads a card title, so there is no
  lock-free read to make sound and a mirror would only add a second copy to keep in sync.

**Gesture ordering.** The double-click is intercepted in `mouseDown` on `getNumberOfClicks() >= 2`,
*before* `dragStartPosition` and `bodyDragActive` are touched — the same interception the port
double-click uses. The first click of the pair has already armed and disarmed a body drag; letting
the second arm another would leave `bodyDragActive` set under an open editor, so the next stray
`mouseDrag` would walk the card out from under the cursor. Pinned by
`ModuleTitleRenameDoesNotArmABodyDrag`.

**Undo** goes through `recordStructuralChange`, which is also what makes the title survive an
undo/redo of any *other* structural change: the snapshot is `graphToJSON`, so the title has to be
serialized for undo to restore it at all — see
[patch-format](../ai/patch-format.md#per-node-displayname).

## Audio Output card identity

Audio Output is a bare `juce::AudioGraphIOProcessor`, not a `ModuleBase`, so it otherwise renders
through the exact same generic path as every other card — its `getType()` falls back to
`ModuleType::Oscillator` (see `detail::getType` in `ModuleComponentInternal.h`). Two small, purely
additive blocks in `ModuleComponent::paint()` give it its own identity, both gated on a local
`isAudioOutputIONode(juce::AudioProcessor*)` helper: a `dynamic_cast` to `AudioGraphIOProcessor`
plus an `IODeviceType == audioOutputNode` check — the same type-not-name idiom `isTerminalAudioSink`
in `GraphEditorInternal.h` uses for cable routing, so a `ModuleBase` named "Audio Output" cannot
impersonate the sink.

**Identity glyph.** The `synth::theme::Icon::CatIO` speaker glyph sits in the activity LED's slot.
Audio Output is never a `ModuleBase`, so it never has a `VisualBuffer` and `cachedRMS` never leaves
`0.0f` for this card — that slot is otherwise always dark. It is sized and positioned by
`ModuleComponent::outputCardIconBoundsForTest()`, public purely so a test can assert the exact
geometry `paint()` draws, rather than by a fixed box:

- **Size** — a square equal to the title's cap-height, computed as `titleFont.getAscent() * 0.72f`
  for the title's own font (`theme.type.h2`, bold). JUCE has no direct cap-height query, and 0.72x
  ascent is the standard sans-serif approximation, close to Inter's real ratio. This keeps the glyph
  proportional to the title — roughly cap-height, about 9-10 px at the default 13 pt title size —
  instead of the header's full 24 px band, which read a touch large and heavy next to the title.
- **Vertical position** — centred on the title's cap-height optical centre, derived from the SAME
  centred-text-box math `drawText` uses to place the title (`textBoxTop = headerTop + (headerHeight
  - titleFont.getHeight()) / 2`, `baseline = textBoxTop + titleFont.getAscent()`, `capCentreY =
  baseline - capHeight / 2`) rather than the header band's raw geometric middle. Centring on the
  full ascent-plus-descent box the title is drawn in reads slightly low against the capital and
  x-height glyphs, which sit above the descender clearance that box reserves (a title is in normal
  case, so its g, p and y do use that clearance: it fits inside the 24 px header with room to spare,
  pinned by `ModuleCardTitleTests`).
- **Horizontal position** — the right edge is pinned to `x = 14`, the activity LED's own right edge
  (`fillEllipse(6, 8, 8, 8)`), so the gap to the title's left inset at `x = 22` stays the
  established 8 px rhythm regardless of the glyph's resulting width.
- **Colour** — re-tinted per paint from the library's baked-in `textMuted` to whichever colour token
  the title is using this frame: `accent` when selected, `textDisabled` when bypassed, `textPrimary`
  otherwise, mirroring `drawModulePanel`'s title-colour logic exactly. It uses
  `Drawable::replaceColour` on an owned clone from `AppLookAndFeel::getIcon()`, never the shared
  `peekIcon()` view other cards and the library sidebar also read, so the glyph and the title always
  read as one lockup instead of a fixed muted tint beside a colour-shifting title.

**Destination line.** One muted line (`theme.type.micro`, `colors.textMuted`) under the header, for
example `"MacBook Pro Speakers · 48 kHz · 2ch"`. It is sourced **message-thread-only** from
`AudioEngine`'s device state and pushed in through a small chain rather than read directly:
`MainComponent::computeOutputDeviceInfoText()` branches on `AudioEngine::isHosted()` — Hosted mode
has no device manager, so it returns the fixed string `"Host audio"` instead of touching one, and a
Standalone build with no device open yet returns an empty string — and is installed as a
`std::function<juce::String()>` via `GraphEditor::setOutputDeviceInfoProvider`;
`GraphEditor::refreshOutputDeviceInfo()` calls it and pushes the result into the Audio Output
`ModuleComponent` via `setOutputDeviceInfoText()`. An empty string means "draw no line", not an
empty one.

There is no timer: `refreshOutputDeviceInfo()` is called once right after the provider is installed,
so the card is populated at startup, and again from `AudioEngine::onDeviceStateChanged`, alongside
the existing `refreshIoModulesAfterDeviceChange()` call that already re-points Audio Input's jacks
on a device change. `setOutputDeviceInfoText()` repaints — the normal `ZoomFrozenCachedImage`
invalidation seam, see [rendering](rendering.md) — only when the text actually changed, and is a
no-op on every module that is not the terminal audio sink, so a caller never needs to check
`isAudioOutputIONode()` first.

The library sidebar's "I/O" category header (Audio Input / Audio Output) uses the same `CatIO` icon
via `ModuleLibraryComponent::categoryIconForHeader` — see [icons](icons.md).

## Wavetable card

The Wavetable card is **double-width**: 15 knobs, 8 combos and a 16-jack port stack. Laid out flat
that came to 560x869 -- technically correct and genuinely unusable, a wall of identical knobs with no
hierarchy. Three mechanisms bring it to 560x578 and give it a reading order.

**Two-column jack gutter, both columns on the LEFT.** `getInputPortColumns()` returns 2 for a card
with more than 10 drawn input jacks at double width; `getPortCenter()` then lays the inputs out
**column-major** -- jack 0 top-left, running down then over -- at `kPortColumnStride` (100 px) apart.
Sixteen jacks in one column set a roughly 390 px floor on the card height before a single control is
placed. Only the pinned Position and Warp Amt knobs take their jacks; the other fourteen are drawn, in
two columns of seven, on every tab ([tab strips](#tab-strips)).

**Inputs stay on the left, outputs on the right -- that convention is not negotiable for a saving in
height.** It is what makes signal flow read left to right across a patch. Spilling the overflow down
the right edge was tried and reverted: it reads as an output, and inputs and outputs are drawn
identically, so the side is the only cue there is. The real objection to an interior column -- the
module covers the lower half of a cable being dragged towards it -- is answered by not aiming at the
gutter at all; release the cable **on the destination knob** instead, see
[modulation.md](../modules/modulation.md#drag-to-knob-modulation).

Everything that touches jack geometry -- wire drawing, hit-testing (`getPortForPoint`), painting --
reads `getPortCenter`, so this is the only place that changes. One consequence:
`getContentTopY()` takes the **maximum** y over all inputs rather than the last one's, because with
more than one column an odd jack count leaves the second column a row short, so the last jack is not
the lowest.

**Tabs are layout data.** The controls are the card body's, placed by the Wavetable's code default
(`DefaultCardLayoutsSources.cpp`): an untitled section with Warp, Position and Warp Amt pinned above
the strip (they are what you actually perform with; the Warp combo sits on its own row above the two
knobs, because a run holds one widget kind), then five `tab` sections -- Tune (Octave, Coarse, Fine,
Level, Pan), Unison (Stack; Unison, Detune, Width, Blend), Phase (Sync In; Phase, Rand Phase, Spread),
Sub (Sub Oct, Sub Wave; Sub) and File (Import, Interp) -- drawn as one tab strip, then a footer with
the Poly and Show Scope pills. Each tab stacks its combos above its knobs, and its section's columns
size the knob cells to its count. The strip, its keys, its accessibility and the rule that a tab switch
never moves anything are the card body's ([tab strips](#tab-strips)); Edit Layout lists each tab as a
group and can hide, move and rename within it. **Table** is not a body control: it sits in the chrome
beside the display it selects (`ModuleComponent::createWavetableTableSelector`; the body's plan skips
the parameter).

**Chrome beside the ports.** The display, the `Table` combo, the button row and the caption sit
**beside** the jack stack, starting 60 px down (`ModuleComponent::layoutWavetableChrome`) -- the same
reclaim the Parametric EQ card makes for its response curve (see [visualizers](visualizers.md)). Its
bottom, `ModuleComponent::wavetableChromeBottomY()` (242), is also what the size estimate reads; the
body starts below both it and the lowest jack.

The card is measured from its plan like every layout card (`CardBodyMeasure.cpp`: double width, the
two-column gutter, the chrome bottom), pinned by `EstimatedModuleSizesMatchTheRealComponents` and
`WavetableDefaultLayout.TheEstimateEqualsTheRealCard`; `ModuleComponentWavetableTests.cpp` guards the
gutter, drop targets and rings, and `WavetableDefaultLayoutTests.cpp` the tabs.

`ModuleComponent` also builds two file affordances for Wavetable modules: a **"Load Wavetable..."**
`TextButton` opening an async `juce::FileChooser` starting in the module's browser folder, which on
success calls `loadWavetableFile()` and switches the module's `table` choice to `Loaded File`; and a
**folder browser row**, `[Folder...] [<] [>] [caption]`, where `Folder...` opens an async directory
chooser and calls `setWavetableFolder()`, `<` and `>` step the folder cursor, and the caption shows
the loaded file name and `index/total`. Both completion lambdas hold a `Component::SafePointer` and
re-derive the module via `dynamic_cast`, since the card can be destroyed while the dialog is open.
The card also implements `FileDragAndDropTarget` for audio files, routing drops through the same
import path as the Load button.
