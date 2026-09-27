#include "AudioEngine/AudioEngine.h"
#include "MacroPortFlowTestHelpers.h"

// Topic: FRO234 — createMacroPortFromDroppedCable (a cable dropped on a collapsed card) and
// maybeAutoCreateMacroPortsForDrag (a cable dragged across an expanded hull) infer a new macro
// port's shape from the dragged cable's OWN jack fan (mirroring resolvePolyLink/getJackTargets)
// instead of always Mono. A single dragged jack can only ever produce Mono, StereoCollapsed (one
// jack, two raw legs) or Poly — never the two-SEPARATE-jack Stereo shape, which needs two
// independent legs no single cable carries.

namespace {

// A collapsed-stereo source: ONE visible jack fronting two raw legs (span 2, Audio role) — the
// same shape a Dual-I/O-off FX module (Delay, Reverb) presents, isolated here from any of a real
// FX module's other quirks, mirroring TestPolyCVModule's own isolation rationale in
// MacroAutoPortTestHelpers.h.
class TestStereoCollapsedModule : public ModuleBase {
public:
    TestStereoCollapsedModule()
        : ModuleBase("TestStereoCollapsed", 2, 2) {}
    void prepareToPlay(double, int) override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::Math; }
    int getVisibleInputPortCount() const override { return 1; }
    int getVisibleOutputPortCount() const override { return 1; }
    LogicalPort mapInputChannel(int raw) const override { return fan(raw); }
    LogicalPort mapOutputChannel(int raw) const override { return fan(raw); }

private:
    static LogicalPort fan(int raw) {
        LogicalPort p;
        if (raw == 0 || raw == 1) {
            p.visibleJackIndex = 0;
            p.role = PortRole::Audio;
            p.isPolyGroupHead = (raw == 0);
            p.polyVoiceSpan = 2;
        }
        return p;
    }
};

// A single Poly-8 ModCV bus — mirrors MacroAutoPortTestHelpers.h's TestPolyCVModule (kept as its
// own local copy: that header is scoped to the MacroAutoPort test suite, not shared across areas).
class TestPolyCVModule : public ModuleBase {
public:
    TestPolyCVModule()
        : ModuleBase("TestPolyCV", 8, 8) {}
    void prepareToPlay(double, int) override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::Math; }
    int getVisibleInputPortCount() const override { return 1; }
    int getVisibleOutputPortCount() const override { return 1; }
    LogicalPort mapInputChannel(int raw) const override { return fan(raw); }
    LogicalPort mapOutputChannel(int raw) const override { return fan(raw); }

private:
    static LogicalPort fan(int raw) {
        LogicalPort p;
        if (raw >= 0 && raw < 8) {
            p.visibleJackIndex = 0;
            p.role = PortRole::ModCV;
            p.isPolyGroupHead = (raw == 0);
            p.polyVoiceSpan = 8;
        }
        return p;
    }
};

} // namespace

// ============================================================================
// createMacroPortFromDroppedCable (collapsed-card drop)
// ============================================================================

TEST(MacroPortFlow, CableDropFromAMonoSourceStillCreatesAMonoPortRegressionGuard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    auto extOsc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    auto* extComp = compFor(editor, extOsc);
    ASSERT_NE(extComp, nullptr);
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    editor.beginConnectionDrag(extComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(card->getBounds().getCentre());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u);
    const auto portId = nodeIdForUuid(engine, macro->ports[0].nodeUuid);
    auto* inlet = dynamic_cast<MacroInletModule*>(engine.getGraph().getNodeForId(portId)->getProcessor());
    ASSERT_NE(inlet, nullptr);
    EXPECT_EQ(inlet->getPortShape(), MacroPortShape::Mono);
}

TEST(MacroPortFlow, CableDropFromAStereoCollapsedSourceCreatesAStereoCollapsedPortAndWiresBothLegs) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    auto extStereo = addModuleAt(editor, engine, std::make_unique<TestStereoCollapsedModule>(), 900, 900);
    auto* extComp = compFor(editor, extStereo);
    ASSERT_NE(extComp, nullptr);
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    editor.beginConnectionDrag(extComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(card->getBounds().getCentre());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u);
    const auto portId = nodeIdForUuid(engine, macro->ports[0].nodeUuid);
    auto* inlet = dynamic_cast<MacroInletModule*>(engine.getGraph().getNodeForId(portId)->getProcessor());
    ASSERT_NE(inlet, nullptr);
    EXPECT_EQ(inlet->getPortShape(), MacroPortShape::StereoCollapsed)
        << "one visible jack, two raw legs on the source -> the new port must infer the same shape";

    // The one visible jack (index 0) fans across both raw legs on each side -- connectPorts'
    // own resolvePolyLink read of the NEW port's now-correct shape does this automatically.
    EXPECT_TRUE(hasConnection(engine, extStereo, 0, portId, 0));
    EXPECT_TRUE(hasConnection(engine, extStereo, 1, portId, 1));
}

