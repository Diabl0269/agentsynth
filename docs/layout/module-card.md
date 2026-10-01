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
EQ, Attenuverter, and the Wavetable's tab pages) build their widgets through the same card body
but place them themselves; MIDI Keyboard, External MIDI and a hosted plugin build their own.

Around the body, `layoutDefaultContent(bool apply)` adds the card's chrome (Sampler and Wavetable
rows above it; the envelope graph, LFO wave editor, Show Response / Spectrum / Scope rows below it;
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
its card-body plan (`CardBodyMeasure.cpp`: the same plan and layout walk, the knob-bound jack rule
from the plan, and the chrome rows above); only the bespoke cards read a small table.
`ModuleComponentTest.EstimatedModuleSizesMatchTheRealComponents` builds every library type and fails
if either path drifts from the real card, and `CardBodyGolden` pins every child's bounds of every
card against `Tests/fixtures/card-body/card-geometry.golden`. Fresh cards, single width (280):

| Module | Height (px) | Module | Height (px) |
|---|---|---|---|
| Oscillator | 433 | Sample & Hold | 451 |
| Filter | 403 | Comparator | 185 |
| LFO | 361 | Sampler | 545 |
| VCA | 233 | Chorus / Phaser / Flanger | 237 |
| ADSR (Amp Env, Filter Env) | 389 | Bitcrusher | 263 |
| Poly MIDI | 185 | Pitch Shifter | 387 |
| Distortion | 283 | Compressor / Gate | 257 |
| Ring Modulator | 331 | Limiter | 161 |
| Delay | 337 | Voice Mixer | 301 |
| Noise | 261 | Math | 239 |
| Envelope Follower | 235 | Channel Strip | 409 |
| Master | 221 | Rec Tap / Track Audio | 131 |
| Track In | 100 | Reverb | 313 |

From the table (bespoke): Sequencer and Poly Sequencer 560x406, MIDI Keyboard 560x150, Macros (tracks
its `Knobs` count, 458 at the default), Attenuverter (square, `kNarrowWidth`), Wavetable 560x565,
Parametric EQ 560x592, External MIDI 146, Hosted Plugin 135, and Audio Input / Audio Output on a
100 px floor.

## The More row

A layout lists what it hides; every parameter it hides or does not place renders in a folded **More**
row at the very bottom of the card, under the Scope row. The row exists only when something is hidden,
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
  also draws its focus ring round the cap. It carries every knob gesture through
  `CardControlGestures` (the modulation-amount drag, the cable pickup on its landing dot, the hover
  that highlights the landing cable, the keyboard steps) and is in the card's slider list, so
  Automate, MIDI Learn, value reflection and the modulation-target lookup treat it as a knob. Its own
  drag is the mixer fader's: Shift drags at an eighth of the rate (re-anchored where the drag is, so
  the value never jumps), Cmd-click and double-click reset to the parameter's default, each one change
  gesture. Modulation shows as a 3 px bar beside the slot (right of a vertical cap, under a horizontal
  one) from the base value to base + CV; a cable lands on a dot just past the bar's zero end.
- **Segmented switch.** Joined `TextButton` segments with the ADSR MS|BPM switch's look; the switch
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
fader** / **Show as knob** for a float or int on the card, and a disabled **Edit Layout...** (the
layout editor is not built yet; the module menu carries the same disabled item). The bespoke cards
offer none of these. Each click is one write of the node's `cardLayout` override, one undo step,
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
401 px in the table above, unchanged. See `ModuleComponentLfoCard.cpp`'s
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
| ADSR Show Envelope Graph | the toggle's `onClick` |

Not wired because the card height does not change: the Spectrum toggle (a backdrop only), the ADSR BPM|MS switch (swaps
knobs for combos in the same slots) and the Wavetable page tabs (the card is sized once at construction). A card that
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
that came to 560x869 — technically correct and genuinely unusable, a wall of identical knobs with no
hierarchy. Three mechanisms bring it to 560x554 and give it a reading order.

**Two-column jack gutter, both columns on the LEFT.** `getInputPortColumns()` returns 2 for a card
with more than 10 visible input jacks at double width; `getPortCenter()` then lays the inputs out
**column-major** — jack 0 top-left, running down then over — at `kPortColumnStride` (100 px) apart.
Sixteen jacks in one column set a roughly 390 px floor on the card height before a single control is
placed.

