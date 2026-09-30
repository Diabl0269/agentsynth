// MacroProjectLoadTests.cpp
// Opening a project whose macro contains a module must build the canvas exactly as saved: one card per
// node, every card at its saved position, and no layout side effects from card construction. A card's
// constructor can call GraphEditor::handleModuleResized (the LFO does, to size its custom section); for a
// macro member that used to run the "make room" displacement mid-construction, which re-entered
// updateComponents() and recursed until the stack overflowed. The matrix test runs every module type the
// app can create through the same load so the next constructor that triggers layout is caught here.

#include "../UI/Graph/OutputDockTestHelpers.h"
#include "MacroContainer/MacroContainerTestHelpers.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "MacroSet.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include <gtest/gtest.h>

namespace {

struct LoadedNode {
    NodeID id;
    juce::String uuid;
    juce::Point<int> saved;
};

struct LoadCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    LoadCanvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
    }

    // A node as a project load leaves it: positions and uuid in the node's properties, and no component yet.
    LoadedNode addSavedNode(std::unique_ptr<juce::AudioProcessor> processor, int x, int y) {
        auto node = engine.getGraph().addNode(std::move(processor));
        const auto uuid = juce::Uuid().toDashedString();
        node->properties.set("x", x);
        node->properties.set("y", y);
        node->properties.set("uuid", uuid);
        return {node->nodeID, uuid, {x, y}};
    }

    void addMacro(const std::vector<LoadedNode>& members, bool collapsed) {
        synth::Macro macro;
        macro.name = "Macro";
        macro.collapsed = collapsed;
        macro.bounds = {members.front().saved.x, members.front().saved.y, 160, 60};
        for (const auto& m : members)
            macro.members.push_back(m.uuid);
        editor.getMacros().add(macro);
    }

    // The load step itself: the graph and MacroSet are populated, THEN the components are built.
    void openProject() { editor.updateComponents(); }

    int componentCount(NodeID id) {
        int n = 0;
        for (auto* comp : editor.getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == id)
                ++n;
        return n;
    }

    void expectLoadedAsSaved(const std::vector<LoadedNode>& nodes, bool expectComponents = true) {
        const auto dockPositions = outputdock_test::expectedDockPositions(editor, engine.getGraph());
        for (const auto& n : nodes) {
            EXPECT_EQ(componentCount(n.id), expectComponents ? 1 : 0);
            if (auto* comp = findComponent(editor, n.id)) {
                // The output dock is the one thing a load moves by design: it sits at computeOutputDock's position.
                const auto* owner = editor.getMacros().findByMember(n.uuid);
                if (owner != nullptr && outputdock_test::isDockNode(engine.getGraph(), n.id))
                    continue; // a dock card forced INTO a macro (this sweep tries every type) is not a dock layout case
                if (outputdock_test::isDockNode(engine.getGraph(), n.id))
                    EXPECT_EQ(comp->getPosition(), dockPositions.at(n.id.uid)) << "dock card " << (int)n.id.uid;
                else
                    EXPECT_EQ(comp->getPosition(), n.saved) << "node " << (int)n.id.uid << " was moved by the load";
            }
        }
    }
};

// The sibling is added first, so its card already exists (and can be pushed) when the subject's constructor
// runs; a loose neighbour beside the macro gives a displacement pass something to push. Everything sits close
// together on purpose. Returns {subject, sibling, loose}; the macro's members are the first two.
std::vector<LoadedNode> addMacroMembers(LoadCanvas& c, std::unique_ptr<juce::AudioProcessor> processor) {
    auto sibling = c.addSavedNode(std::make_unique<OscillatorModule>(), 420, 340);
    auto loose = c.addSavedNode(std::make_unique<OscillatorModule>(), 700, 300);
    auto subject = c.addSavedNode(std::move(processor), 400, 300);
    return {subject, sibling, loose};
}

} // namespace

TEST(MacroProjectLoad, OpenMacroContainingAnLfoOpensAsSaved) {
    LoadCanvas c;
    auto members = addMacroMembers(c, std::make_unique<LFOModule>());
    c.addMacro({members[0], members[1]}, /*collapsed=*/false);
    c.openProject();
    c.expectLoadedAsSaved(members);
}

TEST(MacroProjectLoad, CollapsedMacroContainingAnLfoOpensAsSaved) {
    LoadCanvas c;
    auto members = addMacroMembers(c, std::make_unique<LFOModule>());
    c.addMacro({members[0], members[1]}, /*collapsed=*/true);
    c.openProject();
    c.expectLoadedAsSaved(members);
}

TEST(MacroProjectLoad, EveryModuleTypeInsideAnOpenMacroOpensAsSaved) {
    const auto types = synth::AIStateMapper::moduleFactoryTypeNames();
    ASSERT_GT(types.size(), 0);
    for (const auto& type : types) {
        SCOPED_TRACE(type.toStdString());
        auto processor = synth::AIStateMapper::createModule(type);
        if (processor == nullptr)
            continue;
        const bool hasNoCard = dynamic_cast<AttenuverterModule*>(processor.get()) != nullptr;
        LoadCanvas c;
        auto members = addMacroMembers(c, std::move(processor));
        c.addMacro({members[0], members[1]}, /*collapsed=*/false);
        c.openProject();
        c.expectLoadedAsSaved({members[1], members[2]});
        c.expectLoadedAsSaved({members[0]}, !hasNoCard);
    }
}

TEST(MacroProjectLoad, EveryModuleTypeInsideACollapsedMacroOpensAsSaved) {
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames()) {
        SCOPED_TRACE(type.toStdString());
        auto processor = synth::AIStateMapper::createModule(type);
        if (processor == nullptr)
            continue;
        const bool hasNoCard = dynamic_cast<AttenuverterModule*>(processor.get()) != nullptr;
        LoadCanvas c;
        auto members = addMacroMembers(c, std::move(processor));
        c.addMacro({members[0], members[1]}, /*collapsed=*/true);
        c.openProject();
        c.expectLoadedAsSaved({members[1], members[2]});
        c.expectLoadedAsSaved({members[0]}, !hasNoCard);
    }
}

// Opening a project never runs the collapse-time restore: a neighbour that sits on an open macro's hull stays where
// it was saved, and no displacement record exists afterwards.
TEST(MacroProjectLoad, LoadNeverMovesNeighboursOrRecordsDisplacement) {
    for (const bool collapsed : {false, true}) {
        SCOPED_TRACE(collapsed ? "collapsed" : "open");
        LoadCanvas c;
        auto m1 = c.addSavedNode(std::make_unique<OscillatorModule>(), 400, 300);
        auto m2 = c.addSavedNode(std::make_unique<OscillatorModule>(), 700, 300);
        auto neighbour = c.addSavedNode(std::make_unique<OscillatorModule>(), 1000, 300);
        c.addMacro({m1, m2}, collapsed);
        c.openProject();
        c.expectLoadedAsSaved({m1, m2, neighbour});
        for (const auto& macro : c.editor.getMacros().getAll())
            EXPECT_TRUE(macro.displaced.empty());
    }
}
