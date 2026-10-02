// A module dragged into and out of an expanded macro re-routes its cables through (or away from) the
// macro's ports EVERY time it crosses the border, not just on the first crossing
// (docs/macros/menu-and-membership.md "Dragging without Cmd").

#include "AppUndoManager.h"
#include "MacroDragTestHelpers.h"
#include "Modules/LFOModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include <gtest/gtest.h>

namespace {

bool hasDirectEdge(AudioEngine& engine, NodeID src, NodeID dst) {
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == src && c.destination.nodeID == dst)
            return true;
    return false;
}

} // namespace

TEST(MacroDragLiveReroute, OutsideLfoDraggedInAndOutRepeatedlyReroutesEveryCrossing) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(2400, 1400);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 600);
    auto lfo = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 1000, 150);
    editor.connectPorts(lfo, 0, b, 0, /*isMidi=*/false);
    ASSERT_TRUE(hasDirectEdge(engine, lfo, b)) << "sanity: the LFO cable connected";
    // Grouping with the cable already there mints the input port a cable drag across the border would.
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro(/*autoCreatePorts=*/true);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    editor.setSelectedNodes({});
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u) << "sanity: grouping minted the LFO's input port";

    auto* compLfo = findComponent(editor, lfo);
    ASSERT_NE(compLfo, nullptr);
    const auto outsideCentre = compLfo->getBounds().getCentre();

    for (int round = 0; round < 3; ++round) {
        SCOPED_TRACE("round " + std::to_string(round));
        compLfo = findComponent(editor, lfo);
        ASSERT_NE(compLfo, nullptr);
        const auto hull = editor.getMacroController().macroHullBounds(macroId);
        juce::Point<int> heldAt;
        dragBodyBy(*compLfo, hull.getCentre() - compLfo->getBounds().getCentre(), kPlainClick,
                   [&] { heldAt = findComponent(editor, lfo)->getPosition(); });
        ASSERT_NE(editor.getMacroController().macroForNode(lfo), nullptr) << "LFO must join on the way in";
        EXPECT_TRUE(hasDirectEdge(engine, lfo, b)) << "inside, the LFO cable goes direct";
        EXPECT_TRUE(editor.getMacros().find(macroId)->ports.empty()) << "no port left once both ends are inside";

        compLfo = findComponent(editor, lfo);
        ASSERT_NE(compLfo, nullptr);
        dragBodyBy(*compLfo, outsideCentre - compLfo->getBounds().getCentre(), kPlainClick,
                   [&] { heldAt = findComponent(editor, lfo)->getPosition(); });
        ASSERT_EQ(editor.getMacroController().macroForNode(lfo), nullptr) << "LFO must leave on the way out";
        EXPECT_FALSE(hasDirectEdge(engine, lfo, b)) << "outside, the cable goes through a macro port";
        EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u) << "one input port for the LFO cable";
    }
}

// The founder's case: the LFO modulates a knob of a member (LFO -> inlet -> attenuverter -> knob),
// built by a real cable drag onto the knob, then the LFO is dragged in and out several times.
TEST(MacroDragLiveReroute, OutsideLfoModulatingAKnobReroutesEveryCrossing) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(2400, 1400);

    auto member = addModuleAt(editor, engine, std::make_unique<WavetableOscillatorModule>(), 100, 100);
    auto filler = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 600);
    editor.setSelectedNodes({member, filler});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    editor.getMacroController().setMacroCollapsed(macroId, false);
    auto lfo = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 1000, 150);
    editor.setSelectedNodes({});

    auto* memberComp = findComponent(editor, member);
    juce::Slider* position = nullptr;
    for (auto* child : memberComp->getChildren())
        if (auto* sl = dynamic_cast<juce::Slider*>(child); sl != nullptr && sl->getComponentID() == "Position")
            position = sl;
    ASSERT_NE(position, nullptr);
    editor.beginConnectionDrag(findComponent(editor, lfo), 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(memberComp->getBounds().getPosition() + position->getBounds().getCentre());
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u) << "sanity: the drop minted an inlet";

    const auto routesIntoMember = [&] {
        for (const auto& r : engine.getModulationRoutings())
            if (r.hasDest && r.destNodeID == member)
                return true;
        return false;
    };
    const auto outsideCentre = findComponent(editor, lfo)->getBounds().getCentre();
    for (int round = 0; round < 3; ++round) {
        SCOPED_TRACE("round " + std::to_string(round));
        auto* compLfo = findComponent(editor, lfo);
        const auto hull = editor.getMacroController().macroHullBounds(macroId);
        const juce::Point<int> inside(hull.getRight() - 140, hull.getBottom() - 60);
        dragBodyBy(*compLfo, inside - compLfo->getBounds().getCentre(), kPlainClick);
        ASSERT_NE(editor.getMacroController().macroForNode(lfo), nullptr) << "LFO joins";
        EXPECT_TRUE(editor.getMacros().find(macroId)->ports.empty()) << "inlet spliced out inside";
        EXPECT_TRUE(routesIntoMember()) << "modulation survives the join";

        compLfo = findComponent(editor, lfo);
        dragBodyBy(*compLfo, outsideCentre - compLfo->getBounds().getCentre(), kPlainClick);
        ASSERT_EQ(editor.getMacroController().macroForNode(lfo), nullptr) << "LFO leaves";
        EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u) << "an inlet is minted again outside";
        EXPECT_TRUE(routesIntoMember()) << "modulation survives the leave";
    }
}

