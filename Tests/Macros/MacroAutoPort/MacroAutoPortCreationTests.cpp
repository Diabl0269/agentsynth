// MacroAutoPortCreationTests.cpp
// Auto-creating macro ports on grouping (founder-review fix F5, docs/macros_implementation.md §7 item 6.1/6.2):
// mono crossing, jack dedup, collapsed-stereo, dual-I/O stereo merge, poly-N, MIDI-as-a-separate
// node, the mod-routing-knob splice (incl. the T144 crossing-knob geometry/drag tests) and
// grouping-is-one-undo-step. Shared test modules/helpers live in MacroAutoPortTestHelpers.h.
//
// Ungroup/presentation/modal-preference tests live in MacroAutoPortUngroupTests.cpp; the T148/T154
// auto-delete suite lives in MacroAutoPortDeleteTests.cpp.

#include "MacroAutoPortTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FilterModule.h"
#include "Modules/MacroInletModule.h"
#include "Modules/MacroMidiInletModule.h"
#include "Modules/MacroMidiOutletModule.h"
#include "Modules/MacroOutletModule.h"
#include "UI/Macros/MacroCardComponent.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"

// ============================================================================
// Preference off (LeaveCablesAsIs / plain default-false call) creates nothing
// ============================================================================

TEST(MacroAutoPort, DefaultCallCreatesNoPorts) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {a, 0}});

    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(); // autoCreatePorts defaults false
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->ports.empty());
    EXPECT_TRUE(hasConnection(engine, ext, 0, a, 0)) << "the crossing cable is untouched when ports aren't requested";
}

// ============================================================================
// Mono in + Mono out, one port each
// ============================================================================

TEST(MacroAutoPort, MonoCrossingCreatesAnInletAndAnOutlet) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto cin = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Cin", 100, 100);
    auto cout = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Cout", 700, 300);
    engine.getGraph().addConnection({{cin, 0}, {a, 0}});
    engine.getGraph().addConnection({{b, 0}, {cout, 0}});

    const auto before = allNodeIds(engine);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 2u);

    const synth::MacroPort* inPort = nullptr;
    const synth::MacroPort* outPort = nullptr;
    for (const auto& p : macro->ports)
        (p.isInput ? inPort : outPort) = &p;
    ASSERT_NE(inPort, nullptr);
    ASSERT_NE(outPort, nullptr);
    EXPECT_EQ(inPort->kind, synth::MacroPortKind::AudioCV);
    EXPECT_EQ(outPort->kind, synth::MacroPortKind::AudioCV);
    EXPECT_EQ(inPort->name, "A In 0"); // internal module + jack it fronts, not "Input"
    EXPECT_EQ(outPort->name, "B Out 0");

    NodeID inNode, outNode;
    for (auto* node : engine.getGraph().getNodes()) {
        if (node->properties["uuid"].toString() == inPort->nodeUuid)
            inNode = node->nodeID;
        if (node->properties["uuid"].toString() == outPort->nodeUuid)
            outNode = node->nodeID;
    }
    ASSERT_NE(dynamic_cast<MacroInletModule*>(engine.getGraph().getNodeForId(inNode)->getProcessor()), nullptr);
    ASSERT_NE(dynamic_cast<MacroOutletModule*>(engine.getGraph().getNodeForId(outNode)->getProcessor()), nullptr);
    auto* inletMb = dynamic_cast<MacroInletModule*>(engine.getGraph().getNodeForId(inNode)->getProcessor());
    EXPECT_EQ(inletMb->getPortShape(), MacroPortShape::Mono);
    EXPECT_EQ(inletMb->getVisibleInputPortCount(), 1); // no regression: a genuinely mono crossing stays one jack

    EXPECT_TRUE(hasConnection(engine, cin, 0, inNode, 0));
    EXPECT_TRUE(hasConnection(engine, inNode, 0, a, 0));
    EXPECT_FALSE(hasConnection(engine, cin, 0, a, 0)) << "the original cable was spliced, not left in place";

    EXPECT_TRUE(hasConnection(engine, b, 0, outNode, 0));
    EXPECT_TRUE(hasConnection(engine, outNode, 0, cout, 0));
    EXPECT_FALSE(hasConnection(engine, b, 0, cout, 0));

    EXPECT_EQ(engine.getGraph().getNodes().size(), before.size() + 2);
}

// ============================================================================
// De-duplication: two cables into the same internal destination jack share ONE port
// ============================================================================

