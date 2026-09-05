// GraphEditor-level tests for auto-creating macro ports on grouping (founder-review fix F5,
// docs/macros.md §7 item 6.1/6.2): for every graph connection crossing a would-be macro's
// boundary, GraphEditor::groupSelectionIntoMacro(true) splices in the matching MacroInlet/
// MacroOutlet (or MIDI variant) node — disconnect external<->internal, insert the port, reconnect
// external->port->internal (or the reverse for an outlet) — all inside ONE
// recordGraphAndMacroChange transaction. requestGroupSelectionIntoMacro() covers the tri-state
// preference + its modal gating. MacroAutoPortPromptDialog itself (pure UI) is covered in
// MacroPortConfigDialogTests.cpp; the preference's ApplicationProperties round trip is covered in
// PreferencesSettingsTabTests.cpp.

#include "../Source/AppUndoManager.h"
#include "../Source/Modules/AttenuverterModule.h"
#include "../Source/Modules/FX/DelayModule.h"
#include "../Source/Modules/FX/ReverbModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/MacroInletModule.h"
#include "../Source/Modules/MacroMidiInletModule.h"
#include "../Source/Modules/MacroMidiOutletModule.h"
#include "../Source/Modules/MacroOutletModule.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/MacroCardComponent.h"
#include <atomic>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

// A plain mono module: 1 audio in, 1 audio out, MIDI accepted/produced (ModuleBase's own
// defaults) — deliberately dumb, so a test's crossing connections exercise only the shape/kind
// derivation under test, never a real module's own quirks.
class TestMonoModule : public ModuleBase {
public:
    TestMonoModule()
        : ModuleBase("TestMono", 1, 1) {}
    void prepareToPlay(double, int) override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::Math; }
};

// A single Poly-8 ModCV bus, both directions — the shape §7 item 6.1 calls "a poly-bus crossing".
// Mirrors PolyMidiModule's own pitch-fan mapping (raw 0-7, head at 0, span 8, ModCV role) but
// isolated from that module's other quirks (its own getVisibleOutputPortCount() override, a
// second Gate fan sharing jack 0) which would make a test depend on code this fix does not own.
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

// A constant-output source for the mod-routing behavioural test below: outputs `value_` on
// channel 0 regardless of input, so the test's expected value at the far end of the spliced chain
// is a fixed number rather than an LFO's time-varying phase.
class TestConstantModule : public ModuleBase {
public:
    explicit TestConstantModule(float value)
        : ModuleBase("TestConstant", 1, 1)
        , value_(value) {}
    void prepareToPlay(double, int) override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            buffer.clear(ch, 0, buffer.getNumSamples());
            if (ch == 0)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample(0, i, value_);
        }
    }
    ModuleType getModuleType() const override { return ModuleType::Math; }

private:
    float value_;
};

// A CV probe: records the last sample it saw on its own channel 0 input, so a test can confirm a
// modulation signal ACTUALLY reached the far side of a spliced port, not just that the port node
// exists and the graph edges look right.
class TestCvProbeModule : public ModuleBase {
public:
    TestCvProbeModule()
        : ModuleBase("TestProbe", 1, 1) {}
    void prepareToPlay(double, int) override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        if (buffer.getNumChannels() > 0 && buffer.getNumSamples() > 0)
            lastSample_.store(buffer.getReadPointer(0)[buffer.getNumSamples() - 1], std::memory_order_relaxed);
    }
    ModuleType getModuleType() const override { return ModuleType::Math; }
    float lastSample() const { return lastSample_.load(std::memory_order_relaxed); }

private:
    std::atomic<float> lastSample_{0.0f};
};

NodeID addModuleAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor,
                   const juce::String& name, int x, int y) {
    if (auto* mb = dynamic_cast<ModuleBase*>(processor.get()))
        if (name.isNotEmpty())
            mb->setModuleName(name);
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

bool hasConnection(AudioEngine& engine, NodeID srcId, int srcCh, NodeID dstId, int dstCh) {
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == srcId && c.source.channelIndex == srcCh && c.destination.nodeID == dstId &&
            c.destination.channelIndex == dstCh)
            return true;
    return false;
}

bool hasMidiConnection(AudioEngine& engine, NodeID srcId, NodeID dstId) {
    return hasConnection(engine, srcId, juce::AudioProcessorGraph::midiChannelIndex, dstId,
                         juce::AudioProcessorGraph::midiChannelIndex);
}

