# Edit Layout: the drop target

Part of [Module card layout](module-card-layout.md#editing-a-layout). What Edit Layout shows while a control is dragged,
and what a group does when its last control leaves.

## The drop target

While a control is dragged the group it would land in is the only one at full strength: every
other stacked group dims (55% of the card's surface laid over it), the target opens a **dashed slot** where the
control would land (the cell the plan below gives it, grown 2 px, with a faint accent wash) and a **chip** on the
slot's top edge names the spot in words, "Stages, line 2" (the group's title, else its name from its id, and
the row among the group's cells as the drop leaves them; a drop below the last row is the next line). Exactly one
landing spot is lit at every moment: the slot, or, with the pointer below the card, only the "Drop to hide" area
(every group is dimmed then and no slot is drawn). The neighbours already glide aside to make room (see [Moving between groups](module-card-layout.md#editing-a-layout)), so the
slot is the gap that opens. The dim and the slot fade in over 120 ms when the drag begins (ease out) and out when
it ends or Esc cancels it (ease in), at once under Reduce Motion or Animations Off
(`CardLayoutOnCardEditorDrag.cpp`; test seams `getLitSlotCountForTest`, `getDropSlotForTest`,
`getDropSlotNameForTest`, `getDimmedSectionCountForTest`). Lane, row, mode-row, footer and tab groups are not drop
targets and never dim.

## A group keeps its title

A titled grid group whose last control left (moved or hidden) keeps its title row and
one row of room under it, drawn in Edit Layout as a quiet dashed slot reading "Drop a control here"
(`CardBodyPlan::Section::isEmptyTitled`, `getEmptySlotsForTest`); a control dropped there joins the group. A
title no longer vanishes with its last control.