TEST(MacroAutoPort, TwoCablesIntoTheSameInternalJackShareOnePort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto filter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), "Filter", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto ext1 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext1", 100, 60);
    auto ext2 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext2", 100, 160);
    // Both land on Filter's Cutoff CV input (raw channel 1) -- the SAME internal destination jack,
    // from two DIFFERENT external sources.
    engine.getGraph().addConnection({{ext1, 0}, {filter, 1}});
    engine.getGraph().addConnection({{ext2, 0}, {filter, 1}});

    const auto before = allNodeIds(engine);
    editor.setSelectedNodes({filter, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u) << "one port, not two, for the shared internal jack";

    const auto portNode = theOneNewPortNode(engine, before);
    EXPECT_TRUE(hasConnection(engine, ext1, 0, portNode, 0));
    EXPECT_TRUE(hasConnection(engine, ext2, 0, portNode, 0));
    EXPECT_TRUE(hasConnection(engine, portNode, 0, filter, 1));
    EXPECT_FALSE(hasConnection(engine, ext1, 0, filter, 1));
    EXPECT_FALSE(hasConnection(engine, ext2, 0, filter, 1));
}

// ============================================================================
// Collapsed stereo: a collapsed jack's own two-raw-channel span must produce a port
// presenting the SAME one visible jack the module does (founder-review fix G2) — never the
// two-jack MacroPortShape::Stereo a hand-picked Configure I/O choice means.
// ============================================================================

