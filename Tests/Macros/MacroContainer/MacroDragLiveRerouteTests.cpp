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

namespace {

// One gesture with several drag ticks: each entry in `centres` is where the card's centre is moved to, and
// `atEach` runs after that tick (before the next one, or the release).
void dragThrough(ModuleComponent& comp, const std::vector<juce::Point<int>>& centres,
                 const std::function<void(size_t)>& atEach) {
    const juce::Point<int> pressPos(comp.getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    const auto startCentre = comp.getBounds().getCentre();
    comp.mouseDown(realMouseEvent(comp, pressPos, pressPos, kPlainClick));
    juce::Point<int> pos = pressPos;
    for (size_t i = 0; i < centres.size(); ++i) {
        // ComponentDragger adds (position - press point) to the card's CURRENT bounds each tick.
        const auto step = centres[i] - comp.getBounds().getCentre();
        pos = pressPos + step;
        comp.mouseDrag(realMouseEvent(comp, pos, pressPos, kPlainClick, /*wasDragged=*/true));
        atEach(i);
    }
    juce::ignoreUnused(startCentre);
    comp.mouseUp(realMouseEvent(comp, pressPos, pressPos, kPlainClick, /*wasDragged=*/true));
}

struct PortedLfoRig {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID a, b, lfo;
    juce::String macroId;

    PortedLfoRig() {
        undo.setGraphEditor(&editor);
        editor.setSize(2400, 1400);
        a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
        b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 600);
        lfo = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 1000, 150);
        editor.connectPorts(lfo, 0, b, 0, /*isMidi=*/false);
        editor.setSelectedNodes({a, b});
        macroId = editor.getMacroController().groupSelectionIntoMacro(/*autoCreatePorts=*/true);
        editor.getMacroController().setMacroCollapsed(macroId, false);
        editor.setSelectedNodes({});
    }
    bool inside() { return editor.getMacroController().macroForNode(lfo) != nullptr; }
    size_t ports() { return editor.getMacros().find(macroId)->ports.size(); }
    // Empty canvas right of the Oscillator, inside the border the two members span.
    juce::Point<int> insideSpot() {
        const auto hull = editor.getMacroController().macroHullBounds(macroId);
        return {hull.getRight() - 160, hull.getY() + 250};
    }
};

} // namespace

// Regression test for FRO563: the cables re-routed only on the drop, so the port and its name stayed on the
// border while the module sat inside, and everything jumped on release.
TEST(MacroDragLiveReroute, CablesRerouteAsTheCardCrossesNotOnTheDrop) {
    PortedLfoRig r;
    ASSERT_EQ(r.ports(), 1u);
    const auto outside = findComponent(r.editor, r.lfo)->getBounds().getCentre();
    const auto in = r.insideSpot();

    dragThrough(*findComponent(r.editor, r.lfo), {in, outside, in}, [&](size_t tick) {
        const bool shouldBeInside = tick != 1;
        EXPECT_EQ(r.inside(), shouldBeInside) << "tick " << tick;
        EXPECT_EQ(r.ports(), shouldBeInside ? 0u : 1u) << "the port goes and comes back as it crosses, tick " << tick;
        EXPECT_EQ(hasDirectEdge(r.engine, r.lfo, r.b), shouldBeInside) << "tick " << tick;
        EXPECT_TRUE(r.editor.hasMacroDragCandidate() == shouldBeInside) << "tick " << tick;
    });

    EXPECT_TRUE(r.inside()) << "dropped where it was last carried: inside";
    EXPECT_EQ(r.ports(), 0u);
}

