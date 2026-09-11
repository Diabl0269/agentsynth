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
#include "../Source/AudioEngine.h"
#include "../Source/MacroSet.h"
#include "../Source/Mixer/ChannelFlows.h"
#include "../Source/Modules/ChannelStripModule.h"
#include "../Source/Modules/MasterModule.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/ModuleComponent.h"
#include "../Source/UI/ModuleLibraryComponent.h"
#include "MainComponent.h"
#include <algorithm>
#include <array>
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

// T183: flips a module's "poly" AudioParameterBool, when it has one. No-op (returns false) for
// Sampler, which has no poly parameter at all.
bool setPolyParamCFT(juce::AudioProcessor* processor, bool poly) {
    if (processor == nullptr)
        return false;
    for (auto* param : processor->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly") {
                boolParam->setValueNotifyingHost(poly ? 1.0f : 0.0f);
                return true;
            }
    return false;
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

    // T183: the Instrument submenu's headless seam, keyed by module type name rather than the raw
    // menu id — matches how the picker itself is spelled everywhere else in this file.
    static void addInstrumentTrack(MainComponent& mc, const juce::String& instrumentModuleType) {
        int menuId = synth::ui::TimelinePanelComponent::kAddInstrumentSamplerMenuId;
        if (instrumentModuleType == "Oscillator")
            menuId = synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId;
        else if (instrumentModuleType == "Wavetable")
            menuId = synth::ui::TimelinePanelComponent::kAddInstrumentWavetableMenuId;
        mc.getTimelinePanel().applyAddTrackMenuChoice(menuId);
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

// Same pinning pattern as AudioClipPlaybackTest.AbsentFromTheLibraryWithAPinnedSizeEstimate /
// RecordTapTest's own — but for two cards at once, since both were missing an estimateModuleSize
// entry (silently falling back to the generic {280, 360} default, which is wrong for either card
// and was part of why Master ended up hidden under the EQ card — see this file's header comment).
// The strip is measured Stereo, matching what buildDefaultAudioChannel always builds; width does
// not move between Mono/Stereo (only the input jack-row count would), so this also stands in for
// the Mono shape.
TEST_F(ChannelFlowTest, ChannelStripAndMasterHaveAPinnedSizeEstimate) {
    ModuleLibraryComponent library;
    EXPECT_FALSE(library.getDraggableModuleNames().contains("Channel Strip"))
        << "Channel Strip is internal-only and must stay out of the module library";
    EXPECT_FALSE(library.getDraggableModuleNames().contains("Master"))
        << "Master is internal-only and must stay out of the module library";

    AudioEngine engine;
    GraphEditor editor(engine);

    auto stripProcessor = synth::AIStateMapper::createModule("Channel Strip");
    ASSERT_NE(stripProcessor, nullptr);
    if (auto* stripModule = dynamic_cast<ChannelStripModule*>(stripProcessor.get()))
        stripModule->setShape(ChannelStripModule::Shape::Stereo);
    ModuleComponent stripComp(stripProcessor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
    const auto stripEstimate = GraphEditor::estimateModuleSize("Channel Strip");
    EXPECT_EQ(stripEstimate.x, stripComp.getWidth());
    EXPECT_EQ(stripEstimate.y, stripComp.getHeight());

    auto masterProcessor = synth::AIStateMapper::createModule("Master");
    ASSERT_NE(masterProcessor, nullptr);
    ModuleComponent masterComp(masterProcessor.get(), juce::AudioProcessorGraph::NodeID(2), editor);
    const auto masterEstimate = GraphEditor::estimateModuleSize("Master");
    EXPECT_EQ(masterEstimate.x, masterComp.getWidth());
    EXPECT_EQ(masterEstimate.y, masterComp.getHeight());
}

// The bug this file's header comment describes, reproduced against the REAL ModuleComponent
// bounds: on the old fixed-300px stride, Parametric EQ's double-width (560px) card overlapped the
// Compressor, and Master (placed at trackAudioPosition + kSingleWidth + gap, i.e. still inside the
// expanded chain) landed underneath the EQ card too. addAudioTrack now derives every card's x from
// GraphEditor::estimateModuleSize, so none of the five cards below should overlap and Master should
// sit to the right of everything else.
TEST_F(ChannelFlowTest, ChannelCardsDoNotOverlapAndMasterIsRightOfStrip) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(2400, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addAudioTrack(mc);

    ASSERT_EQ(macros.size(), 1);
    const auto macroId = macros.getAll().front().id;

    // Expand the macro through the same API the "Expand" menu item and the collapsed card's own
    // click use (GraphEditor::setMacroCollapsed) so member ModuleComponents are laid out for real
    // (applyMacroCollapsed calls updateComponents()) rather than inferring bounds ourselves.
    mc.getGraphEditor().setMacroCollapsed(macroId, false);
    ASSERT_FALSE(macros.find(macroId)->collapsed);

    auto* trackAudioNode = findNodeOfTypeCFT(graph, ModuleType::TimelineAudioSource);
    auto* eqNode = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* compNode = findNodeOfTypeCFT(graph, ModuleType::Compressor);
    auto* stripNode = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* masterNode = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(trackAudioNode, nullptr);
    ASSERT_NE(eqNode, nullptr);
    ASSERT_NE(compNode, nullptr);
    ASSERT_NE(stripNode, nullptr);
    ASSERT_NE(masterNode, nullptr);

    auto findComp = [&mc](juce::AudioProcessorGraph::Node* node) -> ModuleComponent* {
        for (auto* comp : mc.getGraphEditor().getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == node->nodeID)
                return comp;
        return nullptr;
    };

    auto* trackAudioComp = findComp(trackAudioNode);
    auto* eqComp = findComp(eqNode);
    auto* compComp = findComp(compNode);
    auto* stripComp = findComp(stripNode);
    auto* masterComp = findComp(masterNode);
    // All five nodes are ordinary graph nodes with their own ModuleComponent (a hidden macro
    // member's component still exists — only setVisible(false) — and expanding just flips that
    // back on), so every lookup above must resolve; a silent nullptr here would make the
    // assertions below pass vacuously.
    ASSERT_NE(trackAudioComp, nullptr) << "Track Audio must have a real ModuleComponent once expanded";
    ASSERT_NE(eqComp, nullptr) << "Parametric EQ must have a real ModuleComponent once expanded";
    ASSERT_NE(compComp, nullptr) << "Compressor must have a real ModuleComponent once expanded";
    ASSERT_NE(stripComp, nullptr) << "Channel Strip must have a real ModuleComponent once expanded";
    ASSERT_NE(masterComp, nullptr) << "Master must have a real ModuleComponent (it is never boxed into the macro)";

    // Anchor the coordinate space once: content-component bounds should track the node's own
    // "x"/"y" properties directly (no zoom/scroll in a freshly-built headless MainComponent), so a
    // mismatch here means the two are in different coordinate spaces rather than a real overlap.
    EXPECT_EQ(trackAudioComp->getX(), static_cast<int>(trackAudioNode->properties.getWithDefault("x", -1)));
    EXPECT_EQ(trackAudioComp->getY(), static_cast<int>(trackAudioNode->properties.getWithDefault("y", -1)));

    const std::array<ModuleComponent*, 5> cards = {trackAudioComp, eqComp, compComp, stripComp, masterComp};
    for (size_t i = 0; i < cards.size(); ++i) {
        for (size_t j = i + 1; j < cards.size(); ++j) {
            EXPECT_FALSE(cards[i]->getBounds().intersects(cards[j]->getBounds()))
                << "card " << i << " " << cards[i]->getBounds().toString().toStdString() << " overlaps card " << j
                << " " << cards[j]->getBounds().toString().toStdString();
        }
    }

    EXPECT_LT(trackAudioComp->getX(), eqComp->getX());
    EXPECT_LT(eqComp->getX(), compComp->getX());
    EXPECT_LT(compComp->getX(), stripComp->getX());
    EXPECT_LT(stripComp->getX(), masterComp->getX());
    EXPECT_GE(masterComp->getX(), stripComp->getRight()) << "Master must be fully clear of the Strip card";
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

// ---------------------------------------------------------------------------------------------
// T183 (P9-3b): "+ Track -> Instrument -> {Oscillator/Wavetable/Sampler}" builds
//
//     Track In -> instrument -> Parametric EQ (bypassed) -> Compressor (bypassed)
//              -> Channel Strip (Stereo) -> Master (Mix)
//
// as ONE undo step, with {Track In, instrument, EQ, Compressor, Strip} boxed into one collapsed
// macro named after the track — the MIDI-track mirror of the Audio Track tests above. See
// MainComponent::addInstrumentTrack's own comment for why this stays a TrackKind::Midi track
// rather than a new TrackKind, and Source/Mixer/ChannelFlows.h for the poly/Voice Mixer contract.
// ---------------------------------------------------------------------------------------------

TEST_F(ChannelFlowTest, InstrumentTrackSamplerBuildsDefaultChannelDirectlyOnAContiguousPair) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    addInstrumentTrack(mc, "Sampler");

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::TimelineMidiSource), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Sampler), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0)
        << "Sampler is a contiguous stereo pair — no Voice Mixer needed";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ParametricEQ), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Compressor), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);

    auto* trackIn = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    auto* sampler = findNodeOfTypeCFT(graph, ModuleType::Sampler);
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    ASSERT_NE(trackIn, nullptr);
    ASSERT_NE(sampler, nullptr);
    ASSERT_NE(eq, nullptr);

    EXPECT_TRUE(graph.isConnected({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {sampler->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));

    auto* samplerModule = dynamic_cast<ModuleBase*>(sampler->getProcessor());
    ASSERT_NE(samplerModule, nullptr);
    ASSERT_EQ(samplerModule->rightAudioLegChannel(), 1) << "Sampler's legs ARE the contiguous ch0/ch1 pair";
    EXPECT_TRUE(graph.isConnected({{sampler->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{sampler->nodeID, 1}, {eq->nodeID, 1}}));
}

TEST_F(ChannelFlowTest, InstrumentTrackOscillatorWiresSplitBlockRightLegNeverCh1) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    addInstrumentTrack(mc, "Oscillator");

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0)
        << "a freshly created Oscillator defaults to poly OFF — no Voice Mixer needed";

    auto* oscillator = findNodeOfTypeCFT(graph, ModuleType::Oscillator);
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    ASSERT_NE(oscillator, nullptr);
    ASSERT_NE(eq, nullptr);

    auto* oscModule = dynamic_cast<ModuleBase*>(oscillator->getProcessor());
    ASSERT_NE(oscModule, nullptr);
    const int rightLeg = oscModule->rightAudioLegChannel();
    ASSERT_GT(rightLeg, 1) << "Oscillator's right leg is a dedicated kRightBase block, never ch1";

    EXPECT_TRUE(graph.isConnected({{oscillator->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{oscillator->nodeID, rightLeg}, {eq->nodeID, 1}}));
    EXPECT_FALSE(graph.isConnected({{oscillator->nodeID, 1}, {eq->nodeID, 1}}))
        << "must never assume ch1 for a split-block source";
}

TEST_F(ChannelFlowTest, InstrumentTrackDefaultInsertsAreBypassedAndStripIsStereo) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    addInstrumentTrack(mc, "Sampler");

    auto* eqModule = dynamic_cast<ModuleBase*>(findNodeOfTypeCFT(graph, ModuleType::ParametricEQ)->getProcessor());
    auto* compModule = dynamic_cast<ModuleBase*>(findNodeOfTypeCFT(graph, ModuleType::Compressor)->getProcessor());
    auto* stripModule =
        dynamic_cast<ChannelStripModule*>(findNodeOfTypeCFT(graph, ModuleType::ChannelStrip)->getProcessor());
    ASSERT_NE(eqModule, nullptr);
    ASSERT_NE(compModule, nullptr);
    ASSERT_NE(stripModule, nullptr);

    EXPECT_TRUE(eqModule->isBypassed());
    EXPECT_TRUE(compModule->isBypassed());
    EXPECT_FALSE(stripModule->isBypassed());
    EXPECT_EQ(stripModule->getShape(), ChannelStripModule::Shape::Stereo);
}

