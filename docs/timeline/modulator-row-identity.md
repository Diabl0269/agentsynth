# Modulator row identity

A modulator row under an automation lane ([automation](automation.md#modulators)) is its routing's
`ModulatorInfo::key()`, and every edit that re-cables a routing gives it a new Attenuverter and so a new key: Change
source, Change parameter, and the undo or redo of either. Read by key alone that is one row removed and another added (a
fade out, a fade in, the fold dropped, the lane block's height jumping, the row moving to the bottom of its lane). The
panel pairs them instead, so the row keeps its place, its component, its fold and keyboard focus.

## One change, seen whole

The edit reaches the panel in several steps (cable the new routing, re-key the amount lane, write the order, cut the old
routing), each with its own doc or graph notification, and an undo restores the timeline before the graph. Derived at
any step in between, the rows would see a routing come and another go in separate changes. So:

- `MainComponent::repointModulator` (Change source) and `retargetLaneWithAttachments` (Change parameter) hold the rows
  with `TimelineAutomationLanes::ModulatorHold` for the whole recorded step.
- An undo or redo step holds them while `AppUndoManager::isRestoring()`; the panel catches up once per step through
  `AppUndoManager::addRestoreSettledHook` (a weak hook: neither side has to outlive the other).
- While held, a refresh only notes that one is due; the rows re-derive once, when the last hold goes.

## The order follows the routing

A lane's `modulatorOrder` is keyed by the routing's Attenuverter uuid (`modulatorOrderKey`), which these edits replace.
Change source writes the new uuid into the old one's slot in the order of every lane on the jack, inside the recorded
step, so the undo restores the old order with the old routing. Change parameter and Duplicate do the same with every
carried routing (`remapModulatorOrder`). An order never stored (graph order) is first written out from the rows as they
show (`MainComponent::shownModulatorOrder`), so the replaced routing cannot fall back to the end of graph order.

## Pairing

`replacedRoutings(before, after)` (`ModulatorInfo.h`) compares one lane's rows (row order) from the last derivation with
the new ones (`TimelineAutomationLanes::noteReplacedRoutings`):

- a routing that went pairs first with a newcomer from the same source output (Change parameter re-cables each routing
  from its own source);
- the rest pair in row order when exactly as many went as came (Change source puts a new source in the old slot);
- anything else is a real removal or addition and fades out or in as before. A routing removed and added back in a
  later change is new: its row fades in, open.

## Re-binding in place

A paired routing hands its fold state, a fold tween and a glide in flight to its successor (`noteReplacedRoutings`, before
the fold set drops keys that are gone), and its row and band re-bind to it (`rekeyModulators`): `ModulatorRow::setRouting`
and `ModulatorBand::setModulator`. The row:

- swaps what shows the source with the [in-place swap motion](../layout/animation.md#controls-swapping-in-place)
  (`SwapMotion`): pictures of the old name button, shape picture and LFO controls shrink away (190 ms), then the new ones
  grow in with the 8% bounce. It lands at once off screen, under Reduce Motion or with Animations Off. Tests force it
  with `setForceSwapMotionForTest` and step it with `stepSwapMotionForTest`.
- builds its LFO controls the first time it shows an LFO, and hides them when an LFO becomes an envelope;
- sends every edit to the source it shows at the time (the control callbacks read the row's current info).
