#include "AudioEngine/AudioEngine.h"
#include "MacroPortFlowTestHelpers.h"

// Topic: removing a modulation from the canvas when it crosses a macro boundary. An LFO outside an
// expanded macro modulating a knob of a member is wired LFO -> inlet -> hidden attenuverter -> knob
// (built here by a real cable drag); every canvas removal path (right-click Disconnect Cable,
// double-clicking the cable's amount knob, Disconnect on the knob's jack) takes the whole chain
// with it, inlet included, as ONE undo step (docs/macros/auto-ports.md#removing-a-modulation-takes-its-whole-chain).

namespace {

juce::MouseEvent doubleClickAt(juce::Component& comp, juce::Point<int> pos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos.toFloat(), juce::Time::getCurrentTime(), 2,
                            false);
}

// An LFO and a spare Wavetable outside, a Wavetable + Filter boxed into an expanded macro.
struct OutsideLfoRig {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID lfo, member, spareTarget;
    juce::String macroId;
    size_t baseConnections = 0, baseNodes = 0;

    OutsideLfoRig() {
        undo.setGraphEditor(&editor);
        editor.setSize(1600, 1200);
        member = addModuleAt(editor, engine, std::make_unique<WavetableOscillatorModule>(), 100, 100);
        const auto filler = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 500);
        editor.setSelectedNodes({member, filler});
        macroId = editor.getMacroController().groupSelectionIntoMacro();
        editor.getMacroController().setMacroCollapsed(macroId, false);
        lfo = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 900, 100);
        spareTarget = addModuleAt(editor, engine, std::make_unique<WavetableOscillatorModule>(), 900, 500);
        baseConnections = engine.getGraph().getConnections().size();
        baseNodes = (size_t)engine.getGraph().getNumNodes();
    }

    // A real drag from the LFO's output onto `target`'s Position knob.
    void dragLfoOntoPositionKnobOf(NodeID target) {
        auto* targetComp = compFor(editor, target);
        ASSERT_NE(targetComp, nullptr);
        juce::Slider* position = nullptr;
        for (auto* child : targetComp->getChildren())
            if (auto* s = dynamic_cast<juce::Slider*>(child))
                if (s->getComponentID() == "Position")
                    position = s;
        ASSERT_NE(position, nullptr);
        editor.beginConnectionDrag(compFor(editor, lfo), 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
        editor.endConnectionDrag(targetComp->getBounds().getPosition() + position->getBounds().getCentre());
    }

    size_t macroPorts() { return editor.getMacros().find(macroId)->ports.size(); }
    size_t connections() { return engine.getGraph().getConnections().size(); }
    size_t nodes() { return (size_t)engine.getGraph().getNumNodes(); }

    size_t edgesFromLfo() {
        size_t n = 0;
        for (const auto& c : engine.getGraph().getConnections())
            if (c.source.nodeID == lfo)
                ++n;
        return n;
    }

    bool routesInto(NodeID target) {
        for (const auto& r : engine.getModulationRoutings())
            if (r.hasDest && r.destNodeID == target)
                return true;
        return false;
    }

    std::optional<GraphEditor::VisibleCable> chainCableInto(NodeID target) {
        for (const auto& c : editor.buildVisibleCables())
            if (c.kind == GraphEditor::VisibleCable::Kind::AttenuverterChain && c.id.dstUid == target.uid)
                return c;
        return std::nullopt;
    }

    // The chain is LFO -> inlet -> attenuverter -> knob.
    void expectChainThroughAnInlet() {
        ASSERT_EQ(macroPorts(), 1u);
        ASSERT_EQ(nodes(), baseNodes + 2) << "one inlet, one attenuverter";
        ASSERT_EQ(edgesFromLfo(), 1u) << "LFO -> inlet";
    }

    void expectNothingLeftBehind() {
        EXPECT_EQ(macroPorts(), 0u) << "the inlet is swept";
        EXPECT_EQ(edgesFromLfo(), 0u) << "no LFO -> port cable left";
        EXPECT_EQ(connections(), baseConnections);
        EXPECT_EQ(nodes(), baseNodes);
    }

    void expectUndoRestoresTheChain() {
        ASSERT_TRUE(undo.undo());
        expectChainThroughAnInlet();
        EXPECT_TRUE(routesInto(member)) << "one Cmd+Z brings back the whole chain";
    }
};

} // namespace

