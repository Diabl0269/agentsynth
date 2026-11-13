#pragma once

// Shared by AutoArrangeTests.cpp and AutoArrangeMacroTests.cpp: a comparable snapshot of everything auto-arrange
// writes, and the "no two layout units overlap at any nesting level" check. Header-only; not compiled on its own and
// not registered in Tests/CMakeLists.txt.

#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/LayoutUtil.h"
#include <gtest/gtest.h>
#include <map>

namespace autoarrange_test {

// Node x/y (what is saved), live component positions, and every macro's persisted card bounds.
struct Snapshot {
    std::map<juce::uint32, juce::Point<int>> nodes;
    std::map<juce::uint32, juce::Point<int>> comps;
    std::map<juce::String, juce::Rectangle<int>> macroBounds;

    bool operator==(const Snapshot& o) const {
        return nodes == o.nodes && comps == o.comps && macroBounds == o.macroBounds;
    }
};

inline Snapshot snapshot(GraphEditor& editor, juce::AudioProcessorGraph& graph) {
    Snapshot s;
    for (auto* node : graph.getNodes())
        s.nodes[node->nodeID.uid] = {static_cast<int>(node->properties.getWithDefault("x", 0)),
                                     static_cast<int>(node->properties.getWithDefault("y", 0))};
    for (auto* comp : editor.getModuleComponents())
        if (comp != nullptr)
            s.comps[comp->getNodeId().uid] = comp->getPosition();
    for (const auto& macro : editor.getMacros().getAll())
        s.macroBounds[macro.id] = macro.bounds;
    return s;
}

// Units (loose modules, open hulls, collapsed cards) at the top level and inside every open macro never overlap.
inline void expectNoOverlaps(GraphEditor& editor) {
    auto& ctl = editor.getMacroController();
    std::vector<juce::String> containers{juce::String()};
    for (const auto& macro : editor.getMacros().getAll())
        if (!editor.getMacros().isEffectivelyCollapsed(macro.id))
            containers.push_back(macro.id);
    for (const auto& container : containers) {
        const auto units = ctl.buildLayoutUnits(container);
        for (size_t i = 0; i < units.size(); ++i)
            for (size_t j = i + 1; j < units.size(); ++j)
                EXPECT_FALSE(units[i].rect.intersects(units[j].rect))
                    << units[i].key << " " << units[i].rect.toString() << " overlaps " << units[j].key << " "
                    << units[j].rect.toString() << " in container '" << container << "'";
    }
}

// A source block that only feeds `target` is in the target's row (overlapping it vertically) and left of it.
inline void expectBeforeInRow(const juce::Rectangle<int>& source, const juce::Rectangle<int>& target,
                              const char* what) {
    EXPECT_LT(source.getRight(), target.getX()) << what << ": source is not left of the target";
    EXPECT_TRUE(source.getY() < target.getBottom() && source.getBottom() > target.getY())
        << what << ": source is not in the target's row";
}

} // namespace autoarrange_test