TEST(MacroAutoPort, CollapsedStereoOutputCrossingCreatesAOneJackStereoCollapsedOutlet) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto reverb = addModuleAt(editor, engine, std::make_unique<ReverbModule>(), "Reverb", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto extL = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "ExtL", 700, 60);
    auto extR = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "ExtR", 700, 160);
    // Reverb defaults to Dual I/O OFF (collapsed): raw ch0/ch1 are ONE "Audio" jack, span 2 — same
    // as the founder's screenshot (a single "Audio" jack on DELAY/REVERB either side).
    engine.getGraph().addConnection({{reverb, 0}, {extL, 0}});
    engine.getGraph().addConnection({{reverb, 1}, {extR, 0}});

    const auto before = allNodeIds(engine);
    editor.setSelectedNodes({reverb, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_FALSE(macro->ports[0].isInput);

    const auto portNode = theOneNewPortNode(engine, before);
    auto* outlet = dynamic_cast<MacroOutletModule*>(engine.getGraph().getNodeForId(portNode)->getProcessor());
    ASSERT_NE(outlet, nullptr);
    EXPECT_EQ(outlet->getPortShape(), MacroPortShape::StereoCollapsed);
    // The bug this fix closes: the port must present exactly as many VISIBLE jacks as the
    // internal jack it fronts (docs/macros_ports.md §5.3). Reverb's own jack is ONE jack; so must this be.
    EXPECT_EQ(outlet->getVisibleInputPortCount(), 1);
    EXPECT_EQ(outlet->getVisibleOutputPortCount(), 1);

    // ...while still carrying BOTH raw channels — a channel-dropping "fix" must fail this.
    EXPECT_TRUE(hasConnection(engine, reverb, 0, portNode, 0));
    EXPECT_TRUE(hasConnection(engine, portNode, 0, extL, 0));
    EXPECT_TRUE(hasConnection(engine, reverb, 1, portNode, 1));
    EXPECT_TRUE(hasConnection(engine, portNode, 1, extR, 0));
    EXPECT_FALSE(hasConnection(engine, reverb, 0, extL, 0));
    EXPECT_FALSE(hasConnection(engine, reverb, 1, extR, 0));
}

// Audio actually flows through the spliced port, both channels, so a channel-dropping "fix"
// cannot pass by only checking connection topology.
TEST(MacroAutoPort, CollapsedStereoOutletPassesBothChannelsOfAudio) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto reverb = addModuleAt(editor, engine, std::make_unique<ReverbModule>(), "Reverb", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto extL = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "ExtL", 700, 60);
    auto extR = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "ExtR", 700, 160);
    engine.getGraph().addConnection({{reverb, 0}, {extL, 0}});
    engine.getGraph().addConnection({{reverb, 1}, {extR, 0}});

    const auto before = allNodeIds(engine);
    editor.setSelectedNodes({reverb, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    juce::ignoreUnused(macroId);

    const auto portNode = theOneNewPortNode(engine, before);
    auto* outlet = dynamic_cast<MacroOutletModule*>(engine.getGraph().getNodeForId(portNode)->getProcessor());
    ASSERT_NE(outlet, nullptr);

    constexpr int kBlockSize = 64;
    outlet->prepareToPlay(48000.0, kBlockSize);
    juce::AudioBuffer<float> buffer(MacroOutletModule::kMaxChannels, kBlockSize);
    buffer.clear();
    for (int i = 0; i < kBlockSize; ++i) {
        buffer.getWritePointer(0)[i] = 0.4f;  // Left, from Reverb ch0
        buffer.getWritePointer(1)[i] = -0.6f; // Right, from Reverb ch1
    }
    juce::MidiBuffer midi;
    outlet->processBlock(buffer, midi);

    EXPECT_GT(buffer.getRMSLevel(0, 0, kBlockSize), 0.0f) << "left channel went silent through the spliced port";
    EXPECT_GT(buffer.getRMSLevel(1, 0, kBlockSize), 0.0f) << "right channel went silent through the spliced port";
    EXPECT_FLOAT_EQ(buffer.getReadPointer(0)[0], 0.4f);
    EXPECT_FLOAT_EQ(buffer.getReadPointer(1)[0], -0.6f);
}

// ============================================================================
// Stereo: a Dual-I/O-on module's separately-jacked Left/Right merge into one port
// ============================================================================

TEST(MacroAutoPort, SeparatelyJackedLeftRightCrossingsMergeIntoOneStereoInlet) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto filter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), "Filter", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto extL = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "ExtL", 100, 60);
    auto extR = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "ExtR", 100, 160);
    // FilterModule defaults to Dual I/O ON (split): Left = raw 0 (its own visible jack 0), Right =
    // raw kRightBase (its own visible jack 1) -- two SEPARATE jacks, from two DIFFERENT externals.
    engine.getGraph().addConnection({{extL, 0}, {filter, 0}});
    engine.getGraph().addConnection({{extR, 0}, {filter, FilterModule::kRightBase}});

    const auto before = allNodeIds(engine);
    editor.setSelectedNodes({filter, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u) << "Left+Right merge into one Stereo port, not two Mono ones";
    EXPECT_TRUE(macro->ports[0].isInput);

    const auto portNode = theOneNewPortNode(engine, before);
    auto* inlet = dynamic_cast<MacroInletModule*>(engine.getGraph().getNodeForId(portNode)->getProcessor());
    ASSERT_NE(inlet, nullptr);
    EXPECT_EQ(inlet->getPortShape(), MacroPortShape::Stereo);
    // Filter shows TWO jacks when Dual I/O is on, so the port must too — unlike the collapsed
    // (StereoCollapsed) case above, which is deliberately one jack for a different internal shape.
    EXPECT_EQ(inlet->getVisibleInputPortCount(), 2);
    EXPECT_EQ(inlet->getVisibleOutputPortCount(), 2);

    EXPECT_TRUE(hasConnection(engine, extL, 0, portNode, 0));
    EXPECT_TRUE(hasConnection(engine, portNode, 0, filter, 0));
    EXPECT_TRUE(hasConnection(engine, extR, 0, portNode, MacroInletModule::kRightBase));
    EXPECT_TRUE(hasConnection(engine, portNode, MacroInletModule::kRightBase, filter, FilterModule::kRightBase));
    EXPECT_FALSE(hasConnection(engine, extL, 0, filter, 0));
    EXPECT_FALSE(hasConnection(engine, extR, 0, filter, FilterModule::kRightBase));
}

// ============================================================================
// Poly-N: the right voice count, per-voice wiring preserved
// ============================================================================