// The one new-port node a group produced, asserting there is exactly one. Most tests below add
// exactly one crossing group, so this is the common case; tests that expect more resolve nodes by
// direction/kind explicitly instead.
NodeID theOneNewPortNode(AudioEngine& engine, const std::vector<NodeID>& before) {
    NodeID found;
    int count = 0;
    for (auto* node : engine.getGraph().getNodes()) {
        if (std::find(before.begin(), before.end(), node->nodeID) == before.end()) {
            found = node->nodeID;
            ++count;
        }
    }
    EXPECT_EQ(count, 1) << "expected exactly one new node";
    return found;
}

std::vector<NodeID> allNodeIds(AudioEngine& engine) {
    std::vector<NodeID> ids;
    for (auto* node : engine.getGraph().getNodes())
        ids.push_back(node->nodeID);
    return ids;
}

} // namespace

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
    // internal jack it fronts (docs/macros.md). Reverb's own jack is ONE jack; so must this be.
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
// behaviour (docs/macros.md §7 item 7).
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

// ============================================================================
// Presentation (founder-review fix G6, docs/macros.md §7 item 4 note): the module-count
// indicator, the tooltip's member list and the content preview must count/list MODULES, never
// the port nodes a crossing cable spliced in — members.size() itself stays untouched.
// ============================================================================

TEST(MacroAutoPort, PresentationCountExcludesAutoCreatedPortsFounderScenario) {
    // The founder's exact reproduction: "the number of modules indicator seems to show more then
    // there are - 4 when i grouped the delay and reverb in the default patch - should have shown
    // 2." Delay -> Reverb, each with a crossing stereo connection on its outward-facing side (both
    // default to Dual I/O OFF, so each crossing collapses into ONE port, same as the founder's
    // screenshot) -> 2 real modules + 2 auto-created port nodes = 4 members, but 2 modules.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto delay = addModuleAt(editor, engine, std::make_unique<DelayModule>(), "Delay", 400, 100);
    auto reverb = addModuleAt(editor, engine, std::make_unique<ReverbModule>(), "Reverb", 400, 300);
    auto cinL = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "CinL", 100, 60);
    auto cinR = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "CinR", 100, 160);
    auto coutL = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "CoutL", 700, 260);
    auto coutR = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "CoutR", 700, 360);
    engine.getGraph().addConnection({{cinL, 0}, {delay, 0}});
    engine.getGraph().addConnection({{cinR, 0}, {delay, 1}});
    engine.getGraph().addConnection({{delay, 0}, {reverb, 0}});
    engine.getGraph().addConnection({{delay, 1}, {reverb, 1}});
    engine.getGraph().addConnection({{reverb, 0}, {coutL, 0}});
    engine.getGraph().addConnection({{reverb, 1}, {coutR, 0}});

    editor.setSelectedNodes({delay, reverb});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 2u) << "one collapsed-stereo inlet feeding Delay, one outlet after Reverb";
    ASSERT_EQ((int)macro->members.size(), 4) << "the founder's literal '4' — members.size() itself is correct";

    // The fix: the user-facing MODULE count must read 2, not members.size()'s 4.
    EXPECT_EQ(macro->moduleMemberCount(), 2);

    const auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr) << "groupSelectionIntoMacro leaves the macro collapsed by default";
    EXPECT_EQ(card->getModuleCountText(), "2 modules, 2 ports")
        << "honest about both quantities, never a bare '2' that hides the ports entirely";

    // Guard (do not remove): a port node must STILL be a real member — this is the invariant the
    // whole feature (bounds/group-drag/bypass-mute/undo/serialization) depends on. A future "fix"
    // that removes ports from `members` to make the count line up is the wrong fix.
    for (const auto& p : macro->ports)
        EXPECT_TRUE(macro->hasMember(p.nodeUuid)) << "port node " << p.nodeUuid << " must remain a member";
}

TEST(MacroAutoPort, PresentationTooltipListsModulesNotPortNodes) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Delay", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Reverb", 400, 300);
    auto cin = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Cin", 100, 100);
    auto cout = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Cout", 700, 300);
    engine.getGraph().addConnection({{cin, 0}, {a, 0}});
    engine.getGraph().addConnection({{b, 0}, {cout, 0}});

    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 2u);

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    const auto tooltip = card->getTooltip();
    EXPECT_TRUE(tooltip.contains("Delay"));
    EXPECT_TRUE(tooltip.contains("Reverb"));
    EXPECT_FALSE(tooltip.contains("Macro In")) << "a port node's own module name must not appear in the member list";
    EXPECT_FALSE(tooltip.contains("Macro Out"));
}

