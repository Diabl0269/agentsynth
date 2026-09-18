# Smart Connections

While a module is being placed, the editor can **suggest logical cables** to nearby modules and
auto-wire them on drop. The ghost and the landing rect it scores against come from
[layout](layout.md#drag-affordance); the wires it draws come from [cables](cables.md).

## Modes

`Settings -> Preferences -> Smart connections`, persisted as `smartConnectionMode` in
`juce::ApplicationProperties`. Default: **When main I/O is free** (`NewAndUnwired`).

| Mode | Library drop | Reposition existing module |
|---|---|---|
| **Off** | Never | Never |
| **New modules only** | Yes | Never |
| **When main I/O is free** (default) | Yes | Yes, when the jacks that would be wired are still free (source output and dest input) |
| **All module moves** | Yes | Yes (dest input must be free; a source that already fans out may still tap a free dest) |

Group multi-select drags never smart-connect, and snippet drops are excluded. "Dest input must be
free" has two exceptions, both below: the terminal audio sink takes a parallel cable anyway, and
**Ctrl** turns any occupied destination into an insert.

## How a suggestion is found

1. During `updateDragPreview()`, when the mode allows it, neighbours whose **module** rects are more
   than **96 px** away edge-to-edge are culled, then **jack-to-jack** distance is scored under the
   same 96 px cap. A pair is rejected when the source jack sits to the right of the dest jack, which
   is what stops wrap-around cables from a neighbour on the right into the dragged module's left
   inputs.
2. Compatible jack pairs are scored with `scoreJackPair` role matching against free destination
   jacks. In **When main I/O is free** the source output must also be unwired. Stereo requires
   explicit Left/Right (or Audio L/R) labels — two unlabelled ports, for example Math A/B, are never
   treated as L/R. The result is capped at the best neighbour's audio group (stereo L-to-L /
   R-to-R, or a mono-to-stereo fan of both legs, both-or-neither when a stereo dest has a taken leg)
   plus one MIDI suggestion. Mod-matrix and attenuverter destinations are never suggested. MIDI
   suggestions are limited to known MIDI sources and destinations (Sequencer, Poly MIDI, MIDI
   Keyboard, Oscillator, ...), because `ModuleBase` defaults `producesMidi()` / `acceptsMidi()` to
   true for almost every card.
3. Frosted preview cables are drawn in `paintOverChildren` at about 40% alpha via
   `drawConnectionWire`. Colours resolve **only** through `GraphEditor::colourForCable` and
   `synth::ui::resolveCableColour`, so the cable-colour mode and user overrides apply to previews
   too.
4. On drop, or from `finalizeModuleDrag`, pending suggestions are applied through `connectPorts` —
   the same path as a manual cable drag, including poly fans, MIDI and structural pitch/gate.

Library drags cache a short-lived `AIStateMapper::createModule` probe for jack metadata before a
real `ModuleComponent` exists.

## Occupied destinations

An already-wired destination jack is not one rule but three, depending on the modifier and the node:

| Drag | Occupied destination | Result |
|---|---|---|
| no modifier | terminal audio sink (Audio Output) | **additive parallel cable** — the ghost's audio out joins what is already there, existing cables untouched |
| no modifier | any other module | **nothing** (hard stop) |
| **Ctrl held** | **any** module | **insert in series** — the upstream cabling is rerouted *through* the ghost |

**Why the sink is special without a modifier.** Audio Output is a bare
`juce::AudioGraphIOProcessor` — never a `ModuleBase`, since the graph's output channel count is tied
to it — and is wired in essentially every real patch, so a hard stop there meant a module parked
next to it could never be offered anything at all. Summing into the mix bus is also exactly what
dragging a cable there by hand already does, so a parallel add is the unsurprising default. Every
other occupied jack stays a hard stop: silently summing into something the user wired mid-patch is
not a suggestion worth making.

**Why the modifier is Ctrl, not Cmd.** Cmd was tried first and lost: Cmd-click is the
additive-selection modifier (`ModuleComponent::mouseDown` — see [selection](selection.md)), and that
path `return`s *before* a body drag begins, so a Cmd-held press could never reach the drag path at
all. Ctrl is the literal Control key on every platform here, macOS included.

**Both press orderings work.** The modifier is read two different ways, and both are Ctrl:

1. **Press, then Ctrl.** `GraphEditor::isInsertModifierDown()` samples the keyboard on every drag
   tick, never latched at press time, so an ordinary drag becomes an insert the moment Ctrl goes
   down. Guarded by `SmartConnectionCtrlPressedMidDragTurnsTheSuggestionIntoAnInsert`.