// A plain cable from outside into a member runs outside -> inlet -> member. Cutting the outside leg
// leaves an inlet nothing feeds: it goes, and its inside leg with it.
TEST(MacroDragLiveReroute, CuttingTheOutsideLegOfAPortedCableClearsThePortAndTheInsideLeg) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(2400, 1400);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 600);
    auto src = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1000, 150);
    editor.connectPorts(src, 0, b, 0, /*isMidi=*/false);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro(/*autoCreatePorts=*/true);
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);
    const auto connectionsWithPort = engine.getGraph().getConnections().size();

    std::optional<GraphEditor::VisibleCable> outsideLeg;
    for (const auto& c : editor.buildVisibleCables())
        if (c.id.srcUid == src.uid)
            outsideLeg = c;
    ASSERT_TRUE(outsideLeg.has_value());
    editor.disconnectCable(*outsideLeg);

    EXPECT_TRUE(editor.getMacros().find(macroId)->ports.empty()) << "the inlet nothing feeds is gone";
    EXPECT_EQ(engine.getGraph().getConnections().size(), connectionsWithPort - 2) << "both legs are gone";
    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u) << "one Cmd+Z brings the port back";
    EXPECT_EQ(engine.getGraph().getConnections().size(), connectionsWithPort);
}

TEST(MacroDragLiveReroute, WithAutoDeleteOffCuttingTheOutsideLegKeepsThePort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2400, 1400);
    editor.setAutoDeleteMacroPortsOnLastCableEnabled(false);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 600);
    auto src = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1000, 150);
    editor.connectPorts(src, 0, b, 0, /*isMidi=*/false);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro(/*autoCreatePorts=*/true);
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);

    for (const auto& c : editor.buildVisibleCables())
        if (c.id.srcUid == src.uid)
            editor.disconnectCable(c);

    EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u) << "the preference keeps hand-made ports";
}

// After a drag out, the modulation runs LFO -> attenuverter -> inlet -> knob. Cutting it from the LFO
// side (its cable or its jack) still takes the inlet and the inside half.
TEST(MacroDragLiveReroute, AModulationReroutedByADragOutIsFullyRemovedFromTheLfoSide) {
    for (const bool viaJack : {false, true}) {
        SCOPED_TRACE(viaJack ? "jack" : "cable");
        AudioEngine engine;
        AppUndoManager undo;
        GraphEditor editor(engine, &undo);
        undo.setGraphEditor(&editor);
        editor.setSize(2400, 1400);
        auto member = addModuleAt(editor, engine, std::make_unique<WavetableOscillatorModule>(), 100, 100);
        auto filler = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 600);
        auto lfo = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 1000, 150);
        const auto baseNodes = engine.getGraph().getNumNodes();
        // Connected while everything sits outside, then grouped: the modulation crosses through an inlet.
        auto* memberComp = findComponent(editor, member);
        juce::Slider* position = nullptr;
        for (auto* child : memberComp->getChildren())
            if (auto* sl = dynamic_cast<juce::Slider*>(child); sl != nullptr && sl->getComponentID() == "Position")
                position = sl;
        ASSERT_NE(position, nullptr);
        editor.beginConnectionDrag(findComponent(editor, lfo), 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
        editor.endConnectionDrag(memberComp->getBounds().getPosition() + position->getBounds().getCentre());
        editor.setSelectedNodes({member, filler});
        const auto macroId = editor.getMacroController().groupSelectionIntoMacro(/*autoCreatePorts=*/true);
        editor.getMacroController().setMacroCollapsed(macroId, false);
        editor.setSelectedNodes({});
        ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);

        if (viaJack) {
            editor.disconnectPort(findComponent(editor, lfo), 0, /*isInput=*/false, /*isMidi=*/false);
        } else {
            std::optional<GraphEditor::VisibleCable> lfoCable;
            for (const auto& c : editor.buildVisibleCables())
                if (c.id.srcUid == lfo.uid)
                    lfoCable = c;
            ASSERT_TRUE(lfoCable.has_value());
            editor.disconnectCable(*lfoCable);
        }

        EXPECT_TRUE(editor.getMacros().find(macroId)->ports.empty()) << "the inlet goes";
        EXPECT_TRUE(engine.getModulationRoutings().empty()) << "the modulation goes";
        EXPECT_EQ(engine.getGraph().getNumNodes(), baseNodes) << "no port or attenuverter left";
        ASSERT_TRUE(undo.undo());
        EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u) << "one Cmd+Z brings it all back";
        EXPECT_FALSE(engine.getModulationRoutings().empty());
    }
}