TEST(MacroAutoPort, PresentationCountIsUnchangedForAPlainMacroWithNoPorts) {
    // Regression guard: the plain P8-12 grouping case (no crossing cable, no ports at all) —
    // the most common case in the existing suite — must read exactly as it always did.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);

    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(true); // no crossing cable -> no ports
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->ports.empty());
    EXPECT_EQ(macro->moduleMemberCount(), 2);

    const auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->getModuleCountText(), "2 modules");
}

TEST(MacroAutoPort, PresentationCountUpdatesWhenAPortIsDeleted) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto cin = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Cin", 100, 100);
    auto cout = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Cout", 700, 300);
    engine.getGraph().addConnection({{cin, 0}, {a, 0}});
    engine.getGraph().addConnection({{b, 0}, {cout, 0}});

    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 2u);
    ASSERT_EQ(macro->moduleMemberCount(), 2);

    const juce::String outletUuid = macro->ports[0].isInput ? macro->ports[1].nodeUuid : macro->ports[0].nodeUuid;
    editor.removeMacroPort(macroId, outletUuid);

    macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->ports.size(), 1u);
    EXPECT_EQ(macro->moduleMemberCount(), 2) << "deleting a port must never touch the MODULE count";

    const auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->getModuleCountText(), "2 modules, 1 port");
}

TEST(MacroAutoPort, PresentationCountExcludesAHandAddedPortViaConfigureIO) {
    // There is no provenance distinction between an auto-created port and one added by hand
    // through Configure I/O — both must be excluded from the module count the same way.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(); // no auto-ports
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_TRUE(editor.getMacros().find(macroId)->ports.empty());

    const auto portUuid = editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                              MacroPortShape::Mono, 1, "Extra In");
    ASSERT_FALSE(portUuid.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->ports.size(), 1u);
    EXPECT_EQ((int)macro->members.size(), 3);
    EXPECT_EQ(macro->moduleMemberCount(), 2) << "a hand-added port must not inflate the module count";

    const auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->getModuleCountText(), "2 modules, 1 port");
}

// ============================================================================
// requestGroupSelectionIntoMacro(): the modal fires only when Unset AND there is a crossing cable
// ============================================================================

TEST(MacroAutoPort, ModalDoesNotFireWhenTheSelectionHasNoCrossingCable) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);

    ASSERT_EQ(editor.getMacroAutoPortPreference(), GraphEditor::MacroAutoPortPreference::Unset);
    bool modalShown = false;
    editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)>) { modalShown = true; };

    editor.setSelectedNodes({a, b});
    editor.requestGroupSelectionIntoMacro();

    EXPECT_FALSE(modalShown);
    EXPECT_EQ(editor.getMacros().size(), 1) << "grouping proceeds immediately when there's nothing to decide";
}

TEST(MacroAutoPort, ModalFiresWhenUnsetAndTheSelectionHasACrossingCable) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {a, 0}});

    ASSERT_TRUE(editor.selectionHasCrossingMacroCable() == false) << "nothing selected yet";
    editor.setSelectedNodes({a, b});
    ASSERT_TRUE(editor.selectionHasCrossingMacroCable());

    bool modalShown = false;
    std::function<void(bool, bool)> capturedRespond;
    editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)> respond) {
        modalShown = true;
        capturedRespond = respond;
    };

    editor.requestGroupSelectionIntoMacro();

    EXPECT_TRUE(modalShown);
    EXPECT_EQ(editor.getMacros().size(), 0) << "grouping is deferred until the user answers";

    ASSERT_TRUE((bool)capturedRespond);
    capturedRespond(true, false); // "Create Ports", don't remember
    ASSERT_EQ(editor.getMacros().size(), 1);
    EXPECT_EQ(editor.getMacros().getAll()[0].ports.size(), 1u);
    EXPECT_EQ(editor.getMacroAutoPortPreference(), GraphEditor::MacroAutoPortPreference::Unset)
        << "remember=false must not persist the choice";
}

