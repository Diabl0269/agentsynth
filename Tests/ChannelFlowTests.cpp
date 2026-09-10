// ChannelFlowTests.cpp
//
// T173a: "+ Track -> Audio Track" now builds a WHOLE mixer channel in one undo step —
//
//     Track Audio -> Parametric EQ (bypassed) -> Compressor (bypassed) -> Channel Strip (Stereo)
//                 -> Master (Mix)
//
// with {Track Audio, EQ, Compressor, Strip} boxed into ONE collapsed macro named after the track.
// Master stays OUTSIDE the macro and the Strip -> Master cable is a plain graph edge, never a macro
// port — see MainComponent::addAudioTrack's own comment and Source/Mixer/ChannelFlows.h for why.
//
// Drives the flow through a real MainComponent via the same headless seam
// AudioClipPlaybackTests.cpp's AddAudioTrackFlowTest uses (TimelinePanelComponent's
// applyAddTrackMenuChoice), so these tests exercise the whole app wiring, not just
// synth::buildDefaultAudioChannel in isolation.

#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AIStateMapper.h"
#include "../Source/MacroSet.h"
#include "../Source/Modules/ChannelStripModule.h"
#include "../Source/Modules/MasterModule.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "MainComponent.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <memory>

namespace {

// Same minimal pattern as AudioClipPlaybackTests.cpp's MockProviderACP — a unique name of its own
// to avoid an ODR clash across test translation units.
class MockProviderCFT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockCFT"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response.";
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

int countNodesOfTypeCFT(juce::AudioProcessorGraph& graph, ModuleType type) {
    int count = 0;
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                if (module->getModuleType() == type)
                    ++count;
    return count;
}

// The type is a singleton at the point every test here calls it (checked separately by the
// count-based assertions), so "the last one seen" is unambiguous.
juce::AudioProcessorGraph::Node* findNodeOfTypeCFT(juce::AudioProcessorGraph& graph, ModuleType type) {
    juce::AudioProcessorGraph::Node* found = nullptr;
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                if (module->getModuleType() == type)
                    found = node;
    return found;
}

juce::AudioProcessorGraph::Node* findNodeNamedCFT(juce::AudioProcessorGraph& graph, const juce::String& name) {
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->getProcessor() != nullptr && node->getProcessor()->getName() == name)
            return node;
    return nullptr;
}

juce::String nodeUuid(juce::AudioProcessorGraph::Node* node) {
    return node != nullptr ? node->properties["uuid"].toString() : juce::String();
}

} // namespace

class ChannelFlowTest : public ::testing::Test {
protected:
    // Same settings-file hygiene as AudioClipPlaybackTests.cpp's AddAudioTrackFlowTest /
    // RecordTapTests.cpp / TimelinePanelTests.cpp: the delegating MainComponent ctor reads/writes
    // the shared on-disk "Agent Synth" settings, so pin the keys this flow depends on before AND
    // after every test.
    void resetKeys() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("librarySidebarVisible", "1");
            s->setValue("aiPanelVisible", "0");
            s->setValue("minimapVisible", "1");
            s->setValue("timelinePanelVisible", "0");
            s->saveIfNeeded();
        }
    }

    void SetUp() override { resetKeys(); }
    void TearDown() override { resetKeys(); }

    // The menu hook, not the async PopupMenu — the same headless seam the binding chip uses.
    static void addAudioTrack(MainComponent& mc) {
        mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddAudioTrackMenuId);
    }
};

