// GraphEditor tests for the Channel Strip's OUTPUT side of Dual I/O: collapsed, the main output is one Audio jack over
// raw ch0 and kRightBase, so a cable made from it, a Smart Connection insert and a delete heal must all keep the two
// raw legs (bug kind jack-raw-mismatch; the heal matrix rows are in GraphEditorSmartConnectionInsertTests.cpp).
// Regression test for FRO1232: the strip showed two output jacks beside one input, with no way to collapse them.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/FX/CompressorModule.h"
#include "Modules/MasterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;
constexpr int kR = ChannelStripModule::kRightBase;

ModuleComponent* stripCardOf(GraphEditor& editor, juce::AudioProcessor* proc) {
    for (auto* mc : editor.getModuleComponents())
        if (mc != nullptr && mc->getModule() == proc)
            return mc;
    return nullptr;
}
} // namespace

TEST_F(GraphEditorTest, ResolvePolyLinkTakesBothLegsOfACollapsedChannelStripOutput) {
    ChannelStripModule strip;
    setDualIOParam(strip, false);

    CompressorModule collapsed; // one Audio jack over raw 0/1
    const auto toFx = GraphEditor::resolvePolyLink(&strip, 0, &collapsed, 0);
    EXPECT_EQ(toFx.voiceCount, 2);
    EXPECT_EQ(toFx.sourceChannel(0), 0);
    EXPECT_EQ(toFx.sourceChannel(1), kR) << "Right leaves kRightBase, never the reserved ch1";
    EXPECT_EQ(toFx.destChannel(0), 0);
    EXPECT_EQ(toFx.destChannel(1), 1);

    MasterModule master; // Mix Left / Right
    const auto toMaster = GraphEditor::resolvePolyLink(&strip, 0, &master, 0);
    EXPECT_EQ(toMaster.voiceCount, 2);
    EXPECT_EQ(toMaster.sourceChannel(1), kR);
    EXPECT_EQ(toMaster.destChannel(1), 1);

    ChannelStripModule bus; // collapsed strip into a collapsed strip: kRightBase to kRightBase
    setDualIOParam(bus, false);
    const auto toStrip = GraphEditor::resolvePolyLink(&strip, 0, &bus, 0);
    EXPECT_EQ(toStrip.sourceChannel(1), kR);
    EXPECT_EQ(toStrip.destChannel(1), kR);

    setDualIOParam(strip, true); // split: the Left jack is one leg, as before
    EXPECT_EQ(GraphEditor::resolvePolyLink(&strip, 1, &master, 1).sourceChannel(0), kR);
}