// A module fresh on the canvas has no "uuid" property yet (only lazily assigned on first save, or
// by groupSelectionIntoMacro() itself once it decides to proceed) — this is the single most common
// real path: drop two never-saved modules, wire one to an existing module, group immediately. The
// crossing-cable gate must not silently under-detect just because nothing has a uuid yet.
TEST(MacroAutoPort, ModalFiresEvenWhenTheSelectedModulesHaveNoUuidYet) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    // Deliberately not addModuleAt() (which stamps a uuid) -- nodes added exactly the way a fresh
    // drop onto the canvas does, with no "uuid" property at all.
    auto addBareModule = [&](std::unique_ptr<juce::AudioProcessor> processor, int x, int y) {
        auto node = engine.getGraph().addNode(std::move(processor));
        node->properties.set("x", x);
        node->properties.set("y", y);
        editor.updateComponents();
        return node->nodeID;
    };
    auto a = addBareModule(std::make_unique<TestMonoModule>(), 400, 100);
    auto b = addBareModule(std::make_unique<TestMonoModule>(), 400, 300);
    auto ext = addBareModule(std::make_unique<TestMonoModule>(), 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {a, 0}});

    editor.setSelectedNodes({a, b});
    ASSERT_TRUE(editor.selectionHasCrossingMacroCable()) << "must detect the crossing cable without any uuid";

    bool modalShown = false;
    editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)>) { modalShown = true; };
    editor.requestGroupSelectionIntoMacro();

    EXPECT_TRUE(modalShown);
    EXPECT_EQ(editor.getMacros().size(), 0) << "grouping is still deferred until the user answers";
}

// A selection that already touches an existing macro is refused outright by
// groupSelectionIntoMacro() (see its own guard) -- nothing to decide, so the modal must mirror that
// refusal rather than asking a question whose answer can never be applied.
TEST(MacroAutoPort, ModalDoesNotFireWhenASelectedModuleIsAlreadyInAMacro) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto x = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "X", 400, 100);
    auto y = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Y", 400, 300);
    editor.setSelectedNodes({x, y});
    const auto firstMacroId = editor.groupSelectionIntoMacro(false);
    ASSERT_FALSE(firstMacroId.isEmpty());

    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 700, 100);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 300);
    engine.getGraph().addConnection({{ext, 0}, {b, 0}});

    editor.setSelectedNodes({x, b}); // x is already a macro member
    EXPECT_FALSE(editor.selectionHasCrossingMacroCable());

    bool modalShown = false;
    editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)>) { modalShown = true; };
    editor.requestGroupSelectionIntoMacro();

    EXPECT_FALSE(modalShown);
    EXPECT_EQ(editor.getMacros().size(), 1) << "the refused grouping must not have created a second macro";
}

TEST(MacroAutoPort, ModalRememberPersistsThePreference) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {a, 0}});

    std::function<void(bool, bool)> capturedRespond;
    editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)> respond) { capturedRespond = respond; };

    editor.setSelectedNodes({a, b});
    editor.requestGroupSelectionIntoMacro();
    ASSERT_TRUE((bool)capturedRespond);
    capturedRespond(false, true); // "Leave Cables As Is", remember it

    EXPECT_EQ(editor.getMacroAutoPortPreference(), GraphEditor::MacroAutoPortPreference::LeaveCablesAsIs);
    ASSERT_EQ(editor.getMacros().size(), 1);
    EXPECT_TRUE(editor.getMacros().getAll()[0].ports.empty());
    EXPECT_TRUE(hasConnection(engine, ext, 0, a, 0));
}

TEST(MacroAutoPort, PreferenceAutoCreatePortsSkipsTheModal) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    editor.setMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference::AutoCreatePorts);
    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {a, 0}});

    bool modalShown = false;
    editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)>) { modalShown = true; };

    editor.setSelectedNodes({a, b});
    editor.requestGroupSelectionIntoMacro();

    EXPECT_FALSE(modalShown);
    ASSERT_EQ(editor.getMacros().size(), 1);
    EXPECT_EQ(editor.getMacros().getAll()[0].ports.size(), 1u);
}

TEST(MacroAutoPort, PreferenceLeaveCablesAsIsSkipsTheModal) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    editor.setMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference::LeaveCablesAsIs);
    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {a, 0}});

    bool modalShown = false;
    editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)>) { modalShown = true; };

    editor.setSelectedNodes({a, b});
    editor.requestGroupSelectionIntoMacro();

    EXPECT_FALSE(modalShown);
    ASSERT_EQ(editor.getMacros().size(), 1);
    EXPECT_TRUE(editor.getMacros().getAll()[0].ports.empty());
    EXPECT_TRUE(hasConnection(engine, ext, 0, a, 0));
}
