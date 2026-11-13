// MacroProjectLoadTests.cpp
// Opening a project whose macro contains a module must build the canvas exactly as saved: one card per
// node, every card at its saved position, and no layout side effects from card construction. A card's
// constructor can call GraphEditor::handleModuleResized (the LFO does, to size its custom section); for a
// macro member that used to run the "make room" displacement mid-construction, which re-entered
// updateComponents() and recursed until the stack overflowed. The matrix test runs every module type the
// app can create through the same load so the next constructor that triggers layout is caught here.

#include "../UI/Graph/OutputDockTestHelpers.h"
#include "MacroContainer/MacroContainerTestHelpers.h"
#include "Modules/ModCurve.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "MacroSet.h"
#include "Modules/ADSRModule.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MacroControlModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardBody/MacroCurveGlyph.h"
#include <functional>
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

namespace {

// A saved project as the app writes it: the nodes with their params, and one macro grouping the LFO and the ADSR.
juce::var savedMacrosProject(const juce::NamedValueSet& macrosParams, const juce::var& macrosState) {
    auto node = [](int id, const juce::String& type, const juce::NamedValueSet& params) {
        auto* object = new juce::DynamicObject();
        object->setProperty("id", id);
        object->setProperty("type", type);
        object->setProperty("x", 100 * id);
        object->setProperty("y", 300);
        auto* paramsObject = new juce::DynamicObject();
        for (const auto& entry : params)
            paramsObject->setProperty(entry.name, entry.value);
        object->setProperty("params", juce::var(paramsObject));
        return object;
    };
    auto* macrosNode = node(1, "Macros", macrosParams);
    if (!macrosState.isVoid())
        macrosNode->setProperty("state", macrosState);
    juce::Array<juce::var> nodes{juce::var(macrosNode), juce::var(node(2, "LFO", {})), juce::var(node(3, "ADSR", {}))};
    auto* patch = new juce::DynamicObject();
    patch->setProperty("nodes", juce::var(nodes));
    return juce::var(patch);
}

MacroControlModule* macrosOf(LoadCanvas& c) {
    for (auto* node : c.engine.getGraph().getNodes())
        if (auto* macros = dynamic_cast<MacroControlModule*>(node->getProcessor()))
            return macros;
    return nullptr;
}

} // namespace

// Regression test for the per-knob polarity change: a project saved with the old bank-wide Bipolar switch on
// opens with every knob bipolar, beside an LFO and an ADSR in a macro, with every card built where it was saved.
TEST(MacroProjectLoad, AnOldProjectWithTheBankWideBipolarSwitchOpensWithEveryKnobBipolar) {
    LoadCanvas c;
    juce::NamedValueSet params;
    params.set("macroBipolar", true);
    params.set("macroCount", 8);
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(savedMacrosProject(params, {}), c.engine.getGraph(), true,
                                                       /*trusted=*/true, /*autoConnectNewNodes=*/false));
    auto* macros = macrosOf(c);
    ASSERT_NE(macros, nullptr);
    std::vector<LoadedNode> members;
    for (auto* node : c.engine.getGraph().getNodes())
        if (node->getProcessor() != nullptr && node->getProcessor() != macros &&
            node->properties.contains("uuid") == false)
            node->properties.set("uuid", juce::Uuid().toDashedString());
    for (auto* node : c.engine.getGraph().getNodes())
        if (node->getProcessor() != macros) {
            node->properties.set("x", 100 * (int)node->nodeID.uid);
            node->properties.set("y", 300);
            members.push_back({node->nodeID,
                               node->properties["uuid"].toString(),
                               {(int)node->properties["x"], (int)node->properties["y"]}});
        }
    ASSERT_EQ(members.size(), 2u);
    c.addMacro(members, /*collapsed=*/false);
    c.openProject();

    for (int i = 0; i < MacroControlModule::kMaxMacros; ++i)
        EXPECT_TRUE(macros->isBipolar(i)) << "knob " << i + 1;
    c.expectLoadedAsSaved(members);
}

TEST(MacroProjectLoad, AnOldProjectWithTheSwitchOffOpensWithEveryKnobUnipolar) {
    LoadCanvas c;
    juce::NamedValueSet params;
    params.set("macroBipolar", false);
    ASSERT_TRUE(
        synth::AIStateMapper::applyJSONToGraph(savedMacrosProject(params, {}), c.engine.getGraph(), true, true, false));
    c.openProject();
    auto* macros = macrosOf(c);
    ASSERT_NE(macros, nullptr);
    for (int i = 0; i < MacroControlModule::kMaxMacros; ++i)
        EXPECT_FALSE(macros->isBipolar(i));
}

TEST(MacroProjectLoad, KnobNamesAndPolaritySaveAndReloadThroughTheProjectJson) {
    LoadCanvas c;
    ASSERT_TRUE(
        synth::AIStateMapper::applyJSONToGraph(savedMacrosProject({}, {}), c.engine.getGraph(), true, true, false));
    auto* macros = macrosOf(c);
    ASSERT_NE(macros, nullptr);
    macros->setMacroName(0, "Drive");
    macros->getBipolarParameter(4)->setValueNotifyingHost(1.0f);
    const auto saved = synth::AIStateMapper::graphToJSON(c.engine.getGraph());

    LoadCanvas reopened;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(saved, reopened.engine.getGraph(), true, true, false));
    reopened.openProject();
    auto* copy = macrosOf(reopened);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->getMacroName(0), "Drive");
    EXPECT_EQ(copy->getMacroName(1), "M2");
    EXPECT_TRUE(copy->isBipolar(4));
    EXPECT_FALSE(copy->isBipolar(0));
}

// Each knob's response curve is saved with the project and shows as the card's glyph once reopened.
TEST(MacroProjectLoad, KnobCurvesSaveAndReloadAndTheReopenedCardShowsThem) {
    LoadCanvas c;
    ASSERT_TRUE(
        synth::AIStateMapper::applyJSONToGraph(savedMacrosProject({}, {}), c.engine.getGraph(), true, true, false));
    auto* macros = macrosOf(c);
    ASSERT_NE(macros, nullptr);
    macros->setMacroCurve(2, synth::ModCurve::preset(synth::ModCurve::Preset::Exp));
    const auto saved = synth::AIStateMapper::graphToJSON(c.engine.getGraph());

    LoadCanvas reopened;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(saved, reopened.engine.getGraph(), true, true, false));
    reopened.openProject();
    auto* copy = macrosOf(reopened);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->getMacroCurve(2), synth::ModCurve::preset(synth::ModCurve::Preset::Exp));
    EXPECT_TRUE(copy->getMacroCurve(0).isLinear());

    int bent = 0, straight = 0;
    // The glyphs are children of the knobs; walk the card's tree rather than reach for its body.
    std::function<void(juce::Component&)> visit = [&](juce::Component& parent) {
        for (auto* child : parent.getChildren()) {
            if (child->getComponentID() == "macroCurveGlyph")
                (static_cast<synth::MacroCurveGlyph*>(child)->isBent() ? bent : straight)++;
            visit(*child);
        }
    };
    for (auto* card : reopened.editor.getModuleComponents())
        if (card != nullptr && card->getModule() == copy)
            visit(*card);
    EXPECT_EQ(bent, 1);
    EXPECT_GE(straight, 1);
}