TEST_F(ChannelFlowTest, InstrumentTrackIsOneCollapsedMacroNamedAfterTrackAndStaysMidiKind) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Sampler");

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    ASSERT_EQ(doc.getTracks().size(), 1u);
    const auto& track = doc.getTracks().back();

    EXPECT_EQ(macro.name, track.name);
    EXPECT_TRUE(macro.collapsed);
    // T183's scope decision: an instrument track is TrackKind::Midi (a Track In feeding exactly
    // one instrument), not a new TrackKind — see MainComponent::addInstrumentTrack's own comment.
    EXPECT_EQ(track.kind, synth::TrackKind::Midi);
    EXPECT_EQ(track.bindingUuid, nodeUuid(findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource)));

    std::vector<juce::String> expected = {nodeUuid(findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Sampler)),
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

TEST_F(ChannelFlowTest, InstrumentTrackOneUndoStepRevertsEverythingAndRedoRestores) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& macros = mc.getGraphEditor().getMacros();

    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docBefore = juce::JSON::toString(doc.toVar());
    const juce::String macrosBefore = juce::JSON::toString(macros.toVar());

    addInstrumentTrack(mc, "Oscillator");
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

// The Wavetable card and the Parametric EQ card are BOTH double-width — the exact overlap shape
// P9-3a's own bug (see this file's header comment) reproduced for, now one node to the left of
// where it was. Reuses ChannelCardsDoNotOverlapAndMasterIsRightOfStrip's real-ModuleComponent
// approach rather than inferring bounds from positions.
TEST_F(ChannelFlowTest, InstrumentTrackWavetableCardsDoNotOverlap) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(2600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Wavetable");

    ASSERT_EQ(macros.size(), 1);
    const auto macroId = macros.getAll().front().id;
    mc.getGraphEditor().setMacroCollapsed(macroId, false);
    ASSERT_FALSE(macros.find(macroId)->collapsed);

    auto* trackInNode = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    auto* wavetableNode = findNodeOfTypeCFT(graph, ModuleType::Wavetable);
    auto* eqNode = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* compNode = findNodeOfTypeCFT(graph, ModuleType::Compressor);
    auto* stripNode = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* masterNode = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(trackInNode, nullptr);
    ASSERT_NE(wavetableNode, nullptr);
    ASSERT_NE(eqNode, nullptr);
    ASSERT_NE(compNode, nullptr);
    ASSERT_NE(stripNode, nullptr);
    ASSERT_NE(masterNode, nullptr);

    auto findComp = [&mc](juce::AudioProcessorGraph::Node* node) -> ModuleComponent* {
        for (auto* comp : mc.getGraphEditor().getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == node->nodeID)
                return comp;
        return nullptr;
    };

    const std::array<ModuleComponent*, 6> cards = {findComp(trackInNode), findComp(wavetableNode),
                                                   findComp(eqNode),      findComp(compNode),
                                                   findComp(stripNode),   findComp(masterNode)};
    for (auto* card : cards)
        ASSERT_NE(card, nullptr) << "every macro member must have a real ModuleComponent once expanded";

    for (size_t i = 0; i < cards.size(); ++i)
        for (size_t j = i + 1; j < cards.size(); ++j)
            EXPECT_FALSE(cards[i]->getBounds().intersects(cards[j]->getBounds()))
                << "card " << i << " " << cards[i]->getBounds().toString().toStdString() << " overlaps card " << j
                << " " << cards[j]->getBounds().toString().toStdString();

    for (size_t i = 0; i + 1 < cards.size(); ++i)
        EXPECT_LT(cards[i]->getX(), cards[i + 1]->getX());
}