TEST(MacroAutoPort, PolyCrossingCreatesAPolyInletWithTheRightVoiceCount) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto poly = addModuleAt(editor, engine, std::make_unique<TestPolyCVModule>(), "Poly", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestPolyCVModule>(), "Ext", 100, 100);
    for (int v = 0; v < 8; ++v)
        engine.getGraph().addConnection({{ext, v}, {poly, v}});

    const auto before = allNodeIds(engine);
    editor.setSelectedNodes({poly, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_TRUE(macro->ports[0].isInput);

    const auto portNode = theOneNewPortNode(engine, before);
    auto* inlet = dynamic_cast<MacroInletModule*>(engine.getGraph().getNodeForId(portNode)->getProcessor());
    ASSERT_NE(inlet, nullptr);
    EXPECT_EQ(inlet->getPortShape(), MacroPortShape::Poly);
    EXPECT_EQ(inlet->getVoiceCount(), 8);
    EXPECT_EQ(inlet->getVisibleInputPortCount(), 1); // no regression: Poly-N stays one fanned jack

    for (int v = 0; v < 8; ++v) {
        EXPECT_TRUE(hasConnection(engine, ext, v, portNode, v)) << "voice " << v;
        EXPECT_TRUE(hasConnection(engine, portNode, v, poly, v)) << "voice " << v;
        EXPECT_FALSE(hasConnection(engine, ext, v, poly, v)) << "voice " << v;
    }
}

// ============================================================================
// MIDI: a separate node type, never an audio/CV port
// ============================================================================

TEST(MacroAutoPort, MidiCrossingCreatesMidiInletAndOutletNodes) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto midiSrc = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "MidiSrc", 100, 100);
    auto midiDst = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "MidiDst", 700, 300);
    engine.getGraph().addConnection(
        {{midiSrc, juce::AudioProcessorGraph::midiChannelIndex}, {a, juce::AudioProcessorGraph::midiChannelIndex}});
    engine.getGraph().addConnection(
        {{b, juce::AudioProcessorGraph::midiChannelIndex}, {midiDst, juce::AudioProcessorGraph::midiChannelIndex}});

    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 2u);

    NodeID inNode, outNode;
    for (const auto& p : macro->ports) {
        EXPECT_EQ(p.kind, synth::MacroPortKind::Midi);
        for (auto* node : engine.getGraph().getNodes())
            if (node->properties["uuid"].toString() == p.nodeUuid)
                (p.isInput ? inNode : outNode) = node->nodeID;
    }
    EXPECT_NE(dynamic_cast<MacroMidiInletModule*>(engine.getGraph().getNodeForId(inNode)->getProcessor()), nullptr);
    EXPECT_NE(dynamic_cast<MacroMidiOutletModule*>(engine.getGraph().getNodeForId(outNode)->getProcessor()), nullptr);

    EXPECT_TRUE(hasMidiConnection(engine, midiSrc, inNode));
    EXPECT_TRUE(hasMidiConnection(engine, inNode, a));
    EXPECT_FALSE(hasMidiConnection(engine, midiSrc, a));

    EXPECT_TRUE(hasMidiConnection(engine, b, outNode));
    EXPECT_TRUE(hasMidiConnection(engine, outNode, midiDst));
    EXPECT_FALSE(hasMidiConnection(engine, b, midiDst));
}

// ============================================================================
// A mod-routing knob's edge: spliced when the crossing is genuine, left alone when it isn't
// (founder-review fix G3: "I noticed mod connections don't get routed - they should")
// ============================================================================

