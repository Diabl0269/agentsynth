// GraphEditor integration tests (issue #163): poly connection auto-fan-out and poly-toggle
// rewiring, exercised through a full graph with drag/toggle interactions.
// Shared GraphEditorTest fixture and helpers live in GraphEditorTestHelpers.h.

#include "GraphEditorTestHelpers.h"

#include "Modules/ADSRModule.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/PolyMidiModule.h"
#include "Modules/VCAModule.h"

// ---- Integration tests (full graph + drag/toggle interactions) ----

TEST_F(GraphEditorTest, DragBetweenPolyModulesFansOutAllVoices) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto adsrNode = engine.getGraph().addNode(std::make_unique<ADSRModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());
    setPolyParam(*adsrNode->getProcessor(), true);
    setPolyParam(*vcaNode->getProcessor(), true);

    editor.updateComponents();

    ModuleComponent* adsrComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == adsrNode->getProcessor())
                    adsrComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(adsrComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    adsrComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(adsrComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto vcaTargetPoint = vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(2, true);
    editor.endConnectionDrag(vcaTargetPoint);

    auto& graph = engine.getGraph();

    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == adsrNode->nodeID && conn.source.channelIndex == i &&
                conn.destination.nodeID == vcaNode->nodeID && conn.destination.channelIndex == 8 + i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing fan connection for voice " << i;
    }

    int connectionCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == adsrNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            ++connectionCount;
    EXPECT_EQ(connectionCount, 8);

    bool foundAttenuverter = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            foundAttenuverter = true;
    EXPECT_FALSE(foundAttenuverter);
}

TEST_F(GraphEditorTest, DragBetweenMonoModulesIsUnchanged) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto adsrNode = engine.getGraph().addNode(std::make_unique<ADSRModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());

    editor.updateComponents();

    ModuleComponent* adsrComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == adsrNode->getProcessor())
                    adsrComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(adsrComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    adsrComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(adsrComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto vcaTargetPoint = vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(2, true);
    editor.endConnectionDrag(vcaTargetPoint);

    auto& graph = engine.getGraph();

    // Legacy behaviour: a mono mod-CV wire is mediated by an attenuverter, not a direct connection.
    bool foundAttenuverter = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            foundAttenuverter = true;
    EXPECT_TRUE(foundAttenuverter);

    bool directConnectionFound = false;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == adsrNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            directConnectionFound = true;
    EXPECT_FALSE(directConnectionFound);
}

TEST_F(GraphEditorTest, DisconnectPolyPortRemovesEntireFan) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto adsrNode = engine.getGraph().addNode(std::make_unique<ADSRModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());
    setPolyParam(*adsrNode->getProcessor(), true);
    setPolyParam(*vcaNode->getProcessor(), true);

    editor.updateComponents();

    ModuleComponent* adsrComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == adsrNode->getProcessor())
                    adsrComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(adsrComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    adsrComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(adsrComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto vcaTargetPoint = vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(2, true);
    editor.endConnectionDrag(vcaTargetPoint);

    auto& graph = engine.getGraph();

    int preCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == adsrNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            ++preCount;
    ASSERT_EQ(preCount, 8) << "Setup must produce the 8-voice fan before disconnecting";

    editor.disconnectPort(vcaComp, 2, true, false);

    int postCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == adsrNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            ++postCount;
    EXPECT_EQ(postCount, 0);
}

// Poly MIDI's pitch fan carries raw Hz, not normalised CV. Wrapping it in an attenuverter scaled an
// absolute frequency AND fed an Hz-magnitude peak into the wire-activity metering, which multiplied
// the stroke width until the "wire" painted as a screen-filling blob.
TEST_F(GraphEditorTest, PitchSourceIsNeverWrappedInAnAttenuverter) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto polyMidiNode = engine.getGraph().addNode(std::make_unique<PolyMidiModule>());
    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    // Mono oscillator: channel 0 is "Pitch", which getModulationTargets() lists as promotable.
    setPolyParam(*oscNode->getProcessor(), false);

    editor.updateComponents();

    ModuleComponent* polyMidiComp = nullptr;
    ModuleComponent* oscComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == polyMidiNode->getProcessor())
                    polyMidiComp = mod;
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
            }
        }
    }

    ASSERT_NE(polyMidiComp, nullptr);
    ASSERT_NE(oscComp, nullptr);

    polyMidiComp->setBounds(0, 0, 100, 100);
    oscComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(polyMidiComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(oscComp->getBounds().getPosition() + oscComp->getPortCenter(0, true));

    auto& graph = engine.getGraph();

    for (auto* node : graph.getNodes())
        EXPECT_EQ(dynamic_cast<AttenuverterModule*>(node->getProcessor()), nullptr)
            << "A pitch-role source must be wired direct, never through an attenuverter";

    bool foundDirect = false;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == polyMidiNode->nodeID && conn.destination.nodeID == oscNode->nodeID)
            foundDirect = true;
    EXPECT_TRUE(foundDirect) << "Poly MIDI should still connect directly to the oscillator's pitch input";
}