TEST_F(ChannelFlowTest, InstrumentTrackRefusedAtMaxTracksCreatesNothing) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& doc = mc.getTimelineDoc();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    while ((int)doc.getTracks().size() < synth::TimelineDoc::kMaxTracks)
        ASSERT_TRUE(doc.addTrack(synth::TrackKind::Midi, "Filler").isValid());
    ASSERT_FALSE(mc.getUndoManager().canUndo());
    const int nodesBefore = graph.getNumNodes();

    addInstrumentTrack(mc, "Sampler");

    EXPECT_EQ((int)doc.getTracks().size(), synth::TimelineDoc::kMaxTracks);
    EXPECT_EQ(graph.getNumNodes(), nodesBefore) << "a refused instrument track must leave no orphan node";
    EXPECT_EQ(macros.size(), 0) << "a refused instrument track must leave no macro";
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "nothing changed in any domain: no undo step";
}

// synth::addVoiceMixerForPolyInstrument / the poly branch of MainComponent::addInstrumentTrack's
// chain-source selection, exercised directly at the ChannelFlows level: a factory-default
// Oscillator is poly OFF (see InstrumentTrackOscillatorWiresSplitBlockRightLegNeverCh1 above), so
// the golden "+ Track -> Instrument" path never takes this branch today — this proves it wires
// correctly for whenever an instrument IS poly (docs/mixer.md §5.4/§5.8).
TEST_F(ChannelFlowTest, PolyInstrumentGetsVoiceMixerAheadOfStripAndFeedsTheChannel) {
    AudioEngine engine;
    auto& graph = engine.getGraph();

    auto oscProcessor = synth::AIStateMapper::createModule("Oscillator");
    ASSERT_NE(oscProcessor, nullptr);
    ASSERT_TRUE(setPolyParamCFT(oscProcessor.get(), true));
    auto oscNode = graph.addNode(std::move(oscProcessor));
    ASSERT_NE(oscNode, nullptr);

    juce::String voiceMixerUuid;
    auto* voiceMixer = synth::addVoiceMixerForPolyInstrument(graph, *oscNode, {0, 0}, voiceMixerUuid);
    ASSERT_NE(voiceMixer, nullptr);
    EXPECT_FALSE(voiceMixerUuid.isEmpty());
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 1);

    for (int voice = 0; voice < 8; ++voice)
        EXPECT_TRUE(graph.isConnected({{oscNode->nodeID, voice}, {voiceMixer->nodeID, voice}}))
            << "voice " << voice << " must be summed into the Voice Mixer";

    const synth::DefaultChannelLayout layout{{100, 0}, {200, 0}, {300, 0}, {400, 0}};
    const auto channel = synth::buildDefaultAudioChannel(graph, *voiceMixer, layout);
    ASSERT_FALSE(channel.stripUuid.isEmpty());

    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    ASSERT_NE(eq, nullptr);
    EXPECT_TRUE(graph.isConnected({{voiceMixer->nodeID, 0}, {eq->nodeID, 0}}))
        << "Voice Mixer's own ch0/ch1 output satisfies buildDefaultAudioChannel's default contiguous-pair contract";
    EXPECT_TRUE(graph.isConnected({{voiceMixer->nodeID, 1}, {eq->nodeID, 1}}));
}

TEST_F(ChannelFlowTest, NonPolyInstrumentGetsNoVoiceMixer) {
    AudioEngine engine;
    auto& graph = engine.getGraph();

    auto oscProcessor = synth::AIStateMapper::createModule("Oscillator");
    ASSERT_NE(oscProcessor, nullptr);
    // Factory default: poly OFF — no setPolyParamCFT call.
    auto oscNode = graph.addNode(std::move(oscProcessor));
    ASSERT_NE(oscNode, nullptr);

    juce::String voiceMixerUuid;
    auto* voiceMixer = synth::addVoiceMixerForPolyInstrument(graph, *oscNode, {0, 0}, voiceMixerUuid);
    EXPECT_EQ(voiceMixer, nullptr);
    EXPECT_TRUE(voiceMixerUuid.isEmpty());
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0);
}