TEST_F(ChannelFlowTest, AudioTrackBuildsDefaultChannel) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    addAudioTrack(mc);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::TimelineAudioSource), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ParametricEQ), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Compressor), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);

    auto* trackAudio = findNodeOfTypeCFT(graph, ModuleType::TimelineAudioSource);
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* comp = findNodeOfTypeCFT(graph, ModuleType::Compressor);
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(trackAudio, nullptr);
    ASSERT_NE(eq, nullptr);
    ASSERT_NE(comp, nullptr);
    ASSERT_NE(strip, nullptr);
    ASSERT_NE(master, nullptr);

    EXPECT_TRUE(graph.isConnected({{trackAudio->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{trackAudio->nodeID, 1}, {eq->nodeID, 1}}));
    EXPECT_TRUE(graph.isConnected({{eq->nodeID, 0}, {comp->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{eq->nodeID, 1}, {comp->nodeID, 1}}));
    EXPECT_TRUE(graph.isConnected({{comp->nodeID, 0}, {strip->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{comp->nodeID, 1}, {strip->nodeID, ChannelStripModule::kRightBase}}))
        << "the right leg must land on kRightBase (4)";
    EXPECT_FALSE(graph.isConnected({{comp->nodeID, 1}, {strip->nodeID, 1}})) << "never ch1 for the right leg";

    EXPECT_TRUE(graph.isConnected({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(graph.isConnected(
        {{strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}));

    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    EXPECT_TRUE(graph.isConnected({{master->nodeID, 0}, {output->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{master->nodeID, 1}, {output->nodeID, 1}}));

    EXPECT_FALSE(graph.isConnected({{trackAudio->nodeID, 0}, {output->nodeID, 0}}))
        << "Track Audio must no longer wire straight to the output";
    EXPECT_FALSE(graph.isConnected({{trackAudio->nodeID, 1}, {output->nodeID, 1}}));
}

TEST_F(ChannelFlowTest, DefaultInsertsAreBypassedAndStripIsStereo) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    addAudioTrack(mc);

    auto* eqNode = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* compNode = findNodeOfTypeCFT(graph, ModuleType::Compressor);
    auto* stripNode = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(eqNode, nullptr);
    ASSERT_NE(compNode, nullptr);
    ASSERT_NE(stripNode, nullptr);

    auto* eqModule = dynamic_cast<ModuleBase*>(eqNode->getProcessor());
    auto* compModule = dynamic_cast<ModuleBase*>(compNode->getProcessor());
    auto* stripModule = dynamic_cast<ChannelStripModule*>(stripNode->getProcessor());
    ASSERT_NE(eqModule, nullptr);
    ASSERT_NE(compModule, nullptr);
    ASSERT_NE(stripModule, nullptr);

    EXPECT_TRUE(eqModule->isBypassed());
    EXPECT_TRUE(compModule->isBypassed());
    EXPECT_FALSE(stripModule->isBypassed());
    EXPECT_EQ(stripModule->getShape(), ChannelStripModule::Shape::Stereo);
}

TEST_F(ChannelFlowTest, ChannelIsOneCollapsedMacroNamedAfterTrack) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& macros = mc.getGraphEditor().getMacros();

    addAudioTrack(mc);

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    ASSERT_EQ(doc.getTracks().size(), 1u);
    const auto& track = doc.getTracks().back();

    EXPECT_EQ(macro.name, track.name);
    EXPECT_TRUE(macro.collapsed);
    EXPECT_EQ(track.bindingUuid, nodeUuid(findNodeOfTypeCFT(graph, ModuleType::TimelineAudioSource)));

    std::vector<juce::String> expected = {nodeUuid(findNodeOfTypeCFT(graph, ModuleType::TimelineAudioSource)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ParametricEQ)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Compressor)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ChannelStrip))};
    auto actual = macro.members;
    std::sort(expected.begin(), expected.end());
    std::sort(actual.begin(), actual.end());
    EXPECT_EQ(actual, expected);

    EXPECT_FALSE(macro.hasMember(nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Master))))
        << "Master must stay outside the macro";
}

TEST_F(ChannelFlowTest, OneUndoStepRevertsEverythingAndRedoRestores) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& macros = mc.getGraphEditor().getMacros();

    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docBefore = juce::JSON::toString(doc.toVar());
    const juce::String macrosBefore = juce::JSON::toString(macros.toVar());

    addAudioTrack(mc);
    ASSERT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1) << "the channel must have been built";
    const juce::String graphAfter = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docAfter = juce::JSON::toString(doc.toVar());
    const juce::String macrosAfter = juce::JSON::toString(macros.toVar());

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docBefore);
    EXPECT_EQ(juce::JSON::toString(macros.toVar()), macrosBefore);
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "the whole channel was ONE undo step";

    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphAfter);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docAfter);
    EXPECT_EQ(juce::JSON::toString(macros.toVar()), macrosAfter);
}

TEST_F(ChannelFlowTest, SecondAudioTrackReusesMaster) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addAudioTrack(mc);
    addAudioTrack(mc);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1) << "Master is a singleton";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 2);
    EXPECT_EQ(macros.size(), 2);

    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(master, nullptr);
    int stripsIntoMix = 0;
    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        auto* module = dynamic_cast<ModuleBase*>(node->getProcessor());
        if (module == nullptr || module->getModuleType() != ModuleType::ChannelStrip)
            continue;
        if (graph.isConnected({{node->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}) &&
            graph.isConnected(
                {{node->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}))
            ++stripsIntoMix;
    }
    EXPECT_EQ(stripsIntoMix, 2) << "both strips must land on Mix";

    // One undo removes only the SECOND channel; Master (and the first channel) remain.
    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);
    EXPECT_EQ(macros.size(), 1);
}

TEST_F(ChannelFlowTest, RefusedAtMaxTracksCreatesNothing) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& doc = mc.getTimelineDoc();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    // Fixture setup goes straight through the doc, not the undo-recording flow, so it pushes no
    // undo step of its own — see TimelinePanelTests.cpp's AddTrackAtTheCapAddsNoNode for the same
    // pattern.
    while ((int)doc.getTracks().size() < synth::TimelineDoc::kMaxTracks)
        ASSERT_TRUE(doc.addTrack(synth::TrackKind::Midi, "Filler").isValid());
    ASSERT_FALSE(mc.getUndoManager().canUndo());
    const int nodesBefore = graph.getNumNodes();

    addAudioTrack(mc);

    EXPECT_EQ((int)doc.getTracks().size(), synth::TimelineDoc::kMaxTracks);
    EXPECT_EQ(graph.getNumNodes(), nodesBefore) << "a refused audio track must leave no orphan node";
    EXPECT_EQ(macros.size(), 0) << "a refused audio track must leave no macro";
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "nothing changed in any domain: no undo step";
}
