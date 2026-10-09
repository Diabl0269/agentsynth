#pragma once

// SignalFlowOrder.h -- the order a patch comes to life in when a project opens: sources first, then what they feed,
// the output last (docs/layout/animation.md#project-load-reveal). Pure: vertices are plain indices, edges plain pairs,
// so it has no graph or component types and is unit-tested headlessly.

#include <utility>
#include <vector>

namespace synth::ui {

/** The wave level of every vertex: 0 for a vertex nothing feeds, otherwise one past the deepest vertex feeding it.
 *  A cycle is broken deterministically: when no vertex is free, the lowest-index vertex left goes next, one past the
 *  deepest of its feeders already placed. Every vertex flagged in `last` (the output) comes after every other one,
 *  keeping their order among themselves. Self-edges and out-of-range edges are ignored. */
std::vector<int> signalFlowLevels(int vertexCount, const std::vector<std::pair<int, int>>& edges,
                                  const std::vector<bool>& last = {});

} // namespace synth::ui
