// GraphEditor poly-link tests: pure resolvePolyLink pairing/scoring (no graph needed), plus
// Dual I/O toggle tests for keeping/completing both stereo legs.
// Shared GraphEditorTest fixture and helpers live in GraphEditorTestHelpers.h.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"

#include "Modules/ADSRModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Modules/PolyMidiModule.h"
#include "Modules/VCAModule.h"

// ============================================================================
// Issue #163: poly connections auto-fan-out
// ============================================================================

// ---- Pure resolvePolyLink tests (no graph needed) ----

TEST_F(GraphEditorTest, ResolvePolyLinkFansEnvelopeToPolyVCA) {
    ADSRModule adsr;
    VCAModule vca;
    setPolyParam(adsr, true);
    setPolyParam(vca, true);

    auto link = GraphEditor::resolvePolyLink(&adsr, 0, &vca, 2); // VCA CV moved to jack 2 behind Audio L/R (#219)
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 8);
    EXPECT_EQ(link.voiceCount, 8);
}

TEST_F(GraphEditorTest, ResolvePolyLinkPicksFanMatchingRole) {
    PolyMidiModule polyMidi;
    OscillatorModule osc;
    ADSRModule adsr;
    setPolyParam(osc, true);
    setPolyParam(adsr, true);

    // PolyMidi's "Poly Out" jack fronts both a Pitch fan (raw 0) and a Gate fan (raw 8).
    // Into the Oscillator's Pitch input, the Pitch fan must win.
    auto linkToOsc = GraphEditor::resolvePolyLink(&polyMidi, 0, &osc, 0);
    EXPECT_EQ(linkToOsc.sourceRawChannel, 0);
    EXPECT_EQ(linkToOsc.destRawChannel, 0);
    EXPECT_EQ(linkToOsc.voiceCount, 8);

    // Into the ADSR's Gate input (same source visible jack), the Gate fan must win instead.
    auto linkToAdsr = GraphEditor::resolvePolyLink(&polyMidi, 0, &adsr, 0);
    EXPECT_EQ(linkToAdsr.sourceRawChannel, 8);
    EXPECT_EQ(linkToAdsr.destRawChannel, 0);
    EXPECT_EQ(linkToAdsr.voiceCount, 8);
}

TEST_F(GraphEditorTest, ResolvePolyLinkStaysMonoWhenDestIsMono) {
    ADSRModule polyAdsr;
    setPolyParam(polyAdsr, true);
    VCAModule monoVca; // poly defaults to false

    // A poly source into a mono jack must not sum eight envelopes onto one CV channel.
    auto link = GraphEditor::resolvePolyLink(&polyAdsr, 0, &monoVca, 2); // CV jack (#219)
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 1);
    EXPECT_EQ(link.voiceCount, 1);
    EXPECT_EQ(link.sourceStride, 1);
}

TEST_F(GraphEditorTest, ResolvePolyLinkBroadcastsMonoSourceAcrossModCvFan) {
    // One mono modulator on a per-voice mod-CV fan drives every voice: all eight wires leave the
    // same source channel (sourceStride == 0) and land on VCA raw channels 8-15.
    LFOModule lfo;
    VCAModule polyVca;
    setPolyParam(polyVca, true);

    auto link = GraphEditor::resolvePolyLink(&lfo, 0, &polyVca, 2); // CV jack (#219)
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 8);
    EXPECT_EQ(link.voiceCount, 8);
    EXPECT_EQ(link.sourceStride, 0);
}

TEST_F(GraphEditorTest, ResolvePolyLinkBroadcastsMonoIntoCollapsedStereoPair) {
    // Collapsed Dual I/O (voiceSpan 2, PortRole::Audio) is a stereo bus, not a poly voice fan —
    // mono sources duplicate onto L and R so a single cable feeds both FX legs.
    OscillatorModule osc;
    DelayModule delay; // Dual I/O defaults off → one Audio jack spanning raw 0/1

    auto link = GraphEditor::resolvePolyLink(&osc, 0, &delay, 0);
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 0);
    EXPECT_EQ(link.voiceCount, 2);
    EXPECT_EQ(link.sourceStride, 0);
}

TEST_F(GraphEditorTest, TogglingDualIOKeepsBothStereoLegs) {
    // Dual I/O only changes jack visibility. A collapsed Audio cable fans onto raw ch0 and ch1;
    // flipping Dual I/O on must not drop the right leg.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto delayNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    editor.updateComponents();

    ModuleComponent* oscComp = nullptr;
    ModuleComponent* delayComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
                if (mod->getModule() == delayNode->getProcessor())
                    delayComp = mod;
            }
        }
    }
    ASSERT_NE(oscComp, nullptr);
    ASSERT_NE(delayComp, nullptr);

    oscComp->setBounds(0, 0, 100, 100);
    delayComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(oscComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(delayComp->getBounds().getPosition() + delayComp->getPortCenter(0, true));

    auto& graph = engine.getGraph();
    auto hasEdge = [&](int srcCh, int dstCh) {
        for (auto& conn : graph.getConnections())
            if (conn.source.nodeID == oscNode->nodeID && conn.source.channelIndex == srcCh &&
                conn.destination.nodeID == delayNode->nodeID && conn.destination.channelIndex == dstCh)
                return true;
        return false;
    };
    ASSERT_TRUE(hasEdge(0, 0));
    ASSERT_TRUE(hasEdge(0, 1)) << "collapsed Audio jack must fan onto Delay Right before the toggle";

    setDualIOParam(*delayNode->getProcessor(), true);

    EXPECT_TRUE(hasEdge(0, 0)) << "Left leg must survive Dual I/O on";
    // The right leg is still fed — but by the Oscillator's own Audio R block, not by a second copy
    // of Audio L. The collapsed jack's mono duplicate has to go with it: keeping both would sum
    // L+R into the Delay's Right input (+6 dB on one side) behind what draws as a single cable.
    //
    // RECONCILED with the later ruling that a module split while wired must come up with both legs
    // live (SplittingAMidChainVoiceModule*): that ruling introduced a mono BROADCAST, and this test
    // is not in tension with it. A broadcast is the fallback for a peer that has no right leg at
    // all; here the Oscillator upstream is dual and owns a real Audio R, so the real leg wins and
    // the stand-in copy is removed. The two rules are one rule: prefer the peer's right leg, copy
    // the left only when there is none. Pinned from the other direction by
    // SplittingAMidChainVoiceModulePrefersRealRightLegsOverABroadcast.
    EXPECT_TRUE(hasEdge(OscillatorModule::kRightBase, 1)) << "Right leg must survive Dual I/O on";
    EXPECT_FALSE(hasEdge(0, 1)) << "the collapsed jack's duplicate of Audio L must not double-feed Right";

    setDualIOParam(*delayNode->getProcessor(), false);

    EXPECT_TRUE(hasEdge(0, 0));
    EXPECT_TRUE(hasEdge(OscillatorModule::kRightBase, 1)) << "Right leg must survive Dual I/O off as well";
}