// The genuinely-external case: the mod routing's SOURCE (and therefore its attenuverter) are
// outside the group; only the TARGET is being grouped. Mirrors the original (pre-G3) pinned test's
// setup exactly, but now expects a port instead of a bare pass-through -- the founder's own
// complaint was precisely this shape of crossing.
TEST(MacroAutoPort, AttenuverterAdjacentCrossingIsSplicedForAGenuineExternalCrossing) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto source = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Source", 100, 100);
    auto ext2 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext2", 100, 300);
    const auto attenId = engine.addModRouting(source, 0, a, 0); // source -> atten -> A, all external but A
    ASSERT_TRUE(attenId.uid != 0);
    engine.getGraph().addConnection({{ext2, 0}, {b, 0}}); // a genuine plain crossing, for contrast

    const auto before = allNodeIds(engine);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 2u) << "both A's mod-routed crossing and B's plain crossing now get a port";
    EXPECT_TRUE(macro->ports[0].isInput);
    EXPECT_TRUE(macro->ports[1].isInput);
    EXPECT_EQ(engine.getGraph().getNodes().size(), before.size() + 2) << "one port node each, for A and B";

    // Find A's port (fronted node feeding A directly) versus B's port (fronted node feeding B).
    NodeID portForA, portForB;
    for (auto* node : engine.getGraph().getNodes()) {
        if (std::find(before.begin(), before.end(), node->nodeID) != before.end())
            continue; // pre-existing node
        if (hasConnection(engine, node->nodeID, 0, a, 0))
            portForA = node->nodeID;
        else if (hasConnection(engine, node->nodeID, 0, b, 0))
            portForB = node->nodeID;
    }
    ASSERT_TRUE(portForA.uid != 0);
    ASSERT_TRUE(portForB.uid != 0);
    auto* inletForA = dynamic_cast<MacroInletModule*>(engine.getGraph().getNodeForId(portForA)->getProcessor());
    auto* inletForB = dynamic_cast<MacroInletModule*>(engine.getGraph().getNodeForId(portForB)->getProcessor());
    ASSERT_NE(inletForA, nullptr);
    ASSERT_NE(inletForB, nullptr);
    // A mono ModCV target (the only shape addModRouting's single-slot channel-0 chain ever lands
    // on) must produce a mono port -- the one-jack-per-CV-mod-jack rule stage G2 established.
    EXPECT_EQ(inletForA->getPortShape(), MacroPortShape::Mono);
    EXPECT_EQ(inletForB->getPortShape(), MacroPortShape::Mono);

    // The attenuverter chain's own two edges are retargeted onto the port on its downstream side —
    // the original direct atten->A edge is gone, but the attenuverter node itself, and its upstream
    // edge from the real source, are untouched.
    EXPECT_TRUE(hasConnection(engine, source, 0, attenId, 0));
    EXPECT_TRUE(hasConnection(engine, attenId, 0, portForA, 0));
    EXPECT_TRUE(hasConnection(engine, portForA, 0, a, 0));
    EXPECT_FALSE(hasConnection(engine, attenId, 0, a, 0)) << "the direct edge was disconnected by the splice";

    EXPECT_TRUE(hasConnection(engine, ext2, 0, portForB, 0));
    EXPECT_TRUE(hasConnection(engine, portForB, 0, b, 0));

    // Still classified as a single mod routing, listed in the mod matrix, now reporting the port
    // as its destination -- exactly how any other boundary-crossing cable reports the port it
    // passes through rather than the member further inside.
    auto active = engine.getActiveModRoutings();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0].attenuverterNodeID, attenId);
    EXPECT_EQ(active[0].sourceNodeID, source);
    EXPECT_EQ(active[0].destNodeID, portForA);

    // buildVisibleCables() still draws the whole chain as ONE AttenuverterChain-kind, ModCV-coloured
    // wire, now landing on the port's own jack (Source/UI/CLAUDE.md: a cable is not a graph edge,
    // enumerate only through buildVisibleCables()).
    const auto& cables = editor.buildVisibleCables();
    bool foundChain = false;
    for (const auto& c : cables) {
        if (c.kind == GraphEditor::VisibleCable::Kind::AttenuverterChain && c.id.attenUid == attenId.uid) {
            foundChain = true;
            EXPECT_EQ(c.signal, synth::ui::CableSignal::ModCV);
            EXPECT_EQ(c.id.dstUid, portForA.uid) << "the chain's visible endpoint is now the port, not A directly";
        }
    }
    EXPECT_TRUE(foundChain);
}

// The fully-internal case: BOTH the mod routing's real source and its real target are being
// grouped together. The hidden attenuverter node can never itself be a macro member (it never gets
// a ModuleComponent), so it is nominally "outside" no matter what -- but splicing here would spawn
// two spurious ports for a routing the user is grouping wholly inside the macro. This is the one
// sub-case G3 deliberately leaves un-ported; this test pins that as the CURRENT, intended
// behaviour (docs/macros_implementation.md §7 item 7).
TEST(MacroAutoPort, ModRoutingWithBothRealEndpointsInsideStaysWhollyInternal) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto source = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Source", 100, 100);
    auto dest = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Dest", 400, 100);
    const auto attenId = engine.addModRouting(source, 0, dest, 0); // source -> atten -> dest
    ASSERT_TRUE(attenId.uid != 0);

    const auto before = allNodeIds(engine);
    editor.setSelectedNodes({source, dest});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->ports.size(), 0u) << "both real endpoints of the mod chain are members; the hidden "
                                          "attenuverter sitting nominally outside must not spawn two "
                                          "spurious ports for what is really a fully internal routing";
    EXPECT_EQ(engine.getGraph().getNodes().size(), before.size()) << "no port nodes were created";

    // Both original edges survive untouched.
    EXPECT_TRUE(hasConnection(engine, source, 0, attenId, 0));
    EXPECT_TRUE(hasConnection(engine, attenId, 0, dest, 0));

    auto active = engine.getActiveModRoutings();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0].sourceNodeID, source);
    EXPECT_EQ(active[0].destNodeID, dest);
}