// Regression test for FRO512: right-click Disconnect Cable left LFO -> inlet behind.
TEST(MacroPortFlowModulationRemoval, DisconnectingTheCableClearsTheInletToo) {
    OutsideLfoRig r;
    r.dragLfoOntoPositionKnobOf(r.member);
    r.expectChainThroughAnInlet();
    const auto cable = r.chainCableInto(r.member);
    ASSERT_TRUE(cable.has_value());

    r.editor.disconnectCable(*cable);

    r.expectNothingLeftBehind();
    r.expectUndoRestoresTheChain();
}

TEST(MacroPortFlowModulationRemoval, DisconnectingTheCableClearsTheInletEvenWithAutoDeleteOff) {
    OutsideLfoRig r;
    r.editor.setAutoDeleteMacroPortsOnLastCableEnabled(false);
    r.dragLfoOntoPositionKnobOf(r.member);
    r.expectChainThroughAnInlet();
    const auto cable = r.chainCableInto(r.member);
    ASSERT_TRUE(cable.has_value());

    r.editor.disconnectCable(*cable);

    r.expectNothingLeftBehind();
    r.expectUndoRestoresTheChain();
}

// Regression test for FRO512: double-clicking the amount knob only cut the attenuverter.
TEST(MacroPortFlowModulationRemoval, DoubleClickingTheAmountKnobClearsTheInletToo) {
    OutsideLfoRig r;
    r.dragLfoOntoPositionKnobOf(r.member);
    r.expectChainThroughAnInlet();
    const auto cable = r.chainCableInto(r.member);
    ASSERT_TRUE(cable.has_value());
    const auto mid = ((cable->p1 + cable->p2) / 2.0f).roundToInt();
    auto* content = r.editor.getChildComponent(0);
    ASSERT_NE(content, nullptr);

    r.editor.mouseDoubleClick(doubleClickAt(r.editor, r.editor.getLocalPoint(content, mid)));

    r.expectNothingLeftBehind();
    r.expectUndoRestoresTheChain();
}

// Regression test for FRO512: Disconnect on the knob's own jack removed the chain but kept the inlet.
TEST(MacroPortFlowModulationRemoval, DisconnectingTheKnobJackClearsTheInletToo) {
    OutsideLfoRig r;
    r.dragLfoOntoPositionKnobOf(r.member);
    r.expectChainThroughAnInlet();
    const auto routing = r.engine.getModulationRoutings();
    ASSERT_FALSE(routing.empty());
    auto* memberComp = compFor(r.editor, r.member);
    auto* module = dynamic_cast<ModuleBase*>(memberComp->getModule());
    ASSERT_NE(module, nullptr);
    const int jack = module->mapInputChannel(routing.front().destChannelIndex).visibleJackIndex;

    r.editor.disconnectPort(memberComp, jack, /*isInput=*/true, /*isMidi=*/false);

    r.expectNothingLeftBehind();
    r.expectUndoRestoresTheChain();
}

TEST(MacroPortFlowModulationRemoval, AnotherChainFromTheSameLfoSurvives) {
    OutsideLfoRig r;
    r.dragLfoOntoPositionKnobOf(r.member);
    r.dragLfoOntoPositionKnobOf(r.spareTarget);
    ASSERT_TRUE(r.routesInto(r.member));
    ASSERT_TRUE(r.routesInto(r.spareTarget));
    const auto cable = r.chainCableInto(r.member);
    ASSERT_TRUE(cable.has_value());

    r.editor.disconnectCable(*cable);

    EXPECT_EQ(r.macroPorts(), 0u);
    EXPECT_FALSE(r.routesInto(r.member));
    EXPECT_TRUE(r.routesInto(r.spareTarget)) << "the LFO's other modulation is untouched";
    EXPECT_EQ(r.edgesFromLfo(), 1u) << "LFO -> its remaining attenuverter";
    EXPECT_EQ(r.nodes(), r.baseNodes + 1) << "just the other chain's attenuverter";
}
