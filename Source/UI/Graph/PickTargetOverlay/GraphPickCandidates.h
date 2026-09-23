#pragma once

#include "UI/Graph/PickTargetOverlay/PickCandidate.h"
#include <vector>

class GraphEditor;

namespace synth::ui {

/** Appends every learnable control on every module card of `editor`'s canvas. A free function beside
 *  the overlay, not a GraphEditor method: GraphEditor is a thin owner (Source/UI/CLAUDE.md), and the
 *  cards already expose their own registries. */
void collectGraphPickCandidates(GraphEditor& editor, std::vector<PickCandidate>& out);

} // namespace synth::ui