// The behavioural proof: a genuinely-crossing mod routing must not just LOOK spliced -- the
// modulation signal must still actually reach the destination through atten -> port -> dest, and
// changing the attenuverter's own amount must still be audible at the far end. A test that only
// asserted "a port node now exists" could pass while the modulation had silently gone dead.
TEST(MacroAutoPort, AttenuverterAdjacentCrossingSplicedModulationSurvives) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    constexpr float kSourceValue = 0.6f;
    auto source = addModuleAt(editor, engine, std::make_unique<TestConstantModule>(kSourceValue), "Source", 100, 100);
    auto dest = addModuleAt(editor, engine, std::make_unique<TestCvProbeModule>(), "Dest", 400, 100);
    auto other = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Other", 400, 300);

    const auto attenId = engine.addModRouting(source, 0, dest, 0); // source -> atten(amount=1) -> dest
    ASSERT_TRUE(attenId.uid != 0);

    editor.setSelectedNodes({dest, other});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u) << "Dest's mod-routed crossing gets a port; Other has no crossing at all";
    ASSERT_TRUE(macro->ports[0].isInput);

    NodeID portNode;
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == macro->ports[0].nodeUuid)
            portNode = node->nodeID;
    ASSERT_TRUE(portNode.uid != 0);
    ASSERT_NE(dynamic_cast<MacroInletModule*>(engine.getGraph().getNodeForId(portNode)->getProcessor()), nullptr);

    EXPECT_TRUE(hasConnection(engine, attenId, 0, portNode, 0));
    EXPECT_TRUE(hasConnection(engine, portNode, 0, dest, 0));
    EXPECT_FALSE(hasConnection(engine, attenId, 0, dest, 0));

    auto* probe = dynamic_cast<TestCvProbeModule*>(engine.getGraph().getNodeForId(dest)->getProcessor());
    ASSERT_NE(probe, nullptr);
    auto* attenProc = engine.getGraph().getNodeForId(attenId)->getProcessor();
    ASSERT_NE(attenProc, nullptr);
    auto* amountParam = dynamic_cast<juce::AudioParameterFloat*>(attenProc->getParameters()[1]);
    ASSERT_NE(amountParam, nullptr);

    constexpr double kSampleRate = 44100.0;
    constexpr int kBlockSize = 64;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 0, kSampleRate, kBlockSize);
    graph.prepareToPlay(kSampleRate, kBlockSize);
    juce::MidiBuffer midi;

    // addModRouting already set amount to 1.0 -- the constant source's value should reach the probe
    // unchanged through atten -> port -> dest.
    for (int i = 0; i < 4; ++i) {
        juce::AudioBuffer<float> buf(1, kBlockSize);
        buf.clear();
        graph.processBlock(buf, midi);
    }
    EXPECT_NEAR(probe->lastSample(), kSourceValue, 0.01f)
        << "the modulation signal must still reach the destination through the spliced port";

    // Zeroing the attenuverter's amount silences the signal reaching the destination -- proves the
    // reading above reflects a LIVE signal path, not a stale/cached value. AttenuverterModule
    // smooths the amount change over 0.01s (prepareToPlay's smoothedAmount.reset), so render enough
    // blocks to clear the ramp with a generous margin (well over 3-4x the 0.01s time constant).
    amountParam->setValueNotifyingHost(amountParam->convertTo0to1(0.0f));
    for (int i = 0; i < 60; ++i) {
        juce::AudioBuffer<float> buf(1, kBlockSize);
        buf.clear();
        graph.processBlock(buf, midi);
    }
    EXPECT_NEAR(probe->lastSample(), 0.0f, 0.01f) << "the amount knob must still control the destination live";

    graph.releaseResources();
}

// Undo of the grouping (and its splice) must restore the mod routing exactly -- a half-restored
// mod chain would be a data-loss bug, not just a cosmetic one.
TEST(MacroAutoPort, UndoRestoresAModRoutingCrossingSpliceExactly) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto source = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Source", 100, 100);
    auto dest = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Dest", 400, 100);
    auto other = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Other", 400, 300);
    const auto attenId = engine.addModRouting(source, 0, dest, 0);
    ASSERT_TRUE(attenId.uid != 0);

    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.setSelectedNodes({dest, other});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 1);
    EXPECT_FALSE(hasConnection(engine, attenId, 0, dest, 0));

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore) << "undo removed the macro AND the spliced port";
    EXPECT_EQ(editor.getMacros().size(), 0);
    EXPECT_TRUE(hasConnection(engine, attenId, 0, dest, 0)) << "the original mod-routing edge is restored exactly";
    EXPECT_TRUE(hasConnection(engine, source, 0, attenId, 0)) << "the attenuverter's other edge was never touched";

    auto restored = engine.getActiveModRoutings();
    ASSERT_EQ(restored.size(), 1u) << "the routing is still exactly one attenuverter chain, not lost or duplicated";
    EXPECT_EQ(restored[0].attenuverterNodeID, attenId);
    EXPECT_EQ(restored[0].sourceNodeID, source);
    EXPECT_EQ(restored[0].destNodeID, dest);

    ASSERT_TRUE(undo.canRedo());
    undo.redo();

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 1);
    ASSERT_EQ(editor.getMacros().size(), 1);
    EXPECT_FALSE(hasConnection(engine, attenId, 0, dest, 0)) << "redo re-splices; the direct edge stays gone";
    auto redone = engine.getActiveModRoutings();
    ASSERT_EQ(redone.size(), 1u);
    EXPECT_EQ(redone[0].attenuverterNodeID, attenId);
    EXPECT_EQ(redone[0].sourceNodeID, source);
    EXPECT_NE(redone[0].destNodeID, dest) << "redo's destination is the port again, not the original module";
}

