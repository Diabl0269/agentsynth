# Motion around a module inserted between others

What moves when a module is dragged between others, beyond the cards'
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

## Tests

`Tests/UI/Graph/InsertGap/InsertGapGhostTests.cpp` (the slide, through the real library and card drags).
