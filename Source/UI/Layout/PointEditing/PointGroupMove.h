// PointGroupMove.h -- moving a group of selected dots as one rigid block along one axis: how far each dot may go,
// and the one delta the whole group then moves by (docs/control/point-editing.md). Pure math.
#pragma once

#include <vector>

namespace synth::ui {

/** How far one moving dot may go along an axis: `below` (zero or less) toward lower positions, `above` (zero or more)
 *  toward higher ones. */
struct PointRoom {
    double below = 0.0;
    double above = 0.0;
};

/** A dot at `position` that must stay within [lo, hi]. */
PointRoom roomWithin(double position, double lo, double hi);

/** One room per moving dot of `positions` (ascending), in order: each stops at the nearest dot that is not moving
 *  (or at `lo` / `hi`), so the block never passes a neighbour. `moving` is parallel to `positions`. */
std::vector<PointRoom> roomBetweenNeighbours(const std::vector<double>& positions, const std::vector<bool>& moving,
                                             double lo, double hi);

/** `delta` shortened so every dot stays within its room; the block stops as a unit at the first limit. A dot already
 *  outside its room never makes the block move back. */
double clampGroupDelta(const std::vector<PointRoom>& rooms, double delta);

} // namespace synth::ui