TEST_F(GraphEditorTest, CollapsingAWiredChannelStripOutputKeepsEveryRawEdgeAndDrawsOneCable) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    auto& graph = engine.getGraph();

    auto stripNode = graph.addNode(std::make_unique<ChannelStripModule>());
    stripNode->properties.set("x", 40);
    stripNode->properties.set("y", 100);
    auto masterNode = graph.addNode(std::make_unique<MasterModule>());
    masterNode->properties.set("x", 640);
    masterNode->properties.set("y", 100);
    setDualIOParam(*masterNode->getProcessor(), false);
    editor.updateComponents();
    sizeModuleComponents(editor);
    ASSERT_TRUE(graph.addConnection({{stripNode->nodeID, 0}, {masterNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{stripNode->nodeID, kR}, {masterNode->nodeID, 1}}));
    EXPECT_EQ(editor.getVisibleCableCount(), 2) << "a split strip into a collapsed Master draws a cable per leg";

    auto* card = stripCardOf(editor, stripNode->getProcessor());
    ASSERT_NE(card, nullptr);
    setDualIOParam(*stripNode->getProcessor(), false);
    card->applyDualIOLayoutChange();
    EXPECT_TRUE(graph.isConnected({{stripNode->nodeID, 0}, {masterNode->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{stripNode->nodeID, kR}, {masterNode->nodeID, 1}}))
        << "the collapsed jack owns kRightBase: the Right cable is not a hidden one to drop";
    EXPECT_EQ(graph.getConnections().size(), 2u);
    EXPECT_EQ(editor.getVisibleCableCount(), 1) << "one Audio jack into one Mix jack is one cable";

    setDualIOParam(*stripNode->getProcessor(), true);
    card->applyDualIOLayoutChange();
    EXPECT_EQ(graph.getConnections().size(), 2u) << "splitting again adds and removes nothing";
    EXPECT_TRUE(graph.isConnected({{stripNode->nodeID, kR}, {masterNode->nodeID, 1}}));
    EXPECT_EQ(editor.getVisibleCableCount(), 2);
}

TEST_F(GraphEditorTest, ACableDraggedFromACollapsedChannelStripOutputWiresBothLegs) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    auto& graph = engine.getGraph();

    auto stripNode = graph.addNode(std::make_unique<ChannelStripModule>());
    setDualIOParam(*stripNode->getProcessor(), false);
    auto compNode = graph.addNode(std::make_unique<CompressorModule>());
    editor.updateComponents();
    editor.connectPorts(stripNode->nodeID, 0, compNode->nodeID, 0, false, false);

    EXPECT_TRUE(graph.isConnected({{stripNode->nodeID, 0}, {compNode->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{stripNode->nodeID, kR}, {compNode->nodeID, 1}}));
    EXPECT_FALSE(graph.isConnected({{stripNode->nodeID, 1}, {compNode->nodeID, 1}})) << "ch1 is reserved";
    EXPECT_EQ(graph.getConnections().size(), 2u);
}

// A Mono-shaped strip is stereo on its output: collapsed, its one Audio jack still carries both legs.
TEST_F(GraphEditorTest, ACollapsedMonoShapedChannelStripFeedsBothLegsFromItsOneOutputJack) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    auto& graph = engine.getGraph();

    auto strip = std::make_unique<ChannelStripModule>();
    ASSERT_TRUE(strip->setShape(ChannelStripModule::Shape::Mono));
    auto stripNode = graph.addNode(std::move(strip));
    setDualIOParam(*stripNode->getProcessor(), false);
    auto compNode = graph.addNode(std::make_unique<CompressorModule>());
    editor.updateComponents();
    editor.connectPorts(stripNode->nodeID, 0, compNode->nodeID, 0, false, false);

    EXPECT_TRUE(graph.isConnected({{stripNode->nodeID, 0}, {compNode->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{stripNode->nodeID, kR}, {compNode->nodeID, 1}}));
}

// Regression test for FRO1230: a collapsed Oscillator or Wavetable showed one Audio jack but a cable made from it
// wired the left leg only, so the Width spread (rendered on the right block) never left the module. Collapsed in
// mono mode the one jack owns both legs, kRightBase apart; a collapsed poly jack still fans 8 left voices.
TEST_F(GraphEditorTest, ACableDraggedFromACollapsedOscillatorOrWavetableOutputWiresBothLegs) {
    struct Case {
        std::unique_ptr<juce::AudioProcessor> src;
        int rightBase;
    };
    std::vector<Case> cases;
    cases.push_back({std::make_unique<OscillatorModule>(), OscillatorModule::kRightBase});
    cases.push_back({std::make_unique<WavetableOscillatorModule>(), WavetableOscillatorModule::kRightBase});
    for (auto& c : cases) {
        AudioEngine engine;
        GraphEditor editor(engine);
        editor.setSize(1200, 700);
        auto& graph = engine.getGraph();
        setDualIOParam(*c.src, false);
        const auto name = c.src->getName();
        auto srcNode = graph.addNode(std::move(c.src));
        auto compNode = graph.addNode(std::make_unique<CompressorModule>());
        editor.updateComponents();
        editor.connectPorts(srcNode->nodeID, 0, compNode->nodeID, 0, false, false);

        EXPECT_TRUE(graph.isConnected({{srcNode->nodeID, 0}, {compNode->nodeID, 0}})) << name;
        EXPECT_TRUE(graph.isConnected({{srcNode->nodeID, c.rightBase}, {compNode->nodeID, 1}}))
            << name << ": the single cable carries the right leg too";
        EXPECT_EQ(graph.getConnections().size(), 2u) << name;
        EXPECT_EQ(editor.getVisibleCableCount(), 1) << name << ": one jack, one drawn cable";
    }
}