TEST_F(GraphEditorTest, TogglingDualIOCompletesStereoOutputPair) {
    // A collapsed Audio output dropped on a 2-channel dest (Audio Output, or Dual I/O) often
    // only records the left edge. Toggling Dual I/O on must add the matching right edge.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto srcNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    auto dstNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    editor.updateComponents();

    ModuleComponent* srcComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild))
                if (mod->getModule() == srcNode->getProcessor())
                    srcComp = mod;
        }
    }
    ASSERT_NE(srcComp, nullptr);

    auto& graph = engine.getGraph();
    ASSERT_TRUE(graph.addConnection({{srcNode->nodeID, 0}, {dstNode->nodeID, 0}}));
    {
        bool hasRightBefore = false;
        for (const auto& conn : graph.getConnections())
            if (conn.source.nodeID == srcNode->nodeID && conn.source.channelIndex == 1)
                hasRightBefore = true;
        ASSERT_FALSE(hasRightBefore);
    }

    setDualIOParam(*srcNode->getProcessor(), true);

    bool hasRight = false;
    for (const auto& conn : graph.getConnections())
        if (conn.source.nodeID == srcNode->nodeID && conn.source.channelIndex == 1 &&
            conn.destination.nodeID == dstNode->nodeID && conn.destination.channelIndex == 1)
            hasRight = true;
    EXPECT_TRUE(hasRight) << "Dual I/O on must pair Delay Right out to the dest Right in";
}

TEST_F(GraphEditorTest, DualIOOnSourceDrawsRightCableOntoCollapsedDest) {
    // Both Dual I/O off: one Audio→Audio cable, two raw edges. Splitting only the source must
    // still draw the Right output onto the dest's remaining Audio jack — not wait until the dest
    // is split too (raw ch1 used to be treated as hidden because dest visibleCount == 1).
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto srcNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    auto dstNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    editor.updateComponents();

    ModuleComponent* srcComp = nullptr;
    ModuleComponent* dstComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == srcNode->getProcessor())
                    srcComp = mod;
                if (mod->getModule() == dstNode->getProcessor())
                    dstComp = mod;
            }
        }
    }
    ASSERT_NE(srcComp, nullptr);
    ASSERT_NE(dstComp, nullptr);
    srcComp->setBounds(0, 0, 200, 200);
    dstComp->setBounds(300, 0, 200, 200);

    editor.beginConnectionDrag(srcComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(dstComp->getBounds().getPosition() + dstComp->getPortCenter(0, true));

    setDualIOParam(*srcNode->getProcessor(), true);
    ASSERT_FALSE(dynamic_cast<ModuleBase*>(dstNode->getProcessor())->isDualIO());

    bool drawnFromRight = false;
    for (const auto& cable : editor.buildVisibleCables())
        if (cable.id.srcUid == srcNode->nodeID.uid && cable.id.dstUid == dstNode->nodeID.uid && cable.id.srcPort == 1)
            drawnFromRight = true;
    EXPECT_TRUE(drawnFromRight) << "Right out must draw onto the dest Audio jack while dest Dual I/O is still off";
}

TEST_F(GraphEditorTest, DualIOOnDestDrawsRightCableFromCollapsedSource) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto srcNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    auto dstNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    editor.updateComponents();

    ModuleComponent* srcComp = nullptr;
    ModuleComponent* dstComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == srcNode->getProcessor())
                    srcComp = mod;
                if (mod->getModule() == dstNode->getProcessor())
                    dstComp = mod;
            }
        }
    }
    ASSERT_NE(srcComp, nullptr);
    ASSERT_NE(dstComp, nullptr);
    srcComp->setBounds(0, 0, 200, 200);
    dstComp->setBounds(300, 0, 200, 200);

    editor.beginConnectionDrag(srcComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(dstComp->getBounds().getPosition() + dstComp->getPortCenter(0, true));

    setDualIOParam(*dstNode->getProcessor(), true);
    ASSERT_FALSE(dynamic_cast<ModuleBase*>(srcNode->getProcessor())->isDualIO());

    bool drawnOntoRight = false;
    for (const auto& cable : editor.buildVisibleCables())
        if (cable.id.srcUid == srcNode->nodeID.uid && cable.id.dstUid == dstNode->nodeID.uid && cable.id.dstPort == 1)
            drawnOntoRight = true;
    EXPECT_TRUE(drawnOntoRight)
        << "Dest Right in must draw from the source Audio jack while source Dual I/O is still off";
}
