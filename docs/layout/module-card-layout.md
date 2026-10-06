# Module card layout — data-driven cards for built-in modules

Agent reference. The design for giving every built-in module a card drawn from layout data instead of
type-specific code: a hand-designed default per module type, a user override per instance or per
type, knob/fader/switch widgets, and room for a future user-built "custom module".

**Status:** model, store, override, `CardBody`, the new widgets, the right-click quick path and the
layout editor built; every card draws the automatic layout until the user changes it. Built: `CardLayout`
v2 and its reader/writer, the shared layout store with its `ModuleCardLayouts` root (the app owns one and
every card resolves its per-type default against it), the per-instance `cardLayout` node property with
its undo, the pure resolver with an empty code-default registry, `CardBody`, which builds and lays out
every built-in card's body from the resolved layout, with the folded More row, section header rows,
label overrides and the Threshold and Envelope views in its view registry, the `knobLarge`, `faderV`, `faderH`,
`segmented` and `stepper` widgets, Hide from card / Show on card / Show as fader / Show as knob on every
control's right-click menu, the **Edit Layout...** editor shared with the hosted plugin's picker,
conditions read live (dim, swap groups, conditional sections, code dim rules), the footer row with the
small toggle pill, `presentation: tab` sections drawn as one tab strip, and the code-default registry
split into one unit per module family ([What exists](#what-exists)). Everything else (spans, the default layouts themselves) is designed and
decided (see [Decisions](#decisions-2026-10-01)), not built. Nothing here describes
current behaviour unless it says "today" or "built".
Where the card is drawn today is [module-card.md](module-card.md); the hosted-plugin half of the
same idea, which is built, is [plugin-card-layout.md](../control/plugin-card-layout.md).

---

## Today

- Every built-in card's body is built by `CardBody` (`Source/UI/Graph/CardBody/`): one widget per
  parameter by JUCE type (choice → `ComboBox`, float/int → `CardKnobSlider`, bool → toggle), in
  declaration order. No module has a code default, so every card draws the **automatic layout**: all
  combos, then all toggles, then the Threshold view, then a 3-column knob grid, each group in
  declaration order, pixel for pixel the card that existed before `CardBody`. A stored layout (the
  node's `cardLayout` override, else the type's stored default) is honoured for order, grid columns,
  hiding, widget choice (where the widget suits the parameter), section titles (a header row) and
  label overrides (the caption), conditions and the footer row; spans are not drawn yet. The user changes it from a
  control's right-click menu or the layout editor ([Editing a layout](#editing-a-layout)).
- Exceptions are hard-coded: skip rules for the ADSR curves/divisions/tempo-sync and the threshold
  parameter and the Wavetable's Table choice (`CardBodyPlan.cpp`'s `isEditedElsewhere`), the LFO
  custom-wave editor, the Sampler/Wavetable chrome, and fully bespoke bodies for Sequencer, Poly
  Sequencer, MIDI Keyboard, Macros, Attenuverter, Parametric EQ and External MIDI. The Wavetable's
  tab strip is layout data (`tab` sections in its code default).
- Dual-mode modules show both modes' controls at once (LFO Hz and Sync Rate; Sample & Hold Rate with
  an external clock; Sampler grain knobs in Sample mode; Pitch Shifter semitones and Hz).
- `CardLayout` (`Source/Modules/CardLayout.h`): hosted-plugin cards use the flat version 1 slot list;
  built-in cards resolve a version 2 layout through `CardBody` ([What exists](#what-exists)).

---

## What exists

| Piece | Where |
|---|---|
| `CardLayout` v2 (sections, items, conditions, `hidden`, `basedOn`), `upgradeV1`, `usesV2Features` | `Source/Modules/CardLayout.{h,cpp}`, `CardLayoutJson.cpp` |
| Store with two roots: `PluginCardLayouts/` and `ModuleCardLayouts/<ModuleType>/` | `Source/Plugin/Hosting/CardLayoutStore.*` (shared), `Source/UI/Graph/CardBody/ModuleCardLayoutStore.*` |
| Instance override: node property `cardLayout`, get/set with one undo step | `Source/UI/Graph/CardBody/CardLayoutOverride.*` |
| Resolver (instance, type default, code default, automatic) and the empty `DefaultCardLayouts` registry | `Source/UI/Graph/CardBody/ModuleCardLayoutResolver.*`, `DefaultCardLayouts.*` |
| `CardBody`: builds, binds and lays out a card's body; the folded More row | `Source/UI/Graph/CardBody/CardBody.*`, `CardBodyLayout.cpp`, `CardBodyMoreRow.cpp`, `CardBodyMoreButton.h` |
| The plan (which widget per parameter, the skip rules, placement, More) and the run layouts | `CardBodyPlan.*`, `CardBodyGeometry.*`, `CardBodyLayoutWalk.h` |
| The view registry (`threshold`, and the ADSR's `envelope`) | `CardBodyViews.*` |
| Conditions read live: `when` (dim, swap groups), `visibleWhen`, code dim rules, the parameter listener | `CardBodyConditions.cpp` |
| The footer row (`CardSection::kFooterId`) and the small toggle pill | `CardBodyFooter.cpp`; `Source/UI/Graph/CardWidgets/CardTogglePill.h`, `AppLookAndFeel::paintTogglePill` |
| Tab groups (consecutive `tab` sections) and their strip, the selected tab | `CardBodyPlan.*` (`TabGroup`), `CardBodyLayout.cpp`, `CardBodyTabs.cpp` (the strip is a `CardSegmentedSwitch`) |
| One registration unit per module family, and the `cardlayout::` builders | `Source/UI/Graph/CardBody/DefaultLayouts/` |
| The size estimate for a card before it exists, measured from the plan | `CardBodyMeasure.*` (`GraphEditor::estimateModuleSize`) |
| The widgets: `CardFader`, `CardSegmentedSwitch`, `CardStepper`, and `CardControlGestures` (the gestures a knob and a fader share) | `Source/UI/Graph/CardWidgets/` |
| The right-click quick path (the explicit layout, the edits, the undoable write and rebuild, the menu items) | `CardLayoutQuickEdit.*`; the stale-card rebuild in `GraphEditorCanvas.cpp` (`CardBody::isStaleFor`) |
| The app's store, bound to the GraphEditor's cards; a default written or cleared rebuilds that type's cards | `ModuleCardLayoutBinding.*` (owned by `MainComponent`) |
| The layout list: the panel, its rows, its working model, and the two sources (built-in module, hosted plugin). Only a hosted plugin's "Edit Layout..." opens it now | `Source/UI/Graph/CardLayoutEditor/`; `ModuleComponentHostedPluginCard.cpp` opens it |
| The on-card editor ("Edit Layout...", the editor of every built-in card): the overlay, one outline per control (a grip on the movable ones), the edit bar (Preset, Apply to, Cancel, Done), the drag, drop and nudge, and its owner on the GraphEditor | `Source/UI/Graph/CardLayoutEditor/OnCard/` (`CardLayoutOnCardEditor*.cpp`, `CardLayoutOutline.*`, `CardLayoutEditBar.*`, `OnCardEditorOwner.*`) |
| The ADSR "Time and tempo" switch: the pure Shared/Separate conversion of the stages group, and the strip switch that writes it | `CardBody/DefaultLayouts/AdsrTimeTempo.*`; `OnCard/CardLayoutOnCardEditorTimeTempo.cpp` |
| Apply to and Preset: the two menus, the scope every write follows, Save as (the shared name prompt), Reset | `OnCard/CardLayoutOnCardEditorScope.cpp`; `CardLayoutEditor/PresetNamePrompt.*`; the writes are `BuiltInCardLayoutSource` |
| "+ Add control": the strip under the card, the searchable panel and its rows, the click and the drag-out drop, and the pure list, search and layout edit behind them | `OnCard/CardLayoutOnCardEditorAdd.cpp`, `...AddDrop.cpp`, `CardLayoutAddPanel.*`, `CardLayoutAddRow.*`, `OnCardAddControlModel.*`, `findFreeSpot` in `OnCardLayoutMath.*` |
| The per-control panel (Show as, Label, Range, Hide from card): its fields, how they open in a call-out and stay anchored to the control, and the pure edits and range rules behind them | `OnCard/CardLayoutControlPanel.*`, `CardLayoutOnCardEditorPanel.cpp`, `OnCardControlOptions.*` |
| Snapping to guides and pushing a crowded neighbour aside, as pure functions on rectangles | `OnCard/OnCardLayoutMath.*` |
| Which controls are outlined (read off the plan's real widget bounds), and the free positions written back | `OnCard/OnCardCells.*`; `CardBodyPlan::Section::cellTop` / `cellBottom` (set by every live layout pass) |
| A fader's modulation bar and drop outline beside the knob rings | `Source/UI/Graph/ModuleComponent/ModuleComponentModRings.cpp` |

As built: the card resolves its layout once, when it is built (instance override, then the type's
stored default, then code default, then automatic). `MainComponent` owns one `ModuleCardLayoutStore`
(the real `<settings>/ModuleCardLayouts`) and binds it to its GraphEditor with a
`ModuleCardLayoutBinding`, which hangs the store on the editor's property set (no `GraphEditor` member)
and listens to it: a default written or cleared bumps the store's in-memory revision for that type, and
`updateComponents` rebuilds every card built from an older revision (`CardBody::isStaleFor`, which never
reads the disk), each rebuilt card then making room for its new height. A card on an editor with no
binding (most tests) skips the per-type step. The size estimate for a card before it exists
(`CardBodyMeasure`) measures the type's code default (or the automatic layout) with its conditions read
at the fresh module's values, so it never sees a stored default: a type with one is placed at its
code-default height and re-flows once built. A stored layout is honoured only for the cards drawn from layout data; the bespoke ones (Sequencer, Poly
Sequencer, Macros, Parametric EQ, Attenuverter, macro ports) always build from the
automatic plan. The fold state of the More row is per card and not saved; a card opens folded.
A card builds its body once; when its node's `cardLayout` changes (a quick-path click, its undo or
redo), `GraphEditor::updateComponents` sees that the card was built from a different override and
rebuilds that card in place (same canvas order; the old card lets go of the processor first). Making
room is the quick path's own, inside its undo record; a restore takes positions from its snapshot.
A layout whose only content is one untitled grid section of Auto/Knob/Toggle/Choice items is still
stored in the v1 form (the writer's rule below); the resolver upgrades it on read.

Reader rules as built: a layout is either the flat v1 `slots` list or v2 `sections`, never both. Reading
v1 fills `slots` (the flat form is the implicit single untitled grid section) and `upgradeV1(layout,
allParamIds)` turns it into one section plus a `hidden` list of every parameter it did not name; call it
once, where the parameter list is known (the resolver does when handed one). Unknown keys are ignored,
a version above 2 is refused, and an out-of-range number (`columns`, `span`, `indexHint`) or an
unrecognised widget, view, presentation or effect name refuses the whole layout rather than quietly
changing it. `ParamItem` also carries the v1 `indexHint`, so a hosted plugin's rescue key survives.

---

## The layout model

`CardLayout` grows to version 2. It stays a plain value type in `Source/Modules/` with no UI or
plugin knowledge.

```text
CardLayout (v2)
  version  : 2
  basedOn  : string | null      // "<ModuleType>@<defaultRevision>" the user started from
  sections : Section[]
  hidden   : string[]           // paramIds the user hid; they render in the "More" row
Section
  id       : string             // stable within the layout ("pitch", "user-1")
  title    : string | null      // null = no header
  columns  : 1..6               // grid columns; a Double card doubles them
  presentation : grid | tab     // consecutive "tab" sections render as one tab strip
  visibleWhen  : Condition | null
  items    : Item[]
Item = ParamItem | ViewItem
ParamItem
  paramId  : string
  node     : string | null      // reserved: a node uuid, for the custom module (see below)
  widget   : auto | knob | knobLarge | faderV | faderH | toggle | choice | segmented | stepper
  label    : string | null
  span     : 1..6               // grid cells taken
  when     : Condition | null   // dim or swap, never resize (see below)
  x, y     : 0..4000 | null     // free position of the item's cell, both or neither (see Rendering)
  min, max : number | null      // narrows a knob or fader, in the parameter's own units; both or neither, min < max
ViewItem
  view     : scope | response | spectrum | envelope | lfoShape | lfoCurve | waveform
           | wavetable | eqCurve | threshold | gainReduction
  open     : bool               // collapsible views: open by default or not
  x, y     : 0..4000 | null     // free position, as a param item's; the on-card editor writes it for every view of an edited group so it stays put
Condition
  param    : string             // a choice or bool parameter of the same module
  is       : string[]           // choice VALUE strings ("Granular"), or "true"/"false"
  effect   : show | dim         // show = swap in place of a sibling; dim = greyed out
```

**Hidden, not shown.** A layout lists what it *hides*, not what it shows. Every parameter that is
neither placed in a section nor listed in `hidden` is appended to an implicit, folded **More** row at
the bottom of the card; parameters in `hidden` also render there. So a parameter a later release adds
to a module appears on every user-customised card automatically, in a known place, rather than being
silently missing.

**Why.** The v1 rule "absent from the layout = hidden" is safe for hosted plugins, whose automatic
default is recomputed, but not for built-in modules whose saved layouts outlive a release. It also
made a hidden parameter unreachable for a new modulation cable, because the cable-drop target lives
on the widget.

**Conditions never change the card's height while a value is being dragged.** `effect: show` is only
allowed where the swapped widgets occupy the same cell (LFO Rate in Hz vs a note division), or where
the condition's parameter is a mode switch the user sets deliberately (Sampler Sample/Granular,
Pitch Shifter mode, Sample & Hold clock). `effect: dim` greys a control out and keeps its space
(Detune while Unison is 1, LFO Glide unless the shape is S&H). Numeric conditions are not part of
the model; a dim rule over a number is expressed in the default layout's code, not in stored JSON.

**Why.** Card height drives canvas make-room ([layout.md](layout.md)); a card that grows while a
knob turns would shove its neighbours around under the cursor.

**Versioning.** Adding an optional key never bumps `version`; only a change of meaning does. The v2
reader keeps reading v1 (a v1 layout is one untitled grid section plus the v1 "absent = hidden" rule,
applied once on read and written back as v2). Hosted-plugin cards keep **writing** v1 unless a layout
uses a v2-only feature, so a project opened in an older build keeps its plugin cards. The writer picks
the version from `usesV2Features()`: one untitled grid section at the default width, holding only plain
parameters with the Auto, Knob, Toggle or Choice widget, and no `hidden`, view, condition, span, position,
range, `node` or `basedOn`, writes v1. Choice
conditions match value strings, not indices, so appending a choice value never breaks a layout.

**Section titles.** A code default's titles are plain English strings in code (the app has no string
table). A user layout stores a title only when the user renamed or added the section; `basedOn` lets
**Reset to default** and a later "update to the new default" diff against the default it started
from.

---

## Where a layout comes from

First hit wins, per node:

1. **Instance override**: a graph-node property `cardLayout` (JSON), stored and emitted like the
   custom card title `displayName`, not inside the module's extra state.
2. **Per-type user default**: `<settings>/ModuleCardLayouts/<ModuleType>/default.json`, presets as
   sibling `<name>.json` files, the same store shape as `PluginCardLayouts` (one generalised store
   class with two roots).
3. **Code default**: `DefaultCardLayouts.cpp` (UI side, keyed by `ModuleType`), one hand-designed
   layout per type, with a `defaultRevision` bumped whenever it changes.
4. **Automatic layout**: for a type with no code default, one grid section reproducing today's order
   (combos, toggles, knobs, in declaration order), so nothing regresses before a type is designed.

**Why a node property and not extra state.** A built-in module's extra state is the module's own
(`ModuleBase::getExtraState()`, overridden per type), and `applyExtraStateIfChanged` compares the
whole blob: a UI key inside it would make every layout undo re-apply the module's state, and the
Sampler and Wavetable reload their files from disk when that happens. A node property has no such
coupling, and the title property already round-trips through `graphToJSON`, `applyJSONToGraph`
and undo (`GraphEditorNodeActionsTests` pins a rename's undo). `applySnapshotPreservingNodes`
restores only position and uuid by name today, so the implementation checks that a node-preserving
undo restores `cardLayout` and adds it to that path if not. Hosted plugins keep their existing
extra-state key; nothing migrates.

Width stays per type ([module-card.md](module-card.md#width-buckets)): hiding parameters shortens a
card, it never moves it to another width bucket. The custom module is the only card whose width a
layout may choose.

---

## Rendering

A new collaborator class, `CardBody` (`Source/UI/Graph/CardBody/`), owns the widgets of a
layout-driven card: it resolves the layout, builds one widget per item, and lays them out with a
single `layout(bool apply)` function (the measure-and-apply rule from `layoutDefaultContent`).
`ModuleComponent`'s generic branch hands its body to `CardBody`; the bespoke branches are untouched.
Views are created through a small registry (`view id → factory`) so the existing scope, response,
envelope, LFO curve and threshold components plug in unchanged.

- **Bindings do not change.** Every parameter keeps its attachment, MIDI Learn registration,
  modulation-amount gesture and knob-bound CV jack exactly as today; `CardBody` only decides which
  widget and where. Widgets are looked up by `paramId` (`CardBody::findWidget`, and a modulation
  target resolves its knob through its bound parameter), not by display-name component ID; the
  component ID stays the display name for the bespoke cards and tests that read it.
- **Free sections.** A section with an item that has `x`/`y` is placed by position, not in runs:
  each cell (caption and widget) sits at that offset from the content edge and the section's top,
  kept inside the content width, and the section is as tall as its lowest cell. Items without a
  position flow after the positioned ones, left to right in rows below them. `min`/`max` narrow the
  widget only (the parameter keeps its full range), applied after the slider's attachment so the range
  survives. Both ride the same measure-and-apply function, so the measured card size matches the card.
- **Hidden parameters stay whole.** They keep their value, stay in the patch `params` map, stay
  automatable, MIDI-learnable and model-authorable. A cable dragged over the folded More row opens
  it, so a hidden parameter can still take a new cable.
- **Heights are measured.** `GraphEditor::estimateModuleSize()` asks `CardBody` to measure the
  resolved layout instead of its hard-coded table, and
  `ModuleComponentTest.EstimatedModuleSizesMatchTheRealComponents` keeps pinning the two together.
- **Conditions (built).** A card listens only to the parameters its conditions read (a
  `juce::AudioProcessorParameter::Listener` per watched parameter; the callback, which may run on the
  audio thread, only triggers an `AsyncUpdater`), re-reads them on the message thread, and re-lays out
  only when a result changed. A `dim` keeps the cell and marks the control dimmed (painted greyed by the
  look-and-feel, still enabled, focusable and operable, with an "Inactive in this mode" description and
  tooltip hint); consecutive `show` items testing one parameter are one **swap group** in one cell (a
  member repeating a condition already in the group starts the next group, so the ADSR's four
  time/division pairs, all testing `tempoSync`, are four groups),
  laid out in the run of its tallest member, every member fitted to that cell at its own height, so a
  swap never moves anything and a swapped-out knob keeps its knob-bound jack (`CardBody::isSwappedOut`);
  a lone `show` item is a group of one whose cell stays empty while it is off. A swapped-out item is
  not hidden and never joins More. A section's `visibleWhen` may change the card's height, which then
  goes through `GraphEditor::handleModuleResized` (make room, give it back). Choice conditions match
  value strings, bool ones "true"/"false"; a condition on a parameter the module lacks, or on a number,
  never applies its effect. Inside the More row conditions are ignored, except dim.
- **Code dim rules (built).** A `DefaultCardLayouts` entry may carry `CardDimRule`s (`paramId`, the
  watched parameter ids, a predicate on the module) for numeric tests the JSON cannot express. A rule
  describes the module, not the layout, so it applies to every layout the card resolves (instance
  override, stored type default or code default; `ResolvedModuleCardLayout::dimRules`) whenever its
  parameter is on the card: a card the user has edited keeps them.
- **Footer row (built).** A section with the reserved id `footer` is laid out last, under the card's
  chrome panels and above More, as one compact row (wrapping when full): toggles as the small pill,
  anything continuous as `faderH`, the rest as their own widget with an inline caption. On such a card
  the chrome toggles (Show Response, Show Spectrum, Show Scope) join the row as pills, and `poly`
  joins it unless the layout places or hides it; a card without a footer keeps its chrome rows.
  Show on card never puts a control into the footer. Details in
  [module-card.md](module-card.md#the-footer-row).
- **Tab strips (built).** A run of consecutive `presentation: tab` sections (the footer never counts)
  is one **tab group**: one strip across the content width, `kRowHeight` tall with a `kTabStripGap`
  above it, then the selected tab's section after another `kTabStripGap`, laid out where the run's first section stands. Each tab is labelled
  with its section's title (its id when untitled) and draws no header row. The group takes its tallest
  tab's height whichever is selected, measured by the same walk, so a tab switch never resizes the card;
  the other tabs' widgets are hidden (`CardBodyPlan::isOnCard` is false for them), keep their bounds,
  take no modulation ring or cable drop, and stay in the parameter arrays, bound, learnable and
  automatable. A knob on a tab, selected or not, never binds its CV jack
  (`ModuleComponent::getModTargetKnobAnchor`, and the size estimate's same rule): the jack stays in the
  gutter, so the jack layout is the same on every tab, while a cable dropped on the knob still lands on
  its jack. The strip is a `CardSegmentedSwitch` (the joined-button look the Wavetable's own strip
  had): one Tab stop, Left/Right step through the tabs and stop at the ends, Home/End jump, the accent
  focus ring, titled "Control tabs" with a tooltip naming its keys, a group of radio buttons named after
  the tabs to a screen reader. The strip is created after every parameter widget, so the controls' child
  and screen-reader order stay the declaration order. The selected tab is the card's own (a rebuilt card
  opens on its first tab) and is never saved, as the Wavetable's page never was. A tab section's
  `visibleWhen` hides its controls, not its tab. Details in [module-card.md](module-card.md#tab-strips).
- **Bespoke cards.** Sequencer, Poly Sequencer, MIDI Keyboard, Macros, Attenuverter, Parametric EQ
  and External MIDI keep their own bodies; their editable surface is at most hide/reorder of a plain
  knob row they expose. The Wavetable is a layout card: its pages are `presentation: tab` sections of
  its code default (built). ADSR, LFO and Sampler become ordinary layouts with views.

---

## Widgets

| Widget | Use | Notes |
|---|---|---|
| `knob` | the default for continuous values | today's `CardKnobSlider` |
| `knobLarge` | the one control a module is "about" (Cutoff, Drive, Delay Time) | same widget, 60 px dial, spans one cell |
| `faderV` | levels and stages compared side by side (ADSR stages, Reverb Dry/Wet, Limiter Input/Ceiling) | new `CardFader` |
| `faderH` | a single level across the card (VCA Gain, Voice Mixer Level, footer output trims) | new `CardFader` |
| `segmented` | a choice with up to about 6 short values (waveform, filter family, clock source) | new; the ADSR MS/BPM control is the precedent; a bool parameter draws as two segments, its off and on texts (the ADSR's Tempo Sync reads "Time" and "Tempo") |
| `choice` | longer choice lists | today's `ComboBox` |
| `toggle` | booleans | today's toggle; footer toggles are the small pill |
| `stepper` | a small integer with a musical reading (MIDI Keyboard octave) | new; − value + |

`CardFader` is a `juce::Slider` in a linear style, painted by `AppLookAndFeel::drawLinearSlider`,
with the shift-fine and reset gestures of `MixerFaderSlider` and **every** gesture `CardKnobSlider`
carries: MIDI Learn, the modulation-amount drag, the cable-drop target and the right-click menu. A
fader shows modulation as a 3 px bar beside the slot from the base value to base + CV, in
`mod-ring-positive` / `mod-ring-negative`.

As built: `knob`, `knobLarge` (the same `CardKnobSlider`, 80 px tall in its cell), `faderV`, `faderH`,
`segmented` and `stepper` are drawn; a widget that does not suit its parameter (a fader on a choice, a
segmented switch over more than 6 values or a value over 10 characters, a stepper on a float or over
more than 24 steps) falls back to the automatic one. The knob and the fader share their gestures through
`CardControlGestures` and one wiring call (`ModuleComponent::wireCardControlGestures`); the fader's own
drag, sizes, bar and the switch's and stepper's behaviour are in
[module-card.md](module-card.md#faders-switches-and-steppers). The footer row and the small toggle pill
are built ([module-card.md](module-card.md#the-footer-row)).

Every card has the same **footer** row: Poly (where the module has it), the Scope / Spectrum
toggles, and an effect's output Level as a small horizontal fader. The **More** row sits under it.

---

## Default layouts

The hand-designed default per type. "New" marks a parameter that does not exist yet; a default
layout only ever names parameters that exist, so each one joins its default when (and if) it is
added.

| Module | Sections, top to bottom | Contextual rules |
|---|---|---|
| Oscillator | waveform `segmented`; Pitch: Octave, Coarse, Fine, Glide (one row of four); Unison: Voices, Detune, Pulse Width; Output: Level, Pan; footer (Poly, Show Scope) | Detune dims at 1 voice (a code dim rule); Pulse Width dims unless Square. As built: every parameter is placed, so no More row |
| Filter | `response` view open; Type `choice`; Cutoff `knobLarge`, Resonance, Drive; Modulation: Key Track, Level; footer with Spectrum | Key Track does not dim on an unplugged Pitch input yet: CardBody has no cable knowledge; Key Track is inert when unplugged. As built: the response panel stays card chrome, closed until opened; Show Response and Show Spectrum are footer pills, so there is no `response` item in the layout |
| VCA | Gain `faderH`; footer | — |
| ADSR | `envelope` view open; Time/Tempo `segmented`; A H D S R `faderV`; (new Velocity) and the `threshold` view; footer | Tempo mode swaps each stage's time for its division in place. As built: Velocity is a `faderH` row; the Time/Tempo caption is "Stage times"; captions Atk, Hold, Dec, Sus, Rel; the footer holds Poly, Show Envelope Graph and Show Scope; the same entry is registered for Amp Env and Filter Env; the card is 597 px tall |
| LFO | Shape; Sync switch; Rate `knobLarge` on its own row; Phase, Fade in, Level, Glide (one row of four); footer: Bipolar, Retrig | Rate swaps Hz and the division in one cell on Sync; Glide dims unless S&H. As built: Shape stays a combo (six values do not fit one switch); the Sync control is a Free/Sync `segmented` switch on the `mode` bool (its own off and on texts); no `lfoShape` view (no component to wrap); the custom-wave editor stays card chrome and opens under the body on Custom, so Draw needs no `lfoCurve` view in the body |
| Noise | Type `segmented`; Color, Level; footer (Poly, Show Scope) | As built: as designed |
| Sampler | waveform and load row (card chrome, above the body); Mode `segmented`; Start, End, Level; Pitch: Pitch, Root, Fine; Grains: Grain Size, Density, Spray; footer: Loop, Reverse | The grain controls dim unless Mode is Granular (as built: a dim, not a swap or a hidden section, so their CV jacks keep a knob to land on and the mode switch never resizes the card; they stay on the card in Sample mode, as before). As built: Root stays a knob (an int over 24 steps, no stepper, and no note-name text); no `waveform` view in the body (it is chrome already) |
| Wavetable | as today, as `tab` sections; Pan moves to Tune, Sync In to Phase | As built: Warp, Position and Warp Amt pinned in one untitled section above the strip (the Warp combo on its own row above the two knobs: a run holds one widget kind); tabs Tune (Octave, Coarse, Fine, Level, Pan), Unison (Stack; Unison, Detune, Width, Blend), Phase (Sync In; Phase, Rand Phase, Spread), Sub (Sub Oct, Sub Wave; Sub), File (Import, Interp); a footer with Poly and Show Scope; Table stays chrome beside the display; the card is 560x578 (565 before) |
| Delay | Tempo Sync; Time `knobLarge`, Feedback, Mix; footer: Ping-pong, Level | Sync swaps Time for a division. As built: Tempo Sync is a Time/Sync `segmented` switch on the boolean (its own off and on texts); the division stays a choice in Time's cell |
| Reverb | Room: Size, Damping, Pre-delay; Mix: Dry, Wet, Width; footer: Level | As built: Dry and Wet are both `faderV` (side by side, as the widget table says), Width a knob |
| Chorus, Phaser, Flanger | Motion: Rate, Depth, Mix; Tone: Delay or Centre Freq, Feedback; footer: Level | As built: Chorus and Flanger caption Centre Delay as "Delay (ms)" (the full name stays in the tooltip), because the full caption ellipsises at a knob's width |
| Distortion | Type `segmented`; Drive `knobLarge`, Mix; footer: Quality (oversampling), Level | As built: Quality is the Oversampling choice, drawn as a combo with its caption in the footer |
| Bitcrusher | Bits, Downsample, Mix; footer: Dither, Level | As built: Dither is continuous, so it is a footer fader, not a pill |
| Ring Modulator | Drive, Character, Mix; footer: Quality, Level | As built: Quality is the Oversampling choice, as for Distortion |
| Pitch Shifter | Mode `segmented`; Pitch `knobLarge`, Fine, Mix; Window, Feedback; footer: Level | Frequency mode swaps Pitch+Fine for Shift (Hz). As built: a swap group holds one cell and two controls swap for one, so Pitch and Shift swap in one cell and Fine dims in Frequency mode (two conditional sections would hide knobs whose CV jacks stay knob-bound, and resize the card on every flip); the card never changes size on the mode |
| Compressor | (new `gainReduction` view); Threshold, Makeup `faderV` side by side; Ratio, Attack, Release; footer: (new Knee) | As built: Makeup is captioned "Makeup (dB)" (the full name stays in the tooltip), because "Makeup Gain (dB)" ellipsises; the card is 365 px tall |
| Limiter | `gainReduction`; Input, Ceiling `faderV` side by side; Release | As built: Threshold is also on the card, before Release, so its CV jack has a knob to land on; the card is 309 px tall |
| Gate | `threshold` view; Attack, Hold, Release; footer: Range, Level | As built: Threshold is a knob, because the Gate publishes no live level for the `threshold` view yet |
| Sample & Hold | Source, Mode `segmented` side by side; Clock `segmented`; Rate, Slew; Level, Offset | External clock swaps Rate for the `threshold` view. As built: Source, Mode and Clock stack (spans are not drawn); the first knob cell swaps Rate for the Threshold knob (Threshold keeps its CV jack in the cell) and the `threshold` meter opens above the knobs (a section's `visibleWhen`, since a view cannot join a swap group), so only the Clock switch changes the card's height; the knobs are two columns (Rate or Threshold, Slew; Level, Offset) |
| Envelope Follower | Detection `segmented`; Attack, Release, Sensitivity; footer | — |
| Voice Mixer | Level `faderH` | As built: no footer (the card has no Poly or Scope) |
| Poly MIDI | Voice Steal `segmented`; footer: Velocity sets gate | As built: Voice Steal stays a combo, because "Round-Robin" is longer than a switch's 10 characters (the value strings are the patch contract) |
| MIDI Keyboard | bespoke keys plus an Octave `stepper` row (the parameter exists today, with no control) | As built: the row sits above the keys (card 560x160), the stepper's two buttons are Tab stops titled "Octave down" and "Octave up", the card listens to them for the right-click and MIDI Learn menu; it is card code (`ModuleComponentMidiKeyboardCard.cpp`), not a layout entry |
| Math | Clip `segmented`; footer | — |
| Comparator | unchanged (`threshold` view) | — |

Adding a parameter to a built-in module needs no change in `synth-platform/packages/contracts`: a
patch node's `params` is an open record there, and per-module ranges come from the client.

As built: `DefaultCardLayouts::builtIn()` calls one registration function per module family, each in
its own unit under `Source/UI/Graph/CardBody/DefaultLayouts/` (`DefaultCardLayoutsSources.cpp`:
Oscillator, Noise, Sampler, LFO, Wavetable; `DefaultCardLayoutsEnvelopes.cpp`: ADSR, Amp Env, Filter
Env, VCA, Envelope Follower, Sample & Hold, Math, Voice Mixer, Poly MIDI, MIDI Keyboard;
`DefaultCardLayoutsFilterDynamics.cpp`: Filter, Compressor, Limiter, Gate; `DefaultCardLayoutsEffects.cpp`:
Delay, Reverb, Chorus, Phaser, Flanger, Distortion, Bitcrusher, Ring Modulator, Pitch Shifter), all
empty so far (the envelopes and utilities family has its entries, below). An entry is `defaults.add(type, layout, revision, dimRules)`, written with the
`cardlayout::` builders in `DefaultCardLayoutsFamilies.h` (`param`, `showWhen`, `dimUnless`, `view`,
`section`, `tab`, `footer`). A family's goldens (`Tests/fixtures/card-body/<Type>.golden`) are recaptured with
`CARDBODY_WRITE_GOLDEN=<Type>,<Type>`, and its heights live in its own table in
[module-card.md](module-card.md#body-layout), so no two families edit the same file. The MIDI Keyboard
builds no data-driven body today, so an entry for it has no effect until its card does.

The envelopes and utilities family, as built. The ADSR draws its graph as the card body's `envelope`
view (the `CurveEditorComponent` unchanged, built by `CardBodyViews.cpp`); the card wires it to the
parameters (`ModuleComponentEnvelopeCard.cpp`) and its **Show Envelope Graph** toggle closes and reopens
it through `CardBody::setViewOpen` (a view item's `open` is the initial state, not persisted; a closed
view takes no height). The old card-owned MS|BPM buttons, knob-hiding and division combos are gone: the
Time/Tempo switch is the layout's `segmented` item on the `tempoSync` bool, and each stage's time and its
division are a swap group, so the one mechanism is the card body's conditions. A layout that leaves the
envelope view out draws no graph and no toggle.

The stages come in two modes. **Shared** (the default above) shows each stage once: its time while the
Time/Tempo switch is on Time, its note division while it is on Tempo. **Separate** replaces the stages group
with two titled groups, **Time** (Atk, Hold, Dec, Sus, Rel faders) and **Tempo** (the four divisions with the
same captions), both always visible: each time is dimmed while Tempo is on and each division while Time is on.
`withAdsrTimeTempo` (`AdsrTimeTempo.cpp`) converts either way in place, keeping each control's label, widget and
range, and going Shared, Separate, Shared returns the default group exactly. The on-card editor's
"Time and tempo" switch (below) writes it.

### Decisions (2026-10-01)

- **New controls: the full candidate list.** Oscillator pulse width and glide; LFO phase and fade-in;
  ADSR velocity; Delay tempo sync and ping-pong; Reverb pre-delay; a gain-reduction meter on
  Compressor and Limiter; Limiter ceiling; Compressor knee; Sampler end point, fine tune and reverse;
  the MIDI Keyboard's existing octave parameter on its card. Each starts at a value that leaves an
  existing patch sounding the same.
- **Filter: Key Track, no Env Amt.** An envelope amount knob would duplicate what a cable already
  does: dropping an ADSR cable on the Cutoff knob inserts an attenuverter whose signed amount is set
  on the knob itself (Alt-drag, or a drag on the ring; [modulation.md](../modules/modulation.md)),
  and the envelope's shape is the ADSR's. A built-in filter envelope would be a second ADSR with its
  own gate input and poly handling, so it is not added either. Key tracking cannot be built from
  cables, because pitch CV is in Hz and the cutoff input clamps to −1..1, so the Filter gains a
  **Pitch** input (Hz, per voice in poly) and a **Key Track** amount: cutoff moves by
  `keyTrack × log2(pitch / 261.63 Hz)` octaves, and does nothing while the input is unplugged.
  Discoverability of the filter envelope is a smart-connection question, not a knob.
- **ADSR stages are faders** by default (`faderV`), switchable per card.
- **Hidden controls go to the folded More row.**
- **A card that grows makes room, and gives it back.** Unfolding More, opening a view or a mode
  swap that changes a loose card's height pushes the neighbours it now covers out of the way
  (the [make-room rules](layout.md#making-room-when-something-grows) macros already use) and returns
  them when the card shrinks, unless they were moved meanwhile. Today a loose card's growth only
  pushes neighbours down and never brings them back; this lands before hiding controls does.
- **Sequencers stay as they are** in this round. Scales, step count and richer patterns are a
  separate piece of work that shares one scale engine with the piano roll's scale assist
  (`Source/Timeline/MusicalScale.h`).
- **Canvas pieces follow the design system.** New widgets (fader on a card, segmented switch,
  stepper, section header, More row, the layout editor) match the AgentSynth design system's values
  and are added to it by the work that builds them; card titles and section headers are never in
  capitals.

---

## Editing a layout

- **Quick path, on any control:** the right-click menu gains **Hide from card**, **Show as
  fader / Show as knob** (for a continuous parameter) and **Edit Layout...**. On a
  control in the More row, **Show on card** puts it back where the default had it. Built: each click edits the layout the
  card draws now (the automatic layout written out as explicit items when the node has none), and Show
  as fader picks `faderV` ([module-card.md](module-card.md#hide-or-show-from-the-right-click-menu)).
- **Edit Layout... (built): the card is the editor, and the only editor of a built-in card.** From a control's menu or the module menu (in the
  block after Bypass Module), the card gets an accent outline and an edit bar in its header (**Preset**,
  **Apply to**, **Cancel**, **Done**), and under the card a strip with a **+ Add control** button; on an ADSR, Amp Env or Filter Env card
  that strip also holds a "Time and tempo" switch with **Shared** and **Separate** segments, left of Add control
  (it shows only while the layout still has its stages) and rewrites the stages group at once,
  like any other edit of the session, so Cancel undoes it, and it is one undo step. Every control
  of a grid group gets a dashed accent outline (7 px corners, drawn just inside its cell so neighbouring
  outlines never touch) and a small grip in its bottom-right corner; section titles stay outside every
  outline; the footer row and tab groups are outlined panel-only (below). A swap group is one outline, on its shown
  member. The outlines are an overlay (`CardLayoutOnCardEditor`) that sits over the card as a sibling in
  the canvas, never a child of it: every write rebuilds the card, so the overlay holds the GraphEditor and
  the node id, and after each write re-finds the card and re-syncs its bounds and outlines.
  - *Moving.* Press anywhere on a control, or on its grip, and drag: the real widget and its caption move
    with the outline and stay under the pointer. Inside the group's content width and no higher than the
    group's top, the control may go anywhere; a drop below the last row grows the group by up to a row.
    An accent guide appears in the gap when the control's left, centre or right (or top, centre, bottom)
    lines up with another control's of the same group, and pulls the control onto the line within 4 px;
    hold Cmd to place it freely. On release every control the drop leaves closer than 8 px goes the
    shortest way out (left, right, up or down, inside the group and clear of the others; a pushed control
    then pushes what it lands on). A neighbour that was already flush with the control before the drag is
    left alone unless the drop overlaps it, so moving a control one pixel does not shove the flush rows
    around it. Esc during a drag puts the control back and writes nothing.
  - *Writing.* A drop writes at once: the group's section gets a free position (`at`) on every control it
    outlines (relative to the group's content origin, so a layout with positions survives a width or
    theme change), through `BuiltInCardLayoutSource` (the same live write the list uses), which rebuilds
    the card. The rebuilt card starts where everything was, then glides: pushed controls 160 ms, the
    dropped one settles in 140 ms, none under Reduce Motion. A drop that changes nothing writes nothing.
    The first drop turns a flowing group into a positioned one, with every control exactly where it was.
    The positioned group is 6 px taller than the flowing one (the free-placement padding), so the groups
    under it sit 6 px lower.
  - *Keys.* Tab moves between the edit bar and the controls (the outlines are one focus stop each, with
    the accent focus ring and a solid outline plus a faint wash on hover or focus). The arrow keys move
    the focused control 1 px, Shift+arrow 8 px; the control moves at once and the write waits until the
    keys stop for 250 ms, so a held arrow is one write. These are the rebindable **Layout Editor** actions
    `layoutEditorNudgeLeft`/`Right`/`Up`/`Down` and `...Big` ([shortcuts.md](../control/shortcuts.md#layout-editor)).
    Esc ends a drag, else cancels the session. Return on a control opens its options panel (below). Each move is announced ("Cutoff moved right 8").
  - *Ending.* **Done** keeps the layout; **Cancel** or Esc writes back the layout the card opened with
    (the node's raw stored value, or none) and records nothing. The editor also closes if its card's
    module is removed or the canvas goes. One session at a time: opening it on another card ends the
    running one as Done. The running session hangs on the GraphEditor's property set
    (`OnCardEditorOwner.cpp`, the way `ModuleCardLayoutBinding` hangs the store) and dies with it. The
    overlay fades in over 160 ms and out over 110 ms (a plain 80 ms either way under Reduce Motion).
  - *The per-control panel (built).* Right-click, double-click or Return on a control opens one small panel
    with every option at once, in a `juce::CallOutBox` beside the control with its arrow pointing at it (so
    it never covers the control; it eases in and out like every popup, [animation.md](animation.md#popup-windows)).
    **Show as** is a row of joined buttons (`CardSegmentedSwitch`) naming only the widgets that suit the
    parameter (the list's `widgetChoicesFor`), hidden when there is one choice. **Label** renames the
    caption; empty or the parameter's own name clears the override (the list's rule, `labelOverrideFor`),
    and the full name stays as the caption's tooltip. **Range** (Minimum and Maximum, in the parameter's
    own units) shows only for a float parameter drawn as a knob or fader: blank both clears it, one blank
    leaves that end at the parameter's own, each end is clamped to the parameter's range, and anything not a
    number or with a minimum not below the maximum is refused (the fields revert and a hint line says why).
    **Hide from card** adds the control to `hidden` (it goes to the More row) and closes the panel. Every
    change is written at once through the session's source, like a drop, so the card rebuilds and the panel
    stays open, re-anchored to the control's new outline, until Esc, a click outside, or Hide. Esc closes
    the panel first; a second Esc cancels the session. The panel is owned by the editor, which closes it
    when the session ends or the control leaves the card; each field's change is its own undo step.
  - *Apply to (built).* The bar's **Apply to** opens a menu: **This module** and **All <Type> modules**, the
    one in force ticked, and a disabled note under them saying how many cards a write changes now ("Changes
    1 card", "Changes 2 cards": the GraphEditor's cards of that module type). Choosing **All** writes the
    layout being edited as the type's default and clears this module's own override at once
    (`BuiltInCardLayoutSource::apply(layout, true)`), and every later write of the session (a drop, a
    nudge, a panel field, a preset, an added control) goes the same way, so the other cards of the type
    re-lay out live. Choosing **This module** again writes the layout to this module's own override and
    leaves the default as written. Cancel puts the type's default back as it was at open, as well as this
    module's own layout; Done keeps it, and undo gives this module its own layout back while the per-type
    file stays (it is a setting, see Undo below).
  - *Preset (built).* The bar's **Preset** opens a menu: **Save as...** (a small window asks for a name,
    the same one the hosted list uses; the card's layout as it is now is saved in the type's
    `ModuleCardLayouts/<Type>/` folder), **Reset to default** (removes the scope's layout, so the card goes
    back to the type's default or the code default; with **All** chosen the stored default goes too), then
    the saved presets, a tick on the one whose layout equals the card's now. Choosing a preset writes it to
    the scope in force and the overlay re-syncs to the rebuilt card. A name the store refuses (the reserved
    "default") is announced and saves nothing. Deleting a preset is not offered here.
  - *+ Add control (built).* A **+ Add control** button (full width, less the switch on an ADSR card) sits in a 30 px strip under the card: the
    overlay reaches that far past the card's bottom edge, so no neighbouring card moves and the card keeps
    its size. It is off while every control is on the card. It opens a call-out panel (the same kind as the
    per-control panel) with a search field, a count line ("3 hidden controls"; "2 of 3 hidden controls"
    while searching; "No control matches"), one row per control the card does not show (the More row's: hidden
    ones, which keep their settings, and parameters the layout never placed; `CardBodyPlan::more`), and the
    hint "Click to add, or drag onto the card". A row shows the control's name with the letters the search
    matched in the accent colour and semi-bold (matching is the app's one `searchMatches`, best match
    first); Up and Down choose a row, Return adds it, typing searches, Esc closes the panel.
    - *Click or Return.* The control moves to the end of the last grid group that is not the footer (it keeps
      its widget, label and range, leaves `hidden` and its old place). If that group has free positions
      the control gets one: the highest, then leftmost, place inside the group that overlaps nothing and
      keeps the 8 px gap (`findFreeSpot`, sized by the cell size the group's own layout gives that kind,
      `cardBodyCellSize`); a flowing group just takes it at the end of its flow. The write is the session's
      source's, so the card rebuilds and the overlay re-syncs; the panel stays open for the next control
      and closes when nothing is left.
    - *Drag out.* Pressing a row and dragging past 3 px lifts a ghost of the control's cell onto the card
      (the panel fades back so the card shows through); on release over the card the control lands there,
      centred on the pointer, in the group under it, and the group is written as positioned (the
      neighbours the drop crowds are pushed aside and glide, as a drop of a control on the card does).
      Released outside the card or over the panel, nothing is added.
    - *Motion and speech.* The added control fades in at its spot over 160 ms (80 ms under Reduce Motion);
      "Drive added" is announced. The panel eases in and out like every call-out.
    - *Keys.* Tab order in the bar is Preset, Apply to, Cancel, Done; then the outlines, then (on an ADSR card) the Time and tempo switch and the Add control button. Each has the accent focus ring, an accessible name ("Preset", "Apply to", "Add control") and a
      tooltip ("Save, load or reset this card's layout", "Choose which cards this layout changes", "Add a
      hidden control"). The panel's rows are named buttons but not Tab stops: the search field keeps focus and
      Up and Down announce the chosen row ("Drive, 1 of 2").
  - *Footer and tab controls (built, panel only).* The footer's controls and the selected tab's controls get
    an outline too, with the same dashed look and focus ring, but no grip: they cannot be dragged or nudged
    (a press does nothing, the arrow keys are left alone), so the tooltip is "Right-click for options" and
    the accessible title is "<name>, layout: Return for options". Right-click, double-click or Return opens
    the same per-control panel (Show as, Label, Range, Hide from card), and Hide, Label and the rest write
    through the same layout edit; a control the footer draws by itself (the Poly toggle) is listed in the
    footer section the first time one of its fields is changed. A tab section stays a tab section. Switching
    the tab strip while editing re-reads the outlines (`CardBody::onTabSelected`): the old tab's outlines go and the
    new tab's appear. The tab strip itself, the group headers and the chrome pills (Show Scope and the like)
    get no outline, and a tab's controls cannot be moved, nor tabs added, renamed, reordered or removed.
  - *Not built yet:* adding or removing groups, moving a control to another group by dragging, and renaming
    or reordering tabs.
- **The list editor (built, hosted plugins only).** A hosted plugin's **Edit Layout...** opens
  `CardLayoutEditorComponent` in a
  `juce::CallOutBox` beside the card; built-in cards no longer have a list, the on-card editor replaced it. It lists
  every control the card can show, grouped by section under a header row per group: a tick (shown, or
  hidden in the More row; a hidden row keeps its place), a grab handle to drag it (the shared reorder
  drag; a row dropped under a group's header joins that group), its name (click to rename; an empty
  name, or the control's own, clears the override), and a widget choice listing only the widgets the
  card would really draw it as (`cardBodyKindFor`, the automatic one first; a toggle, or a choice too
  long to segment, gets none). A header's title is renamed the same way; an untitled group is listed by a name from its id (`cardSectionDisplayName`: "Footer", "Controls" for `main`, "Group 2" for `group-2`, "Trigger meter" for `trigger-meter`), never "Untitled group", and the card still draws no header row for it. Above the list: **Apply to**
  (this module / all <Type> modules), **Presets** (save as, load, delete, in the type's
  `ModuleCardLayouts/<Type>/` folder), **Reset to default**, a search (it hides rows, never group
  headers, so a drop can still land in any group) and **+ Add group** (a titled "New group" at the end).
  Every edit writes at once and the card re-lays out live; there is no OK button. A `tab` section is
  listed as a group like any other, its header titled "Tab: <title>" (built): its controls can be
  hidden, moved within it or onto a neighbouring tab, and the tab renamed by renaming its header, and
  every edit keeps the section a tab. Not built: adding or removing a tab, reordering tabs, turning a
  group into a tab or back, and choosing which tab a new card opens on (+ Add group always adds a grid
  group at the end).
- **Two sources.** What the list (hosted) and the on-card editor (built-in) edit comes from a `CardLayoutEditorSource`:
  `BuiltInCardLayoutSource` (the card's own parameters, the node's `cardLayout`, the type's default
  in the bound store) and `HostedCardLayoutSource` (a hosted instance's parameters, its extra-state
  layout, written as the flat v1 slot list). The hosted plugin's **Edit Layout...** is this list
  (`PluginKnobPickerComponent` adds touch-to-add); its module-menu item is named **Edit Layout...**
  too. The hosted source has no groups and no widget choice, and an unticked row leaves its layout
  (it lists after the ticked ones), as before. The on-card editor writes through the built-in source too
  (it adds `restoreOpeningLayout()` for Cancel).
- **Undo:** one step per quick-path click, and one per change made in the on-card editor of a built-in card (a
  drop, a settled nudge, a panel edit, an add or hide, a preset, a reset, an Apply to, a Time and tempo switch):
  each write records the graph before against the graph after (`AppUndoManager::recordGraphChangeSince`, the
  "before" taken at the write), so the layout, its label and widget edits and the neighbours a taller card
  pushed aside undo together, and Cmd+Z while the editor is open steps back one change, never the earlier
  work on the canvas first. Cmd+Z and Cmd+Shift+Z are the app's: the overlay leaves them to bubble (a nudge
  still waiting is written first, as its own step) and re-syncs to the card the restore rebuilds, writing
  nothing; undoing past the module's own creation closes the editor with the card. **Apply to all** writes the
  per-type file and clears this module's override inside its step; undo gives the override back, but the
  per-type file is a setting, not part of the project, and stays. A write that changes nothing records
  nothing. Cancel puts the opening layout back as one more step (when it differs), so Cmd+Z after Cancel
  brings the cancelled edits back.
  The hosted editor keeps one step per edit (`recordNodeExtraStateChange`), as the picker did.
- **Later:** the list's built-in-only parts (groups, widget choice, tab rows, unticked rows staying in place) have no
  entry point now and can go once nothing needs them.

---

## Trust boundary and the AI patch format

`graphToJSON` emits a node's `"cardLayout"` only when it is set, so every other node's JSON stays
byte-identical. `applyJSONToGraph` applies it on the **trusted path only** (undo snapshots, presets,
projects, the in-app clipboard); untrusted apply ignores it, and `getPatchSchema()` never advertises
it, so a provider is never invited to emit one. A `.agsnip` written to disk drops it, like extra
state. Layout is presentation only: it never changes a parameter's value, id, range or binding, and
the patch `params` map always carries every parameter, hidden or not.

**Why trusted-only for now, though a layout is display-only like `displayName`.** A hostile layout
could hide every control of an authorable module. Accepting it untrusted needs a validator (known
`paramId`s only, bounded sizes, whitelisted keys), which the custom-module work needs anyway; the
model stays validatable so that path can be opened later without a format change.

---

## Custom module (future)

A user-built "synth in one card" is **a macro with a face** (decided 2026-10-01): any macro may be
given a face, and that is all a custom module is; there is no second kind of node or library item
to tell apart. The user groups ordinary modules
(Oscillator, ADSR, Filter, LFO, a Macros knob bank for their own knobs) into a macro, and the
collapsed macro card renders a `CardLayout` whose items point at its members' parameters.

- **Why not a new container module.** A node's parameter set and channel count are fixed for its
  lifetime (`Source/Modules/CLAUDE.md`), members would leave uuid space and orphan their lanes and
  bindings, and it would need a second sound engine. A macro reuses every module, poly voices come
  from members' own Poly flags, and undo is already joint.
- **What v2 reserves now:** `ParamItem.node` (a member's uuid; null = this module), a layout that can
  live on a macro (a `layout` key in the macro's own record, next to its ports), and a model that can
  be validated on the untrusted path. Paste and snippet insert remap `node` through the same id map
  macro ports use.
- **What it changes when it starts:** two of the macro design's deliberate limits
  ([macros.md](../macros/macros.md#deliberate-limits)), "a macro is not a saveable library item" and
  "no macro-level parameter exposure", are lifted by that work, not this one.

---

## Accessibility

Every widget kind follows the app's accessibility rules: a Tab stop in card
order (section by section, then the footer, then More), the accent focus ring via
`Source/UI/Layout/FocusRegion.h`, `setTitle` with the parameter's display label and its value, and a
tooltip naming the full parameter name when the label was shortened or renamed.

- `segmented`: one Tab stop; Left/Right move the selection; role radio group; each segment titled.
- `faderV` / `faderH`: arrow keys step, Shift+arrow fine, Home/End to range ends; value text as the
  accessible value.
- `stepper`: − and + are separate buttons with titles ("Octave down", "Octave up").
- More row: a button titled "More controls (N)", Enter/Space unfolds it.
- Tab strip (built): one Tab stop; Left/Right switch tabs and stop at the ends, Home/End jump to the
  first and last; the accent focus ring; titled "Control tabs" with a tooltip naming those keys; a
  group of radio buttons titled with the tab names, each with a tooltip.
- On-card editor (built): the edit bar's Cancel and Done are named by their text with tooltips; each
  control's outline is one focus stop (the shared accent focus ring, a faint wash and a solid outline on
  hover or focus), a button titled "<caption>, layout: drag to move, Return for options" with the tooltip
  "Drag to move (arrow keys nudge, Shift for 8px). Right-click for options"; the arrow keys nudge, Esc
  cancels, and each move is announced. The per-control panel is titled "<caption> options"; its fields are
  titled "Show as", "Label", "Minimum", "Maximum" and "Hide from card", each with a tooltip, in that Tab order
  (the text fields and buttons carry the shared accent focus ring); Esc closes it.
- List editor (built): each row (a control or a group header) is one focus stop with the accent focus ring,
  a title and a tooltip naming its keys as bound now. Up/Down move between rows (fixed list keys);
  Space shows or hides, Cmd+Up/Down moves (across a group's edge into the next group), Enter renames:
  the rebindable **Layout Editor** actions (`layoutEditorToggleShown`, `layoutEditorMoveUp`,
  `layoutEditorMoveDown`, `layoutEditorRename`, [shortcuts.md](../control/shortcuts.md#layout-editor)).
  The widget choice is a Tab stop of its own; the tick and name take no focus (the row's keys reach
  them). Escape is left to the CallOutBox, which closes the panel. Every button and combo has a title
  and tooltip; the search field has no hidden Tab stops.
- The app's accessibility coverage check passes with no new exemption.

---

## Tests

- `Tests/Modules/CardLayoutTests.cpp`: v2 round trip; v1 read → v2 with the absent-means-hidden rule
  applied once; unknown keys ignored; newer version refused; conditions match value strings;
  `hidden` plus unplaced parameters both land in More; a hosted layout without v2 features still
  writes v1.
- `Tests/UI/Graph/CardBody/CardBodyGoldenTests.cpp` (built): every card's size, every child's kind,
  bounds, visibility, title, tooltip and focus order, and every jack centre, against one file per type,
  `Tests/fixtures/card-body/<Type>.golden` (the type name with every run of other characters made one
  "-"), captured from the card before `CardBody`. `CARDBODY_WRITE_GOLDEN=1` rewrites them all, a
  comma-separated list of types only those; every type has its file and no file is stray.
- `Tests/UI/Graph/CardBody/CardBodyConditionsTests.cpp` (built): a dim greys the control and its caption
  and keeps its bounds; an LFO Rate (Hz) / Sync Rate swap on the Sync switch keeps the card's and a
  neighbour's bounds; swapped-out items are never in More; a `visibleWhen` section grows the card,
  pushes a neighbour and gives it back; a code dim rule follows its predicate and comes only with the
  code default; choice and bool conditions match value strings and "true"/"false".
- `Tests/UI/Graph/CardBody/CardBodyFooterTests.cpp` (built): the footer row places a Poly pill, a
  horizontal Level fader and the Show Scope pill in one row under the body and above More; a card
  without a footer keeps its chrome rows; the Filter footer with Show Response and Spectrum; and the
  size estimate equals the real card for layouts with a swap, conditional sections, More and a footer.
- `Tests/UI/Graph/CardBody/CardBodyLayoutTests.cpp` (built: order, measure == apply, estimate, More
  row, no height change on a knob drag): automatic layout reproduces today's order and
  heights for every type without a code default; each code default builds with no missing
  `paramId`; measure and apply agree; a `show` swap and a `dim` rule change no height;
  `estimateModuleSize` matches every library type.
- `Tests/UI/Graph/CardBody/DefaultLayouts/WavetableDefaultLayoutTests.cpp` (built): the five tabs in
  order as one strip, Position and Warp pinned on every tab, Pan on Tune and Sync In on Phase, a tab
  switch that moves nothing outside the tab's own controls (card, jacks, pinned controls, chrome,
  footer), only the pinned knobs taking their jacks, the estimate equal to the real card with measure ==
  apply on every tab, the strip's keys and accessibility, the tab kept per card and never saved, and the
  layout editor listing the tabs and hiding, moving and renaming within them. The family files beside it
  pin each family's designed layout the same way.
- `Tests/UI/Graph/CardBody/CardBodyBindingTests.cpp` (built for knob, toggle and choice) and
  `CardBodyLoadTests.cpp` (project load with stored layouts and LFO/ADSR macro members): every widget kind drives its parameter,
  registers for MIDI Learn and accepts the modulation-amount gesture; a hidden parameter keeps its
  value, stays in `graphToJSON`'s `params`, and takes a cable dropped on the More row (real
  synthesized drag).
- `Tests/UI/Graph/CardWidgets/CardFaderTests.cpp` (built): drag, Shift-fine, reset, keyboard steps,
  right click, the design-system sizes (software-image paint), learnable and a modulation target on a
  card, a real cable drop, Alt-drag and bar-drag adjusting the amount, the modulation bar's pixels.
  `CardSegmentedSwitchTests.cpp` and `CardStepperTests.cpp` beside it (built): keys, clicks,
  accessibility roles and titles, one undo step on a card, a right click opening the control menu.
- `Tests/UI/Graph/CardBody/CardBodyWidgetKindTests.cpp` (built): the plan honours or falls back per
  widget, each kind's size, measure == apply with a never-built plan, no value change resizes the card,
  and the automatic layout written out builds the same card on every library type.
- `Tests/UI/Graph/CardBody/CardLayoutQuickEditTests.cpp` (built): the real right-click path (a
  synthesized right click on a real card control, the item picked from the menu the card built) for
  Hide from card, Show on card, Show as fader / Show as knob with make-room, and one undo each.
- `Tests/AI/CardLayoutTrustTests.cpp`: `cardLayout` round-trips on the trusted path and through a
  node-preserving undo (`applySnapshotPreservingNodes`); untrusted apply ignores it; `.agsnip` on disk drops it; the in-app
  clipboard keeps it; not in `getPatchSchema()`.
- `Tests/UI/Graph/CardLayoutEditor/` (built): `CardLayoutEditorBuiltInTests.cpp` opens the editor
  through the real right-click path (a control's menu and the module menu) and covers tick, rename,
  widget choice (only suitable kinds), + Add group with a header row on the card and measure == apply,
  a real drag across a group header, search, one undo step per change (and none for a session that
  changes nothing), Apply to all clearing the override in its step with another Filter re-laid out,
  a per-type default written elsewhere rebuilding only that type's cards, presets and reset;
  `CardLayoutEditorKeyboardTests.cpp` a keyboard-only session, the rebindable keys and the
  accessibility audit with no gaps; `CardLayoutEditorModelTests.cpp` the working model in both modes.
  The hosted source keeps `Tests/UI/Graph/PluginKnobPicker/` (search, tick, reorder, label, scope,
  presets, reset, touch-to-add, the entry points).
- `Tests/UI/Graph/CardLayoutEditor/OnCard/` (built): `OnCardLayoutMathTests.cpp` snapping (4 px, centres,
  Cmd) and pushing (shortest way, clamped, cascading, a flush neighbour left alone) as pure functions;
  `OnCardEditorTests.cpp` opens the editor through the real module and control menus over a Filter with
  one titled, tooltipped outline per control, free placement leaving every control where it was, the
  overlay following the rebuilt card, closing and a vanished module, and the accessibility audit with no
  gaps; `OnCardEditorDragTests.cpp` real mouse events: a 60 px drop writing positions, the widget following
  the pointer, guides and Cmd, a drop onto a neighbour, Esc mid-drag; `OnCardEditorKeyboardTests.cpp` the
  nudge (1 px, Shift 8 px, one write), Esc, Return and the actions; `OnCardEditorSessionTests.cpp` Done keeping per-change steps, Cancel restoring the opening layout as one more
  step; `OnCardEditorUndoTests.cpp` Cmd+Z/Cmd+Shift+Z with the editor open (one change at a time, repeated, past the
  module add). Motion is off in them
  (`setReducedMotionForTest`); the glide and fades need a window and are not exercised headless.
- E2E (built, `CardLayoutEditorE2ETests.cpp`): add a Filter, hide Drive, rename Cutoff to Freq, make
  Level a fader, save (`graphToJSON`), reload into a fresh canvas (`applyJSONToGraph`, trusted), and
  the card matches; Apply to all and a Filter added later shows it.

---

## Docs Updates

When each part lands, in the same PR: this doc's **Status** line and the section it built;
[module-card.md](module-card.md) (body layout, measured heights, footer, More row);
[plugin-card-layout.md](../control/plugin-card-layout.md) (shared editor, v1/v2 writing rule);
[patch-format.md](../ai/patch-format.md) (the `cardLayout` node field);
[visualizers.md](visualizers.md) (new views: LFO shape, gain reduction);
[modules.md](../modules/modules.md) and [fx-modules.md](../modules/fx-modules.md) for any parameter
added; `Source/UI/CLAUDE.md` for the `CardBody` invariants (bindings unchanged, no height change on a
value); [macros.md](../macros/macros.md) when the custom module lifts its limits; and the design
system's component list for every new widget.

---

## Related

- [module-card.md](module-card.md) — card geometry today · [layout.md](layout.md) — make-room
- [plugin-card-layout.md](../control/plugin-card-layout.md) — `CardLayout` v1 and the picker
- [macros.md](../macros/macros.md) — the container a custom module builds on
- [patch-format.md](../ai/patch-format.md) — the trusted/untrusted apply paths