2. **Ctrl, then press.** A Ctrl+press is ambiguous at mouse-down — it could be an additive-select
   toggle or an insert drag — so `ModuleComponent::mouseDown` **arms both** and lets mouse-up
   decide, the same deferred classification the piano roll uses for Cmd on a note body
   (`cmdToggleNote_`). The drag wins at press time because it needs state (dragger, undo capture,
   landing ghost) that cannot be conjured later; the toggle is the half that can be completed
   retroactively, and `mouseUp` finishes it **only if nothing moved**. Guarded by
   `SmartConnectionCtrlHeldBeforePressStillArmsAnInsertDrag`.

Two things are load-bearing for ordering 2:

- **Ctrl is tested before the additive modifiers.** On Windows and Linux JUCE defines
  `commandModifier` *as* `ctrlModifier`, so `isCommandDown()` is true whenever Ctrl is down. Testing
  additive first would early-return there and a Ctrl+drag could never arm the dragger — insert would
  be macOS-only. Taking the Ctrl branch keeps Ctrl+click toggling on those platforms anyway, through
  the deferred completion.
- **The module body menu keys on the true right button, not `isPopupMenu()`.** On macOS JUCE
  defines `popupMenuClickModifier` as `(rightButtonModifier | ctrlModifier)`, so `isPopupMenu()` is
  *also* true for Ctrl+**left**-click — which opened the card's context menu and returned before any
  drag could start. A card cannot open a menu and begin a drag from one press, and Ctrl+click must
  stay a selection toggle, so the legacy one-button-mouse affordance loses on the body. Right-click
  and two-finger tap still open the menu everywhere. Jack and knob menus are untouched and still
  open on `isPopupMenu()` — neither is on a drag path, so a Ctrl+click there still shows Disconnect
  and Automate as before.

**The Ctrl press collapses the selection onto the dragged card**, restoring the pre-press selection
in `mouseUp` if the press turns out to be a click. A group drag suppresses smart connections
entirely and moves every member, so a leftover multi-selection would otherwise silently disable
insert. Guarded by `CtrlClickTogglesSelectionButCtrlDragDoesNot`.

**Ctrl reaches the library drag too.** A library row starts its drag on mouse-down, and that press
was guarded by `isPopupMenu()` — which on macOS is `(rightButtonModifier | ctrlModifier)` — so a
Ctrl-held press never started a drag and Ctrl+drag-from-library could not reach the canvas. The
guard is `ModuleLibraryComponent::pressSuppressesRowDrag` (true right button only), the same call
the module body makes. Guarded by `ModuleLibraryRowPress.CtrlLeftClickDoesNotSuppressTheDrag` and
`SmartConnectionCtrlLibraryDropInsertsIntoAnOccupiedModule`.

**A modifier change is not a mouse move.** Suggestions used to be recomputed only from
`updateDragPreview`, so pressing or releasing Ctrl while the mouse was still did nothing — most
visibly, releasing Ctrl left a stale insert preview on screen.
`GraphEditor::refreshSuggestionsIfInsertModifierChanged()` re-samples on the existing 30 Hz drag
tick (no new timer) and recomputes only when the sampled state actually flipped, so a drag holding
its modifier costs one bool compare per tick and `refreshSmartSuggestions` still repaints only when
the suggestion set really changed. It is seeded at `beginDragPreview`, so a drag started with Ctrl
already held is not reported as a change on its first tick. Guarded by
`SmartConnectionReleasingCtrlMidDragDowngradesTheInsert`, which flips the modifier twice without
moving the mouse.

**Insert previews are tinted.** An insert reroutes existing cabling rather than adding to it, so its
new legs are drawn tinted toward the theme's `warning` token — interpolated over the colour
`resolveCableColour` already produced, never replacing it, so the cable's own
signal/category/user-override identity still reads through. The doomed cables keep their dashed
dimmed treatment. A token, not a literal.

**Ghost jack positions come from one constant.** `GraphEditor::estimatePortCenter` (drag ghost) and
`ModuleComponent::getPortCenter` (real card) have to agree, and they carried separate header
literals, 30 and 38. Every preview cable therefore terminated 8 px ABOVE the jack dot it claimed to
land on, floating over the jack's label row. Both now read
`ModuleComponent::kPortGutterHeaderHeight`. Guarded by `GhostPortEstimateMatchesTheRealJackCentre`
(estimate versus real, every jack) and
`SmartConnectionPreviewLegsLandOnTheRealDestinationJack` (end-to-end, within 1 px).