**Inputs stay on the left, outputs on the right — that convention is not negotiable for a saving in
height.** It is what makes signal flow read left to right across a patch. Spilling the overflow down
the right edge was tried and reverted: it reads as an output, and inputs and outputs are drawn
identically, so the side is the only cue there is. The real objection to an interior column — the
module covers the lower half of a cable being dragged towards it — is answered by not aiming at the
gutter at all; release the cable **on the destination knob** instead, see
[modulation.md](../modules/modulation.md#drag-to-knob-modulation).

Everything that touches jack geometry — wire drawing, hit-testing (`getPortForPoint`), painting —
reads `getPortCenter`, so this is the only place that changes. One consequence:
`getContentTopY()` takes the **maximum** y over all inputs rather than the last one's, because with
more than one column an odd jack count leaves the second column a row short, so the last jack is not
the lowest.

**Tabbed control body.** The 23 controls are grouped into five pages — Tune / Unison / Phase / Sub /
File — by the page table in `WavetableTabStrip.cpp`, which maps parameter *display names* to pages.
The strip is a real collaborator: `WavetableTabStrip` (`Source/UI/Graph/ModuleComponent/`) is a
`juce::Component` child of the card that owns the tab buttons (`wtTab0`..`wtTab4`, one radio group
per card), the active page and the control -> page assignment. `ModuleComponent` still owns the
sliders/combos and lends them to the strip via `addSlider`/`addCombo`, so modulation rings, drop
targeting and MIDI Learn keep reading the card's own arrays; the strip only toggles their visibility
and sets their bounds. `ModuleComponent` keeps just the hookup (`createWavetableTabs()`) and the
`onPageChanged` callback (re-layout, repaint, `notifyModuleContentChanged()`). Three controls sit outside the strip: `Position` and `Warp` / `Warp Amt` are **pinned**
above it (they are what you actually perform with), and `Table` is laid out in the chrome band
beside the display it selects.

- **Jacks are never tabbed.** All 16 CV inputs stay on the card at all times, so a cable can never
  point at a hidden port. Only knobs and combos page.
- **Anything that paints from a knob's bounds must check visibility.** A hidden knob keeps the
  bounds it had when its page was last laid out. Modulation rings go through
  `getModRingSliderIndex()` and drop targeting through `getModTargetPortForPoint()`; both return
  "none" for a knob whose page is hidden, so a ring cannot paint over empty card and a hidden knob
  cannot swallow a cable drop.
- The card is sized to the **tallest** page (`WavetableTabStrip::getTallestPageHeight()`), not the
  active one. A card that grew and shrank would
  shove its neighbours around the canvas on every tab click.
- Page knob rows are **centred** and page combos run three across — most pages carry fewer than the
  6 available knob columns, and left-aligning them stranded half the card's width.
- `createWavetableTabs()` must run **after** `createControls()` (it hands the strip the controls that
  call creates) and must end by calling `updateLayout()` — `createControls()` has already sized the card,
  so without a second pass the card keeps its flat-grid height and the tabbed layout never applies.

**Chrome beside the ports.** The display, `Table` combo, button row and caption sit **beside** the
jack stack, starting just under the header — the same reclaim the Parametric EQ card makes for its
response curve (see [visualizers](visualizers.md)). The body still starts below the lowest jack.

The final size is pinned by `EstimatedModuleSizesMatchTheRealComponents`;
`WavetableCardSplitsItsJackGutterIntoTwoColumns` and
`WavetableTabsSwitchContentWithoutResizingTheCard` guard the two mechanisms above, including that
every knob is reachable from some page — a control on no page is unusable.

`ModuleComponent` also builds two file affordances for Wavetable modules: a **"Load Wavetable..."**
`TextButton` opening an async `juce::FileChooser` starting in the module's browser folder, which on
success calls `loadWavetableFile()` and switches the module's `table` choice to `Loaded File`; and a
**folder browser row**, `[Folder...] [<] [>] [caption]`, where `Folder...` opens an async directory
chooser and calls `setWavetableFolder()`, `<` and `>` step the folder cursor, and the caption shows
the loaded file name and `index/total`. Both completion lambdas hold a `Component::SafePointer` and
re-derive the module via `dynamic_cast`, since the card can be destroyed while the dialog is open.
The card also implements `FileDragAndDropTarget` for audio files, routing drops through the same
import path as the Load button.
