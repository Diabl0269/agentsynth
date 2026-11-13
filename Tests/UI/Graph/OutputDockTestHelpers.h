#pragma once

// Shared by the tests that open a project and assert "nothing moved on load": the output dock (Master / Rec Tap /
// Audio Output) is the one thing a load DOES move, by design, so those tests compare every other card with what was
// saved and the dock cards with the position computeOutputDock derives for the loaded canvas.
// Header-only; not compiled on its own and not registered in Tests/CMakeLists.txt.

#include "Mixer/MasterSplice.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/LayoutUtil.h"
#include <map>

namespace outputdock_test {

inline bool isDockNode(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr && synth::isOutputDockProcessor(node->getProcessor());
}

/** Where each dock card must be: computeOutputDock over every non-dock layout unit, on Audio Output's own y. */
inline std::map<juce::uint32, juce::Point<int>> expectedDockPositions(GraphEditor& editor,
                                                                      juce::AudioProcessorGraph& graph) {
    const auto dock = synth::outputDockNodes(graph);
    std::map<juce::uint32, juce::Point<int>> result;
    if (dock.empty())
        return result;
    std::vector<juce::Point<int>> sizes;
    std::vector<juce::String> keys;
    for (auto* node : dock) {
        keys.push_back("n:" + juce::String((juce::int64)node->nodeID.uid));
        for (auto* comp : editor.getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == node->nodeID)
                sizes.push_back({comp->getWidth(), comp->getHeight()});
    }
    std::vector<synth::LayoutUtil::LayoutUnit> content;
    for (auto& unit : editor.getMacroController().buildLayoutUnits({}))
        if (std::find(keys.begin(), keys.end(), unit.key) == keys.end())
            content.push_back(unit);
    const auto positions =
        synth::LayoutUtil::computeOutputDock(content, sizes, static_cast<int>(dock.back()->properties["y"]));
    for (size_t i = 0; i < dock.size() && i < positions.size(); ++i)
        result[dock[i]->nodeID.uid] = positions[i];
    return result;
}

} // namespace outputdock_test