**A dual FX output pairs onto a collapsed input.** A collapsed destination jack is ONE visible jack
owning two raw channels, so the only way to fill both is a fan from a single cable — there is no
cable-level way to address its right leg on its own. A dual I/O FX's Left jack was being treated as
mono and stride-0 duplicated onto both destination legs, which also made the Right jack's pair
redundant so the dedupe dropped it: a dual Reverb landing on a collapsed Chorus wired only Left.
`resolvePolyLink` pairs L to raw0 and R to raw1 for a dual source whose right leg is **adjacent**
(the FX raw0/raw1 layout).

This is deliberately limited to adjacent legs. The split-block voice modules (Oscillator, Filter,
VCA, Wavetable) put Audio R on its own `kRightBase` block far from ch0, and for those the
established behaviour is the mono broadcast —
`ResolvePolyLinkBroadcastsMonoIntoCollapsedStereoPair` encodes it directly, and
`TogglingDualIOKeepsBothStereoLegs` covers their right leg being picked up separately from the
module's own Audio R block. Widening the rule to non-adjacent legs would change **manual cable
drags** as well, which is why it is a deliberate call rather than something folded into a
smart-connect fix. Guarded by `SmartConnectionDualOutputWiresBothLegsIntoACollapsedInput` and
`ResolvePolyLinkPairsADualSourceOntoACollapsedDestination`.

## Insert in series

`SmartSuggestion::isInsert` turns one suggestion record into the reroute. It carries two cable
**sets** — `doomedLinks` (upstream to destination, all to be removed) and `upstreamCables` (upstream
to ghost, replacing them) — plus the record's own `ghostJack` to `neighborJack`.

**Both sets describe the whole insert group, not the record's own leg**, and both are deduped, so
applying them once per suggestion is idempotent. This is load-bearing: a Dual I/O upstream reaches
the destination through *two* distinct cables, while a collapsed ghost output fans across both raw
legs so only one jack pair survives the dedupe. Hanging the doomed link off the surviving pair loses
the other one, and that cable stays connected, summing into the destination's right leg alongside
the ghost's output. Guarded by
`SmartConnectionCtrlInsertRemovesEveryDoomedLegOfADualIOUpstream`.

`upstreamCables` is deduped by the raw **ghost-input** channels each cable covers — the mirror of
the destination-side rule, since a collapsed jack on *either* end fans. Without it, a collapsed
upstream's single jack wired into both of a Dual I/O ghost's inputs would duplicate its left leg
onto the right. Guarded by
`SmartConnectionCtrlInsertDoesNotDuplicateOneUpstreamLegOntoADualIOGhost`.

All of these must hold, or nothing is offered:

- **Ctrl is held.** Nothing ever inserts without it.
- The ghost is the cable **source** and has at least one audio **input** leg. A pure source
  (Oscillator, LFO) is refused: there is nothing for the rerouted upstream to feed. At the sink a
  source instead gets the parallel add above.
- **Every** leg of the group is occupied, by one and the same upstream node — both-or-neither,
  mirroring the stereo group rule. A mix of free and occupied legs, or two different feeds, would
  change the summing.
- The feeding cable resolves through `findSingleUpstreamAudioLink`, which works at **cable** level
  (a visible output jack, never a raw graph edge) and returns nothing for a jack summed from several
  cables or fed through a mod routing or attenuverter chain.
- The upstream is not the ghost itself, which would self-loop.

**Hit-testing keys on the destination's input side.** The jack-to-jack filter measures the ghost's
output jack against the destination's *input* jack centre and rejects a source sitting to its right,
so the ghost has to be approaching from the left — the natural insert position.

**Aim at the gap.** A library drag CENTRES the ghost on the cursor (`ghostTopLeftForCursor`), which
is what every other drag-and-drop surface does. A canvas MOVE keeps its grab-point anchoring, since
that card is already under the user's finger.

For the INSERT path the jack-level left-to-right test is skipped. Both of its halves assume the
ghost sits clear to the LEFT of the card it is being wired into, which is true for a new cable and
false for an insert: the natural aim is the gap between two wired cards, or the cable itself, and a
card is wider than the gap, so the ghost necessarily OVERLAPS its destination there. Instead the
insert relies on the module-level proximity cull plus one guard — the ghost's centre must not be
past the destination's **right edge**, because dragged clean past a card is not "insert into it",
and that is also what stops an insert being offered into the upstream the ghost has already moved
beyond.

**Candidacy is judged from the aim, not the landing spot.** `dragPreviewAim` is the
un-de-overlapped rect under the cursor. A ghost aimed into a gap narrower than itself gets pushed
clear by `resolvePlacement`, so scoring only the landing rect meant aiming at the gap could never
earn a suggestion. The proximity cull measures against whichever of the two is closer; the card
still *lands* at `dragPreviewGhost`, and `findFreeSlot` owns the final geometry either way. Preview
cables are drawn from the landing spot, so they show where the card is really going.