// Toggling poly off collapses the fan back through the same connect path, which is how the blob was
// originally triggered — it must not reintroduce an attenuverter on the pitch wire either.
TEST_F(GraphEditorTest, TogglingPolyOffKeepsPitchWireDirect) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto polyMidiNode = engine.getGraph().addNode(std::make_unique<PolyMidiModule>());
    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    setPolyParam(*oscNode->getProcessor(), true);

    editor.updateComponents();

    ModuleComponent* polyMidiComp = nullptr;
    ModuleComponent* oscComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == polyMidiNode->getProcessor())
                    polyMidiComp = mod;
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
            }
        }
    }

    ASSERT_NE(polyMidiComp, nullptr);
    ASSERT_NE(oscComp, nullptr);

    polyMidiComp->setBounds(0, 0, 100, 100);
    oscComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(polyMidiComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(oscComp->getBounds().getPosition() + oscComp->getPortCenter(0, true));

    setPolyParam(*oscNode->getProcessor(), false);

    for (auto* node : engine.getGraph().getNodes())
        EXPECT_EQ(dynamic_cast<AttenuverterModule*>(node->getProcessor()), nullptr)
            << "Collapsing a pitch fan to mono must not insert an attenuverter";
}

TEST_F(GraphEditorTest, AudioIONodesAreSingletons) {
    EXPECT_TRUE(GraphEditor::isSingletonIOModule("Audio Output"));
    EXPECT_TRUE(GraphEditor::isSingletonIOModule("Audio Input"));
    EXPECT_FALSE(GraphEditor::isSingletonIOModule("Oscillator"));

    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    graph.clear();
    EXPECT_FALSE(GraphEditor::graphHasModuleNamed(graph, "Audio Output"));

    DummyDragSource dummySource;
    juce::var description("Audio Output");
    juce::DragAndDropTarget::SourceDetails details(description, &dummySource, juce::Point<int>(100, 100));

    // The first drop creates it — the module library offers Audio Output so a deleted one is
    // recoverable rather than stranding the patch with no way to hear it.
    editor.itemDropped(details);
    EXPECT_TRUE(GraphEditor::graphHasModuleNamed(graph, "Audio Output"));
    auto countAfterFirst = graph.getNodes().size();

    // A second drop must be a no-op: two output nodes would sum into the same device buffer.
    editor.itemDropped(details);
    EXPECT_EQ(graph.getNodes().size(), countAfterFirst) << "A duplicate Audio Output must not be created";
}