TEST(MacroPortFlow, CableDropFromAPolyCvSourceCreatesAPolyPortWithTheMatchingVoiceCount) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    auto extPoly = addModuleAt(editor, engine, std::make_unique<TestPolyCVModule>(), 900, 900);
    auto* extComp = compFor(editor, extPoly);
    ASSERT_NE(extComp, nullptr);
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    editor.beginConnectionDrag(extComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(card->getBounds().getCentre());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u);
    const auto portId = nodeIdForUuid(engine, macro->ports[0].nodeUuid);
    auto* inlet = dynamic_cast<MacroInletModule*>(engine.getGraph().getNodeForId(portId)->getProcessor());
    ASSERT_NE(inlet, nullptr);
    EXPECT_EQ(inlet->getPortShape(), MacroPortShape::Poly);
    EXPECT_EQ(inlet->getVoiceCount(), 8);

    for (int v = 0; v < 8; ++v)
        EXPECT_TRUE(hasConnection(engine, extPoly, v, portId, v)) << "voice " << v << " must be wired";
}

// ============================================================================
// maybeAutoCreateMacroPortsForDrag (drag across an EXPANDED macro's hull, jack-to-jack)
// ============================================================================

TEST(MacroPortFlow, DragAcrossHullFromAStereoCollapsedMemberCreatesAStereoCollapsedPort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto stereoMember = addModuleAt(editor, engine, std::make_unique<TestStereoCollapsedModule>(), 100, 100);
    auto spare = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 400);
    editor.setSelectedNodes({stereoMember, spare});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    auto* memberComp = compFor(editor, stereoMember);
    ASSERT_NE(memberComp, nullptr);
    auto extStereo = addModuleAt(editor, engine, std::make_unique<TestStereoCollapsedModule>(), 900, 100);
    auto* extComp = compFor(editor, extStereo);
    ASSERT_NE(extComp, nullptr);

    editor.beginConnectionDrag(memberComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(extComp->getBounds().getPosition() + extComp->getPortCenter(0, /*isInput=*/true));

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_FALSE(macro->ports[0].isInput);
    const auto portId = nodeIdForUuid(engine, macro->ports[0].nodeUuid);
    auto* outlet = dynamic_cast<MacroOutletModule*>(engine.getGraph().getNodeForId(portId)->getProcessor());
    ASSERT_NE(outlet, nullptr);
    EXPECT_EQ(outlet->getPortShape(), MacroPortShape::StereoCollapsed)
        << "the internal member's own collapsed-stereo jack must be mirrored onto the boundary port";

    EXPECT_TRUE(hasConnection(engine, stereoMember, 0, portId, 0));
    EXPECT_TRUE(hasConnection(engine, stereoMember, 1, portId, 1));
    EXPECT_TRUE(hasConnection(engine, portId, 0, extStereo, 0));
    EXPECT_TRUE(hasConnection(engine, portId, 1, extStereo, 1));
}

TEST(MacroPortFlow, DragAcrossHullFromAPolyCvMemberCreatesAPolyPort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto polyMember = addModuleAt(editor, engine, std::make_unique<TestPolyCVModule>(), 100, 100);
    auto spare = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 400);
    editor.setSelectedNodes({polyMember, spare});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    auto* memberComp = compFor(editor, polyMember);
    ASSERT_NE(memberComp, nullptr);
    auto extPoly = addModuleAt(editor, engine, std::make_unique<TestPolyCVModule>(), 900, 100);
    auto* extComp = compFor(editor, extPoly);
    ASSERT_NE(extComp, nullptr);

    editor.beginConnectionDrag(memberComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(extComp->getBounds().getPosition() + extComp->getPortCenter(0, /*isInput=*/true));

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u);
    const auto portId = nodeIdForUuid(engine, macro->ports[0].nodeUuid);
    auto* outlet = dynamic_cast<MacroOutletModule*>(engine.getGraph().getNodeForId(portId)->getProcessor());
    ASSERT_NE(outlet, nullptr);
    EXPECT_EQ(outlet->getPortShape(), MacroPortShape::Poly);
    EXPECT_EQ(outlet->getVoiceCount(), 8);

    for (int v = 0; v < 8; ++v)
        EXPECT_TRUE(hasConnection(engine, portId, v, extPoly, v)) << "voice " << v << " must be wired";
}