Measured with a positional sweep of two 280 px cards with a 140 px gap, the cursor window spans
**740 px covering the whole gap and the whole destination card**, bounded exactly at the
destination's right edge by the guard — where the jack-level rule alone gave 140 px, sitting 210-350
px LEFT of the destination and never over the gap. It is identical for every FX type, and the old
far-left aim is still inside the window. `SmartConnectionFxInsertTest` runs the gap aim across all
twelve FX; `SmartConnectionInsertAimWindowSpansTheWholeGap` walks the span and checks the
past-the-destination guard; `SmartConnectionPlainSuggestionKeepsTheLeftToRightFlowRule` pins that
the relaxation is insert-only.

**One jack pair survives per distinct set of raw destination channels.** A collapsed jack already
fans across the whole raw pair, so when the destination fronts two legs — the sink is the only node
that does — a second pair for its right leg would wire the source's *left* leg there too. This
applies to plain adds and inserts alike and is a no-op wherever the pairs already claim distinct
raws; the doomed links are collected before it and are deliberately unaffected.

Scoring and the 96 px proximity cap are unchanged by an insert: it scores like the plain cable it
replaces, so a neighbour offering a free jack can still win.

**Dual ghosts wire per leg.** When the ghost is Dual I/O, each of its legs gets its own cable on
both sides — upstream L to ghost L, upstream R to ghost R, ghost L to dest L, ghost R to dest R. No
leg is folded or dropped. The one place a leg still folds is a *collapsed* ghost: its single input
jack owns both raw channels, so a Dual I/O upstream feeding it can only reach the ghost through one
cable and the upstream's right leg is not carried. That is inherent to collapsing a split pair, and
the destination is left correctly wired either way.

**The probe carries the Dual I/O default.** The library-drop ghost is an
`AIStateMapper::createModule` probe, and its jack layout decides *both* the preview and the plan
applied on drop, so it goes through the same `applyDefaultDualIOForNewModule` (global default plus
per-module override) the real module gets in `itemDropped`. Skipping it was a live bug: with the
default set to dual, the plan was computed for a collapsed ghost and then applied to a module that
spawned dual, and only the left legs got wired — the ghost's fan resolved to one raw channel per
jack instead of two, leaving the upstream's and destination's Right jacks dangling. Guarded by
`SmartConnectionProbeHonoursTheDualIODefault` and
`SmartConnectionCtrlInsertWiresBothLegsOfADualGhostBetweenDualNeighbours`.

**The preview draws resolved legs, not one segment per suggestion.** One suggestion is not one
cable: `connectPorts` fans a collapsed jack across a whole raw pair, and when the far end fronts
those raws as two separate visible jacks — the terminal sink does, having no `ModuleBase` to group
them — that is *two* cables on screen. `mainPreviewLegs` and `upstreamPreviewLegs` are resolved from
the same `PolyLink` `connectPorts` walks, mapped back to visible jacks and deduped to distinct jack
pairs, so N raw edges through one jack pair are still one cable. Guarded by
`SmartConnectionParallelAddPreviewCoversBothOutputLegs`.

*Every* doomed cable is stroked first, dashed and dimmed at about 18% alpha, underneath the frosted
segments that replace them — otherwise the extra previews read as "and also", and the user expects
the old wires to still be there after the drop. Paint-only; no new timers or repaints.

**Apply.** `applySmartSuggestions` drops **every** doomed cable first (`disconnectAudioLink`, the
exact inverse of `connectPorts`, so a collapsed stereo wire takes both raw legs), then wires each
`upstreamCables` entry into the ghost, then the ghost's own leg into the destination. Dropping only
the current leg's cable would leave the other summing in. All of it shares the caller's transaction,
so **one** undo restores the original patch.

## Double-click a port to disconnect

Double-clicking a **connected** jack removes every cable on that port — the same path as the
right-click **Disconnect** menu (`GraphEditor::disconnectPort`, which fans across every raw channel
a visible jack owns). An unconnected jack is a no-op. The first click of a double-click still begins
(and immediately ends) a cable drag; the second click is intercepted in
`ModuleComponent::mouseDown` (`getNumberOfClicks() >= 2`) so it does not start another drag.

`Settings -> Preferences -> Double-click port to disconnect`, persisted as
`doubleClickPortDisconnect` in `juce::ApplicationProperties`. Default: **on**. Restored in
`MainComponent::initialiseCommon()`, so the canvas honours it without opening Settings. When off,
double-clicking a jack behaves like two single clicks, i.e. a cable drag.
