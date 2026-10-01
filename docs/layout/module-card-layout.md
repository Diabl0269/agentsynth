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
small toggle pill, and the code-default registry split into one unit per module family
([What exists](#what-exists)). Everything else (spans, the default layouts themselves) is designed and
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
  parameter (`CardBodyPlan.cpp`'s `isEditedElsewhere`), the LFO custom-wave editor, the Sampler/Wavetable chrome, the Wavetable tab strip
  (a name-keyed page table in `WavetableTabStrip.cpp`, the only grouping that exists), and fully
  bespoke bodies for Sequencer, Poly Sequencer, MIDI Keyboard, Macros, Attenuverter, Parametric EQ
  and External MIDI.
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
| One registration unit per module family, and the `cardlayout::` builders | `Source/UI/Graph/CardBody/DefaultLayouts/` |
| The size estimate for a card before it exists, measured from the plan | `CardBodyMeasure.*` (`GraphEditor::estimateModuleSize`) |
| The widgets: `CardFader`, `CardSegmentedSwitch`, `CardStepper`, and `CardControlGestures` (the gestures a knob and a fader share) | `Source/UI/Graph/CardWidgets/` |
| The right-click quick path (the explicit layout, the edits, the undoable write and rebuild, the menu items) | `CardLayoutQuickEdit.*`; the stale-card rebuild in `GraphEditorCanvas.cpp` (`CardBody::isStaleFor`) |
| The app's store, bound to the GraphEditor's cards; a default written or cleared rebuilds that type's cards | `ModuleCardLayoutBinding.*` (owned by `MainComponent`) |
| The layout editor: the panel, its rows, its working model, and the two sources (built-in module, hosted plugin) | `Source/UI/Graph/CardLayoutEditor/`; `ModuleComponentLayoutEditor.cpp` opens it |
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
Sequencer, Macros, Parametric EQ, Attenuverter, Wavetable, macro ports) always build from the
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
ViewItem
  view     : scope | response | spectrum | envelope | lfoShape | lfoCurve | waveform
           | wavetable | eqCurve | threshold | gainReduction
  open     : bool               // collapsible views: open by default or not
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
parameters with the Auto, Knob, Toggle or Choice widget, and no `hidden`, view, condition, span, `node`
or `basedOn`, writes v1. Choice
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
- **Bespoke cards.** Sequencer, Poly Sequencer, MIDI Keyboard, Macros, Attenuverter, Parametric EQ
  and External MIDI keep their own bodies; their editable surface is at most hide/reorder of a plain
  knob row they expose. The Wavetable tab strip becomes `presentation: tab` sections, so its page
  table leaves `WavetableTabStrip.cpp`. ADSR, LFO and Sampler become ordinary layouts with views.

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
| LFO | Shape; Sync switch; Rate `knobLarge` on its own row; Phase, Fade in, Level, Glide (one row of four); footer: Bipolar, Retrig | Rate swaps Hz and the division in one cell on Sync; Glide dims unless S&H. As built: Shape stays a combo (six values do not fit one switch) and Sync a toggle (`mode` is a bool, and `segmented` suits a choice); no `lfoShape` view (no component to wrap); the custom-wave editor stays card chrome and opens under the body on Custom, so Draw needs no `lfoCurve` view in the body |
| Noise | Type `segmented`; Color, Level; footer (Poly, Show Scope) | As built: as designed |
| Sampler | waveform and load row (card chrome, above the body); Mode `segmented`; Start, End, Level; Pitch: Pitch, Root, Fine; Grains: Grain Size, Density, Spray; footer: Loop, Reverse | The grain controls dim unless Mode is Granular (as built: a dim, not a swap or a hidden section, so their CV jacks keep a knob to land on and the mode switch never resizes the card; they stay on the card in Sample mode, as before). As built: Root stays a knob (an int over 24 steps, no stepper, and no note-name text); no `waveform` view in the body (it is chrome already) |
| Wavetable | as today, as `tab` sections; Pan moves to Tune, Sync In to Phase | — |
| Delay | (new ms/Sync `segmented`); Time `knobLarge`, Feedback, Mix; footer: (new Ping-pong), Level | Sync swaps Time for a division |
| Reverb | Room: Size, Damping, (new Pre-delay); Mix: Dry, Wet `faderV`, Width; footer: Level | — |
| Chorus, Phaser, Flanger | Motion: Rate, Depth, Mix; Tone: Delay or Centre Freq, Feedback; footer: Level | — |
| Distortion | Type `segmented`; Drive `knobLarge`, Mix; footer: Quality (oversampling), Level | — |
| Bitcrusher | Bits, Downsample, Mix; footer: Dither, Level | — |
| Ring Modulator | Drive, Character, Mix; footer: Quality, Level | — |
| Pitch Shifter | Mode `segmented`; Pitch `knobLarge`, Fine, Mix; Window, Feedback; footer: Level | Frequency mode swaps Pitch+Fine for Shift (Hz) |
| Compressor | (new `gainReduction` view), Threshold `faderV`; Ratio, Makeup, Attack, Release; footer: (new Knee) | — |
| Limiter | Input `faderV`, `gainReduction`, Ceiling `faderV`, Release | As built: Threshold is also on the card, before Release, so its CV jack has a knob to land on |
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
`section`, `footer`). A family's goldens (`Tests/fixtures/card-body/<Type>.golden`) are recaptured with
`CARDBODY_WRITE_GOLDEN=<Type>,<Type>`, and its heights live in its own table in
[module-card.md](module-card.md#body-layout), so no two families edit the same file. The MIDI Keyboard
and the Wavetable build no data-driven body today, so an entry for them has no effect until their cards
do.

The envelopes and utilities family, as built. The ADSR draws its graph as the card body's `envelope`
view (the `CurveEditorComponent` unchanged, built by `CardBodyViews.cpp`); the card wires it to the
parameters (`ModuleComponentEnvelopeCard.cpp`) and its **Show Envelope Graph** toggle closes and reopens
it through `CardBody::setViewOpen` (a view item's `open` is the initial state, not persisted; a closed
view takes no height). The old card-owned MS|BPM buttons, knob-hiding and division combos are gone: the
Time/Tempo switch is the layout's `segmented` item on the `tempoSync` bool, and each stage's time and its
division are a swap group, so the one mechanism is the card body's conditions. A layout that leaves the
envelope view out draws no graph and no toggle.

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
  fader / Show as knob** (for a continuous parameter) and **Edit Layout...**. On a control in the More
  row, **Show on card** puts it back where the default had it. Built: each click edits the layout the
  card draws now (the automatic layout written out as explicit items when the node has none), and Show
  as fader picks `faderV` ([module-card.md](module-card.md#hide-or-show-from-the-right-click-menu)).
- **The editor (built):** **Edit Layout...** (a control's menu, or the module menu in the block after
  Bypass Module) opens `CardLayoutEditorComponent` in a `juce::CallOutBox` beside the card. It lists
  every control the card can show, grouped by section under a header row per group: a tick (shown, or
  hidden in the More row; a hidden row keeps its place), a grab handle to drag it (the shared reorder
  drag; a row dropped under a group's header joins that group), its name (click to rename; an empty
  name, or the control's own, clears the override), and a widget choice listing only the widgets the
  card would really draw it as (`cardBodyKindFor`, the automatic one first; a toggle, or a choice too
  long to segment, gets none). A header's title is renamed the same way. Above the list: **Apply to**
  (this module / all <Type> modules), **Presets** (save as, load, delete, in the type's
  `ModuleCardLayouts/<Type>/` folder), **Reset to default**, a search (it hides rows, never group
  headers, so a drop can still land in any group) and **+ Add group** (a titled "New group" at the end).
  Every edit writes at once and the card re-lays out live; there is no OK button.
- **Two sources, one editor.** What the list edits comes from a `CardLayoutEditorSource`:
  `BuiltInCardLayoutSource` (the card's own parameters, the node's `cardLayout`, the type's default
  in the bound store) and `HostedCardLayoutSource` (a hosted instance's parameters, its extra-state
  layout, written as the flat v1 slot list). The hosted plugin's **Edit Layout...** is this editor
  (`PluginKnobPickerComponent` adds touch-to-add); its module-menu item is named **Edit Layout...**
  too. The hosted source has no groups and no widget choice, and an unticked row leaves its layout
  (it lists after the ticked ones), as before.
- **Undo:** one step per quick-path click, and one per editor session on a built-in card: the
  session takes a graph snapshot when it opens and records the difference when the panel closes
  (`AppUndoManager::recordGraphChangeSince`), so the layout, its label and widget edits and the
  neighbours a taller card pushed aside undo together. **Apply to all** writes the per-type file and
  clears this module's override inside that same step; undo gives the override back, but the per-type
  file is a setting, not part of the project, and stays. A session that changes nothing records
  nothing. The hosted editor keeps one step per edit (`recordNodeExtraStateChange`), as the picker did.
- **Direct in-card editing** (dragging controls on the card itself) is a later addition on top of the
  same model; the editor comes first because it is fully keyboard-reachable.

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
- Editor (built): each row (a control or a group header) is one focus stop with the accent focus ring,
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
  a real drag across a group header, search, one undo step per session (and none for a session that
  changes nothing), Apply to all clearing the override in that step with another Filter re-laid out,
  a per-type default written elsewhere rebuilding only that type's cards, presets and reset;
  `CardLayoutEditorKeyboardTests.cpp` a keyboard-only session, the rebindable keys and the
  accessibility audit with no gaps; `CardLayoutEditorModelTests.cpp` the working model in both modes.
  The hosted source keeps `Tests/UI/Graph/PluginKnobPicker/` (search, tick, reorder, label, scope,
  presets, reset, touch-to-add, the entry points).
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
