# Motion around an inserted or deleted module

What moves when a module is dragged between others, dropped on a cable, or deleted out of a chain, beyond the cards'
own glide ([Adding a module between others](animation.md#adding-a-module-between-others), the
[layout rule](layout.md#making-room-for-a-module-dropped-between-others)). All of it follows the
[Motion rules](animation.md#motion-rules): Reduce Motion is the short fade or lands at once as written below,
Animations: Off and a canvas that is not showing land at once.

## The hovered module slides to the gap

The landing ghost of the dragged module (library or canvas card) follows the pointer a grid step at a time, but when
the drag opens a gap it takes the gap's slot, and that jump is a 160 ms `easeOutCubic` slide, never a jump
(`DragGhostSlide`, `Source/UI/Graph/DragGhostSlide/`, owned by `GraphDragDropController::getGhostSlide()` and aimed from
`GraphEditor::refreshSmartSuggestions`, which runs on every drag tick).

- A move of the aim up to 16 px on both axes is followed one to one, so the ghost does not lag the pointer. A bigger one
  starts a slide; an aim that changes mid-slide carries on from where the ghost is drawn (never from where it began),
  and the same aim again leaves the slide running.
- The preview cables' ghost-side ends ride the drawn ghost, so a cable never points at a spot the ghost has left.
- **Reduce Motion** and **Animations: Off** land the ghost at once (a ghost only marks a spot, it has no fade to turn
  into); so does a canvas that is not showing. The drag ending, or a new one beginning, forgets the ghost.

## Cables around an inserted or deleted module

A module dropped onto a cable, and a module deleted out of a chain, change the cabling without any cable coming or
going: one cable becomes two, or two become one. They move that way instead of retracting and growing
(`CableRetractAnimator` chains, [cables](cables.md#retracting-removed-cables)):

- **Insert splits the cable.** The old cable A to C is replaced by A to B and B to C. Both new halves start exactly as the
  old cable (so nothing pops) and run, 220 ms `easeOutCubic`, to their own ends: the first half's far end from C to the
  new card, the second half's near end from A to the new card. Every cable of a stereo or fanned insert splits by itself.
- **Delete joins the cables.** The two cables A to B and B to C of the deleted card run onto the single cable A to C that
  the delete heals (both ends of each half, `easeOutCubic`); the new cable is not drawn until the halves have reached
  it, so it appears exactly under them.
- **Reduce Motion** is the plain 80 ms fade: the old cable or cables out, the new ones in, with no geometry change.
- Cables that are not a chain keep their ordinary motion; the insert and the delete arm nothing but the chain
  (`GraphEditor::morphCablesSince`). Undo and redo of either use the same chains through `retractCablesGoneSince`.
- A card still gliding when the cables run is followed by the halves' live ends; the ends that stay put are where they
  were drawn when the change was made.

## Outlines follow the card's corners

Every outline that wraps a module card is drawn on the card's body, which `AppLookAndFeel::drawModulePanel` draws 2 px in
from the component's bounds (`synth::ui::cardBodyRect`, `Source/UI/Graph/CardOutline.h`) with the theme's
`metrics.cornerRadius`: the landing ghost, the 1 px accent ring that fades round a card an undo brought back, the
macro-crossing flash ring (which grows concentrically from that radius), the placeholder outline of a card still
waiting for its assets while a project opens and the hover outline of Pick on canvas. A macro card is drawn by
`MacroCardComponent` on its whole component, 8 px round with its border 1 px in, so its undo ring uses that instead.
A new outline around a card takes both from there, never a literal.

## Tests

`Tests/UI/Graph/InsertGap/InsertGapGhostTests.cpp` (the slide, through the real library and card drags),
`Tests/UI/Graph/CableChainMotionTests.cpp` (the arming rules, then the real drop, Delete key and Reduce Motion paths, and
the outline corners read back from rendered pixels).