TEST(MacroDragLiveReroute, OneUndoPutsBackTheWholeGestureHoweverOftenItCrossed) {
    PortedLfoRig r;
    const auto uuid = uuidOf(r.engine, r.lfo);
    auto* node = r.engine.getGraph().getNodeForId(r.lfo);
    const int x0 = node->properties["x"], y0 = node->properties["y"];
    const auto outside = findComponent(r.editor, r.lfo)->getBounds().getCentre();
    const auto in = r.insideSpot();
    const int serial = r.undo.getEditSerial();

    dragThrough(*findComponent(r.editor, r.lfo), {in, outside + juce::Point<int>(0, 200), in}, [](size_t) {});
    ASSERT_TRUE(r.inside());
    EXPECT_EQ(r.undo.getEditSerial(), serial + 1) << "the gesture is one undo step";

    ASSERT_TRUE(r.undo.undo());
    const auto lfoAfter = nodeIdForUuid(r.engine, uuid);
    ASSERT_NE(lfoAfter.uid, 0u);
    EXPECT_EQ(r.editor.getMacros().findByMember(uuid), nullptr) << "back outside";
    EXPECT_EQ(r.ports(), 1u) << "with its port";
    EXPECT_EQ((int)r.engine.getGraph().getNodeForId(lfoAfter)->properties["x"], x0) << "where it was pressed";
    EXPECT_EQ((int)r.engine.getGraph().getNodeForId(lfoAfter)->properties["y"], y0);
}

TEST(MacroDragLiveReroute, TheBorderGlidesToItsNewSizeAndThePortWidgetsGoWithIt) {
    PortedLfoRig r;
    const auto hullBefore = r.editor.paintedMacroHullBounds(r.macroId);
    const auto outside = findComponent(r.editor, r.lfo)->getBounds().getCentre();
    const auto farOut = outside + juce::Point<int>(300, 0);

    dragThrough(*findComponent(r.editor, r.lfo), {r.insideSpot(), farOut}, [&](size_t tick) {
        if (tick != 0)
            return;
        ASSERT_TRUE(r.inside());
        EXPECT_TRUE(r.editor.isHullGlideLiveForTest()) << "joining moved the border, so it glides";
        EXPECT_EQ(r.editor.paintedMacroHullBounds(r.macroId), hullBefore)
            << "at the start of the glide the border is drawn where it was";
        r.editor.finishHullGlideForTest();
        EXPECT_NE(r.editor.paintedMacroHullBounds(r.macroId), hullBefore) << "and it settles around the newcomer";
    });
    ASSERT_FALSE(r.inside());
    ASSERT_EQ(r.ports(), 1u);
    r.editor.finishHullGlideForTest();
    // The port widget docks against the border that is drawn, so it slides with it.
    const auto& port = r.editor.getMacros().find(r.macroId)->ports.front();
    auto* portComp = findComponent(r.editor, nodeIdForUuid(r.engine, port.nodeUuid));
    ASSERT_NE(portComp, nullptr);
    const auto painted = r.editor.paintedMacroHullBounds(r.macroId);
    EXPECT_LE(std::abs(portComp->getX() - painted.getX()), 60) << "the inlet sits on the drawn border's left side";
}

// A macro at the canvas's left edge that gains an input port slides into view, but not under the pointer: during
// the drag the members hold still, and the slide happens on the drop, in the same undo step.
TEST(MacroDragLiveReroute, AMacroAtTheCanvasEdgeMakesRoomOnTheDropNotUnderThePointer) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(2400, 1400);
    auto lfo = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 0, 40);
    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 400, 40);
    editor.connectPorts(lfo, 0, osc, 0, /*isMidi=*/false);
    editor.setSelectedNodes({lfo, osc});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro(/*autoCreatePorts=*/true);
    editor.getMacroController().setMacroCollapsed(macroId, false);
    editor.setSelectedNodes({});
    ASSERT_TRUE(editor.getMacros().find(macroId)->ports.empty());
    const auto oscAtPress = findComponent(editor, osc)->getPosition();

    auto* compLfo = findComponent(editor, lfo);
    dragBodyBy(*compLfo, {0, 900}, kPlainClick, [&] {
        ASSERT_EQ(editor.getMacroController().macroForNode(lfo), nullptr) << "sanity: the LFO left as it crossed";
        EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u) << "sanity: its input port is minted live";
        EXPECT_EQ(findComponent(editor, osc)->getPosition(), oscAtPress) << "nothing in the macro moves mid-drag";
    });

    editor.finishHullGlideForTest();
    EXPECT_GE(editor.getMacroController().macroHullBounds(macroId).getX(), synth::LayoutUtil::kMacroPortOverhang)
        << "on the drop the macro slides far enough in for its new input port to be on the canvas";
    ASSERT_TRUE(undo.undo());
    EXPECT_NE(editor.getMacroController().macroForNode(lfo), nullptr) << "one Cmd+Z undoes the drag and the slide";
}