TEST_F(GraphEditorTest, DragMonoLfoOntoPolyVcaBroadcastsToEveryVoice) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto lfoNode = engine.getGraph().addNode(std::make_unique<LFOModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());
    setPolyParam(*vcaNode->getProcessor(), true);

    editor.updateComponents();

    ModuleComponent* lfoComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == lfoNode->getProcessor())
                    lfoComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(lfoComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    lfoComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(lfoComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto vcaTargetPoint = vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(2, true);
    editor.endConnectionDrag(vcaTargetPoint);

    auto& graph = engine.getGraph();

    // Every voice's gain CV is fed from the SAME LFO output channel — that is the broadcast.
    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == lfoNode->nodeID && conn.source.channelIndex == 0 &&
                conn.destination.nodeID == vcaNode->nodeID && conn.destination.channelIndex == 8 + i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing broadcast connection for voice " << i;
    }

    int connectionCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == lfoNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            ++connectionCount;
    EXPECT_EQ(connectionCount, 8);

    // The broadcast must read back as ONE PolyBus wire with an x8 badge, not eight stacked wires.
    int polyBusCount = 0;
    for (const auto& r : engine.getModulationRoutings()) {
        if (r.kind == AudioEngine::RoutingKind::PolyBus && r.sourceNodeID == lfoNode->nodeID &&
            r.destNodeID == vcaNode->nodeID) {
            ++polyBusCount;
            EXPECT_EQ(r.voiceCount, 8);
            EXPECT_EQ(r.destChannelIndex, 8);
            EXPECT_EQ(r.role, PortRole::ModCV);
        }
    }
    EXPECT_EQ(polyBusCount, 1);

    int directCvCount = 0;
    for (const auto& r : engine.getModulationRoutings())
        if (r.kind == AudioEngine::RoutingKind::DirectCV && r.sourceNodeID == lfoNode->nodeID)
            ++directCvCount;
    EXPECT_EQ(directCvCount, 0) << "Broadcast edges should be consumed by the PolyBus collapse";
}

TEST_F(GraphEditorTest, TogglingPolyOnBroadcastsExistingMonoModWire) {
    // A mono LFO -> mono VCA CV wire (an attenuverter chain) must spread across all eight voices
    // when the VCA is switched to poly, rather than staying on voice 0.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto lfoNode = engine.getGraph().addNode(std::make_unique<LFOModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());

    editor.updateComponents();

    ModuleComponent* lfoComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == lfoNode->getProcessor())
                    lfoComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(lfoComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    lfoComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(lfoComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(2, true));

    auto& graph = engine.getGraph();

    setPolyParam(*vcaNode->getProcessor(), true);

    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == lfoNode->nodeID && conn.source.channelIndex == 0 &&
                conn.destination.nodeID == vcaNode->nodeID && conn.destination.channelIndex == 8 + i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing broadcast connection for voice " << i << " after poly toggle";
    }

    bool foundAttenuverter = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            foundAttenuverter = true;
    EXPECT_FALSE(foundAttenuverter) << "A poly fan is wired direct, so the attenuverter must be gone";
}

TEST_F(GraphEditorTest, TogglingPolyOnFansOutExistingConnection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto filterNode = engine.getGraph().addNode(std::make_unique<FilterModule>());

    editor.updateComponents();

    ModuleComponent* oscComp = nullptr;
    ModuleComponent* filterComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
                if (mod->getModule() == filterNode->getProcessor())
                    filterComp = mod;
            }
        }
    }

    ASSERT_NE(oscComp, nullptr);
    ASSERT_NE(filterComp, nullptr);

    oscComp->setBounds(0, 0, 100, 100);
    filterComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(oscComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto filterTargetPoint = filterComp->getBounds().getPosition() + filterComp->getPortCenter(0, true);
    editor.endConnectionDrag(filterTargetPoint);

    auto& graph = engine.getGraph();

    int monoCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == oscNode->nodeID && conn.source.channelIndex == 0 &&
            conn.destination.nodeID == filterNode->nodeID && conn.destination.channelIndex == 0)
            ++monoCount;
    ASSERT_EQ(monoCount, 1) << "Setup must produce a single mono connection before toggling poly";

    // Toggling poly on both ends must re-anchor the mono wire onto the 8-voice fan.
    setPolyParam(*oscNode->getProcessor(), true);
    setPolyParam(*filterNode->getProcessor(), true);

    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == oscNode->nodeID && conn.source.channelIndex == i &&
                conn.destination.nodeID == filterNode->nodeID && conn.destination.channelIndex == i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing fan connection for voice " << i;
    }

    int fanCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == oscNode->nodeID && conn.destination.nodeID == filterNode->nodeID)
            ++fanCount;
    EXPECT_EQ(fanCount, 8);
}

