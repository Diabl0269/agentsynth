#pragma once

// Moving and renaming a whole group in the on-card layout editor, as pure functions over a layout and
// rectangles, so the drag preview, the keyboard move and the write all come from one plan.
// docs/layout/module-card-layout.md#editing-a-layout.

#include "Modules/CardLayout.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::ui::oncard {

/** One group's strip of the card: from its header's top down to the next group (card pixels). Groups stand flush,
 *  so a strip's extent already holds the space the layout leaves under the group. */
struct GroupBlock {
    int top = 0;
    int extent = 0;
};

/** Where each block's top sits once the blocks stand in `order` (`order[k]` = the block at place k), starting at the
 *  first block's top. Indexed by the block's own index, not its place. */
std::vector<int> stackedTops(const std::vector<GroupBlock>& blocks, const std::vector<int>& order);

/** The order of `count` blocks after block `from` moved to place `to` (the rest keep their order). */
std::vector<int> orderAfterMoving(int count, int from, int to);

/** True when `order` leaves every block where it is. */
bool isIdentityOrder(const std::vector<int>& order);

/** What a screen reader hears after a move: "Unison moved below Output" (the group it passed, now next to it) or
 *  "Unison moved above Pitch". `titles` are the groups' names before the move, `order` as for `stackedTops`,
 *  `moved` the block that moved. Empty when nothing moved. */
juce::String describeGroupMove(const std::vector<juce::String>& titles, const std::vector<int>& order, int moved);

/** `layout` with its sections at `slots` (indices into `layout.sections`, ascending) holding the sections that stood
 *  there in `order`: place k gets the section that stood at `slots[order[k]]`. Every other section stays put, and
 *  every section keeps its items and the positions in them, which are measured from the section's own top. */
CardLayout withGroupsReordered(CardLayout layout, const std::vector<int>& slots, const std::vector<int>& order);

/** `layout` with section `index` titled `title` (trimmed); unchanged when the title is blank or the index is out of
 *  range, so an emptied title reverts to the old name. */
CardLayout withGroupTitle(CardLayout layout, int index, const juce::String& title);

} // namespace synth::ui::oncard