// ---- T144: the amount knob for a mod route crossing TWO macro boundaries -----------------
// Founder review round 3, item 1. A mod chain source->attenuverter->dest, each real endpoint
// grouped into its OWN macro (so the attenuverter sits between an outlet port on one macro and an
// inlet port on another — the reported screenshot's exact topology), left the on-canvas knob
// completely unclickable: GraphEditor::getAttenuverterNodeAt re-derived the chain's source/dest
// positions from raw graph connections (raw channel index into ModuleComponent::getPortCenter, no
// mapOutputChannel/mapInputChannel visible-jack mapping, and no collapsed-macro re-anchoring) — an
// independent geometry computation from the one buildVisibleCables()/paint() actually use, so it
// silently drifted ~300px off the real knob position the moment either endpoint became a macro
// port node, in EVERY collapse state including both macros fully expanded. It now reuses
// buildVisibleCables()'s own AttenuverterChain cable, so hit-testing can never disagree with what
// is painted (docs/macros_implementation.md §7 item 7's fix note).
TEST(MacroAutoPort, TwoMacroCrossingKnobHitTestMatchesPaintedGeometryInEveryCollapseState) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto source = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Source", 100, 100);
    auto other1 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Other1", 100, 300);
    auto dest = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Dest", 700, 100);
    auto other2 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Other2", 700, 300);
    const auto attenId = engine.addModRouting(source, 0, dest, 0); // source -> atten -> dest
    ASSERT_TRUE(attenId.uid != 0);

    editor.setSelectedNodes({source, other1});
    const auto macroA = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroA.isEmpty());
    ASSERT_EQ(editor.getMacros().find(macroA)->ports.size(), 1u) << "source's crossing gets an outlet port";

    editor.setSelectedNodes({dest, other2});
    const auto macroB = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroB.isEmpty());
    ASSERT_EQ(editor.getMacros().find(macroB)->ports.size(), 1u) << "dest's crossing gets an inlet port";

    // The graph-level chain is intact regardless of UI state: port -> atten -> port.
    auto active = engine.getActiveModRoutings();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0].attenuverterNodeID, attenId);

    auto assertKnobIsClickableAtItsPaintedMidpoint = [&](const char* label) {
        const GraphEditor::VisibleCable* chain = nullptr;
        for (const auto& c : editor.buildVisibleCables())
            if (c.kind == GraphEditor::VisibleCable::Kind::AttenuverterChain)
                chain = &c;
        ASSERT_NE(chain, nullptr) << label << ": no AttenuverterChain cable is even painted";
        const auto mid = (chain->p1 + chain->p2) / 2.0f;
        EXPECT_EQ(editor.getAttenuverterNodeAt(mid), attenId)
            << label << ": clicking the exact spot the knob is painted at must hit the attenuverter";
    };

    assertKnobIsClickableAtItsPaintedMidpoint("both expanded");

    editor.setMacroCollapsed(macroA, true);
    editor.updateComponents();
    assertKnobIsClickableAtItsPaintedMidpoint("macro A collapsed");

    editor.setMacroCollapsed(macroB, true);
    editor.updateComponents();
    assertKnobIsClickableAtItsPaintedMidpoint("both collapsed");
}

