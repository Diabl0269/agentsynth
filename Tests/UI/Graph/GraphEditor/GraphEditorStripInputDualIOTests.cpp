// GraphEditor tests for the Channel Strip's INPUT side of Dual I/O: collapsed, the input is one Audio jack over raw
// ch0 and kRightBase, so a cable into it feeds both legs and collapsing a wired stereo strip keeps them (the output
// side is in GraphEditorStripOutputDualIOTests.cpp). Shared GraphEditorTest fixture: GraphEditorTestHelpers.h.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"

#include "Modules/ChannelStripModule.h"
#include "Modules/FX/CompressorModule.h"
#include "Modules/OscillatorModule.h"

namespace {
ModuleComponent* stripInputCardFor(GraphEditor& editor, juce::AudioProcessor* proc) {
    for (auto* mc : editor.getModuleComponents())
        if (mc != nullptr && mc->getModule() == proc)
            return mc;
    return nullptr;
}
} // namespace

TEST_F(GraphEditorTest, ResolvePolyLinkFeedsBothLegsOfACollapsedChannelStripJack) {
    ChannelStripModule strip;
    setDualIOParam(strip, false);
    constexpr int kR = ChannelStripModule::kRightBase;

    OscillatorModule mono;
    const auto fromMono = GraphEditor::resolvePolyLink(&mono, 0, &strip, 0);
    EXPECT_EQ(fromMono.voiceCount, 2) << "a mono cable feeds both legs, like a collapsed FX jack";
    EXPECT_EQ(fromMono.sourceStride, 0);
    EXPECT_EQ(fromMono.destChannel(0), 0);
    EXPECT_EQ(fromMono.destChannel(1), kR);

    CompressorModule collapsed; // one Audio jack over raw 0/1
    const auto fromPair = GraphEditor::resolvePolyLink(&collapsed, 0, &strip, 0);
    EXPECT_EQ(fromPair.voiceCount, 2);
    EXPECT_EQ(fromPair.sourceStride, 1);
    EXPECT_EQ(fromPair.destChannel(0), 0);
    EXPECT_EQ(fromPair.destChannel(1), kR) << "Right lands on kRightBase, never on the reserved ch1";

    setDualIOParam(strip, true);
    const auto split = GraphEditor::resolvePolyLink(&collapsed, 0, &strip, 0);
    EXPECT_EQ(split.destChannel(1), kR) << "a split strip's Left jack also takes the whole pair";
}

TEST_F(GraphEditorTest, CollapsingAWiredStereoChannelStripKeepsBothLegsAndDrawsOneCable) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    auto& graph = engine.getGraph();
    constexpr int kR = ChannelStripModule::kRightBase;

    auto compNode = graph.addNode(std::make_unique<CompressorModule>());
    compNode->properties.set("x", 40);
    compNode->properties.set("y", 100);
    auto stripNode = graph.addNode(std::make_unique<ChannelStripModule>());
    stripNode->properties.set("x", 640);
    stripNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);
    ASSERT_TRUE(graph.addConnection({{compNode->nodeID, 0}, {stripNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{compNode->nodeID, 1}, {stripNode->nodeID, kR}}));
    const int cablesSplit = editor.getVisibleCableCount();

    auto* card = stripInputCardFor(editor, stripNode->getProcessor());
    ASSERT_NE(card, nullptr);
    setDualIOParam(*stripNode->getProcessor(), false);
    card->applyDualIOLayoutChange();

    EXPECT_TRUE(graph.isConnected({{compNode->nodeID, 0}, {stripNode->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{compNode->nodeID, 1}, {stripNode->nodeID, kR}}))
        << "the collapsed jack owns kRightBase: the Right cable is not a hidden one to drop";
    EXPECT_EQ(cablesSplit, 2) << "a collapsed source into a split strip draws a cable per leg";
    EXPECT_EQ(editor.getVisibleCableCount(), 1) << "collapsing the strip too leaves one collapsed cable";

    setDualIOParam(*stripNode->getProcessor(), true);
    card->applyDualIOLayoutChange();
    EXPECT_TRUE(graph.isConnected({{compNode->nodeID, 0}, {stripNode->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{compNode->nodeID, 1}, {stripNode->nodeID, kR}}));
}

TEST_F(GraphEditorTest, TheNewModuleDualIOPreferenceReachesAChannelStrip) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ChannelStripModule strip;
    editor.setDefaultDualIOForNewModules(false);
    editor.applyDualIODefaultTo(strip, "Channel Strip");
    EXPECT_FALSE(strip.isDualIO());
    EXPECT_EQ(strip.getVisibleInputPortCount(), 1);

    editor.setDefaultDualIOForNewModules(true);
    editor.setDualIOPerModuleOverrides({{"Channel Strip", false}});
    editor.applyDualIODefaultTo(strip, "Channel Strip");
    EXPECT_FALSE(strip.isDualIO()) << "the per-module override wins over the global default";

    editor.setDualIOPerModuleOverrides({});
    editor.applyDualIODefaultTo(strip, "Channel Strip");
    EXPECT_TRUE(strip.isDualIO());
    EXPECT_EQ(strip.getVisibleInputPortCount(), 2);

    // A Mono strip has one jack either way.
    ChannelStripModule mono;
    ASSERT_TRUE(mono.setShape(ChannelStripModule::Shape::Mono));
    editor.setDefaultDualIOForNewModules(true);
    editor.applyDualIODefaultTo(mono, "Channel Strip");
    EXPECT_EQ(mono.getVisibleInputPortCount(), 1);
}
