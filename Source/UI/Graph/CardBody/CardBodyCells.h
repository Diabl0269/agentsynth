#pragma once

// The cells a section's items make, shared by the layouts that place them beside each other (the row presentation
// in CardBodyLayout.cpp, the lane grid and mode row in CardBodyLanes.cpp).

#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/CardBody/CardBodyPlan.h"

namespace synth::cardcells {

/** One place in a run: an item, or a swap group (`group` >= 0) showing `item`, -1 when no member's condition holds
 *  (the cell stays, empty, so the card keeps its height). */
struct Cell {
    int item = -1;
    CardBodyItem::Kind runKind = CardBodyItem::Kind::Knob;
    int group = -1;
};

/** `indices` (into plan.items) as cells: a swap group is one cell when `groupSwaps`, and an item a code rule took
 *  the cell away from is left out. */
std::vector<Cell> cellsOf(const CardBodyPlan& plan, const std::vector<int>& indices, bool groupSwaps);

/** Places `cell` (caption and widget, and a swap group's other members) exactly as its run layout would place a run
 *  of one in `box`, a box that is the cell's own (never a double-width card, one column). */
void placeCellIn(const CardBodyPlan& plan, juce::AudioProcessor& module, const Cell& cell, juce::Rectangle<int> box,
                 const cardbody::BodyGeometry& g);

} // namespace synth::cardcells