// The end-to-end proof: a REAL mouse drag on the knob (not just a NodeID lookup) must actually
// change the attenuverter's amount, and a real double-click on it must delete the routing — the
// same two gestures a user has, both of which go through getAttenuverterNodeAt. Driving synthesized
// MouseEvents into GraphEditor itself, rather than calling the hit-test function directly, catches
// the case where the hit-test is right but the gesture wiring above it silently no-ops.
TEST(MacroAutoPort, TwoMacroCrossingKnobRespondsToARealMouseDrag) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto source = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Source", 100, 100);
    auto other1 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Other1", 100, 300);
    auto dest = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Dest", 700, 100);
    auto other2 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Other2", 700, 300);
    const auto attenId = engine.addModRouting(source, 0, dest, 0);
    ASSERT_TRUE(attenId.uid != 0);

    editor.setSelectedNodes({source, other1});
    ASSERT_FALSE(editor.groupSelectionIntoMacro(true).isEmpty());
    editor.setSelectedNodes({dest, other2});
    const auto macroB = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroB.isEmpty());

    editor.setMacroCollapsed(macroB, true); // the realistic case: macros collapsed to hide detail
    editor.updateComponents();

    const GraphEditor::VisibleCable* chain = nullptr;
    for (const auto& c : editor.buildVisibleCables())
        if (c.kind == GraphEditor::VisibleCable::Kind::AttenuverterChain)
            chain = &c;
    ASSERT_NE(chain, nullptr);
    // chain->p1/p2 are CANVAS coordinates (what buildVisibleCables()/paint() use); a MouseEvent fed
    // into GraphEditor itself needs GraphEditor-local (screen) coordinates -- canvasToEditorLocal
    // is the one place that conversion happens, mirroring what mouseDown does internally in reverse.
    const auto knobCanvasPos = (chain->p1 + chain->p2) / 2.0f;
    const auto knobPos = canvasToEditorLocal(editor, knobCanvasPos);

    auto* attenProc = engine.getGraph().getNodeForId(attenId)->getProcessor();
    auto* amountParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(attenProc, "amount"));
    ASSERT_NE(amountParam, nullptr);
    // addModRouting starts amount at its +1 ceiling (source -> atten(amount=1) -> dest) -- pull it
    // to the middle first so an upward drag has room to register a real increase.
    amountParam->setValueNotifyingHost(amountParam->convertTo0to1(0.0f));
    const float before = amountParam->get();

    editor.mouseDown(makeEditorMouseEvent(editor, knobPos));
    editor.mouseDrag(makeEditorMouseEvent(editor, knobPos + juce::Point<float>(0.0f, -40.0f))); // drag up
    editor.mouseUp(makeEditorMouseEvent(editor, knobPos + juce::Point<float>(0.0f, -40.0f)));

    EXPECT_GT(amountParam->get(), before) << "dragging the knob up on a two-macro crossing must raise the amount";

    // Double-click on the same knob deletes the routing outright — proves the second gesture that
    // shares this hit-test (mouseDoubleClick) survives the fix too.
    editor.mouseDoubleClick(makeEditorMouseEvent(editor, knobPos));
    EXPECT_TRUE(engine.getActiveModRoutings().empty()) << "double-clicking the knob must delete the routing";
}

// ============================================================================
// One undo step: grouping and every spliced port together
// ============================================================================

TEST(MacroAutoPort, GroupingAndSplicedPortsIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto cin = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Cin", 100, 100);
    auto cout = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Cout", 700, 300);
    engine.getGraph().addConnection({{cin, 0}, {a, 0}});
    engine.getGraph().addConnection({{b, 0}, {cout, 0}});

    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 2u);
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 2);
    EXPECT_FALSE(hasConnection(engine, cin, 0, a, 0));

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore) << "a single Cmd+Z removed the macro AND both ports";
    EXPECT_EQ(editor.getMacros().size(), 0);
    EXPECT_TRUE(hasConnection(engine, cin, 0, a, 0)) << "the original cable is restored, not left spliced";
    EXPECT_TRUE(hasConnection(engine, b, 0, cout, 0));

    // Redo must restore the macro AND both ports together too, not just the macro (the sharp edge
    // changeMacroPortShape's own comment names: "the combined graph+macro restore has to be a
    // single action to get both undo AND redo right").
    ASSERT_TRUE(undo.canRedo());
    undo.redo();

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 2);
    ASSERT_EQ(editor.getMacros().size(), 1);
    EXPECT_EQ(editor.getMacros().getAll()[0].ports.size(), 2u);
    EXPECT_FALSE(hasConnection(engine, cin, 0, a, 0)) << "redo re-splices; the direct cable stays gone";

    bool foundInlet = false;
    bool foundOutlet = false;
    for (auto* node : engine.getGraph().getNodes()) {
        if (auto* inlet = dynamic_cast<MacroInletModule*>(node->getProcessor())) {
            foundInlet = true;
            EXPECT_EQ(inlet->getPortShape(), MacroPortShape::Mono) << "redo must not lose the spliced port's shape";
        }
        if (auto* outlet = dynamic_cast<MacroOutletModule*>(node->getProcessor())) {
            foundOutlet = true;
            EXPECT_EQ(outlet->getPortShape(), MacroPortShape::Mono);
        }
    }
    EXPECT_TRUE(foundInlet);
    EXPECT_TRUE(foundOutlet);
}