TEST_F(GraphEditorTest, TogglingPolyOffCollapsesFanToSingleConnection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto filterNode = engine.getGraph().addNode(std::make_unique<FilterModule>());

    editor.updateComponents();

    ModuleComponent* oscComp = nullptr;
    ModuleComponent* filterComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
                if (mod->getModule() == filterNode->getProcessor())
                    filterComp = mod;
            }
        }
    }

    ASSERT_NE(oscComp, nullptr);
    ASSERT_NE(filterComp, nullptr);

    oscComp->setBounds(0, 0, 100, 100);
    filterComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(oscComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto filterTargetPoint = filterComp->getBounds().getPosition() + filterComp->getPortCenter(0, true);
    editor.endConnectionDrag(filterTargetPoint);

    auto& graph = engine.getGraph();

    // Toggle poly ON for both to build the 8-voice fan.
    setPolyParam(*oscNode->getProcessor(), true);
    setPolyParam(*filterNode->getProcessor(), true);

    int fanCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == oscNode->nodeID && conn.destination.nodeID == filterNode->nodeID)
            ++fanCount;
    ASSERT_EQ(fanCount, 8) << "Setup must produce the 8-voice fan before toggling poly off";

    // Toggle poly OFF for both — the fan must collapse back to a single mono wire.
    setPolyParam(*oscNode->getProcessor(), false);
    setPolyParam(*filterNode->getProcessor(), false);

    int monoCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == oscNode->nodeID && conn.destination.nodeID == filterNode->nodeID)
            ++monoCount;
    EXPECT_EQ(monoCount, 1);

    bool foundMonoConn = false;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == oscNode->nodeID && conn.source.channelIndex == 0 &&
            conn.destination.nodeID == filterNode->nodeID && conn.destination.channelIndex == 0)
            foundMonoConn = true;
    EXPECT_TRUE(foundMonoConn);
}

TEST_F(GraphEditorTest, TogglingPolyMovesModCvWireOntoPolyChannels) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto adsrNode = engine.getGraph().addNode(std::make_unique<ADSRModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());

    editor.updateComponents();

    ModuleComponent* adsrComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == adsrNode->getProcessor())
                    adsrComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(adsrComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    adsrComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    // Mono ADSR out jack0 -> mono VCA in jack1 (CV) creates an attenuverter chain.
    editor.beginConnectionDrag(adsrComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto vcaTargetPoint = vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(2, true);
    editor.endConnectionDrag(vcaTargetPoint);

    auto& graph = engine.getGraph();

    bool foundAttenuverterBefore = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            foundAttenuverterBefore = true;
    ASSERT_TRUE(foundAttenuverterBefore) << "Setup must create an attenuverter chain before toggling poly";

    // Toggling poly on both ends must collapse the attenuverter chain into a direct 8-voice fan
    // landing on raw channels 8-15 (VCA's poly CV bus), not the stale raw channel 1.
    setPolyParam(*adsrNode->getProcessor(), true);
    setPolyParam(*vcaNode->getProcessor(), true);

    bool foundAttenuverterAfter = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            foundAttenuverterAfter = true;
    EXPECT_FALSE(foundAttenuverterAfter);

    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == adsrNode->nodeID && conn.source.channelIndex == i &&
                conn.destination.nodeID == vcaNode->nodeID && conn.destination.channelIndex == 8 + i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing direct fan connection for voice " << i;
    }

    int directCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == adsrNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            ++directCount;
    EXPECT_EQ(directCount, 8);
}
