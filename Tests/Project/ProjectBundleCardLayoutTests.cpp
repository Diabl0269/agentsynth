// ProjectBundleCardLayoutTests.cpp -- FRO137: a plugin-card layout override survives a full
// project save/load, exactly like the rest of a HostedPluginModule's extra state (its plugin
// identity + state blob). ProjectBundle::save/load always apply on the trusted path (it is the
// user's own project file, not untrusted model output -- see the class's own doc comment), so
// there is no cardLayout-specific code here either: this proves the general mechanism, mirroring
// SnippetManagerCardLayoutTests.cpp's trusted-insert case.

#include "../StubPluginInstance.h"
#include "Modules/CardLayout.h"
#include "PatchDocument.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <chrono>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using synth::HostedPluginModule;
using synth::MacroSet;
using synth::MidiRemoteProjectDoc;
using synth::ProjectBundle;
using synth::TimelineDoc;
using synth::test::StubBackend;
using synth::test::StubParamSpec;
using synth::test::StubPluginInstance;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

juce::PluginDescription stubDescription() {
    juce::PluginDescription description;
    description.name = "Bundle Plugin";
    description.pluginFormatName = "VST3";
    description.uniqueId = 0xC0DE05;
    description.deprecatedUid = 0xC0DE05;
    description.fileOrIdentifier = "/nonexistent/test/path/BundlePlugin.vst3";
    return description;
}

class ProjectBundleCardLayoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("agentsynth-projectbundle-cardlayout-tests");
        root_.deleteRecursively();
        root_.createDirectory();
    }
    void TearDown() override { root_.deleteRecursively(); }

    juce::File root_;
    StubBackend backend_;
};

} // namespace

TEST_F(ProjectBundleCardLayoutTest, CardLayoutOverrideSurvivesASaveLoadRoundTrip) {
    juce::AudioProcessorGraph graph;
    backend_.setFactory([]() -> std::unique_ptr<StubPluginInstance> {
        return std::make_unique<StubPluginInstance>(2, 2, "Bundle Plugin", 0xC0DE05, "VST3",
                                                    std::vector<StubParamSpec>{{"cutoff", "Cutoff", 0.0f, {}, false}});
    });
    auto module = std::make_unique<HostedPluginModule>();
    auto* hosted = module.get();
    auto node = graph.addNode(std::move(module));
    node->properties.set("x", 0);
    node->properties.set("y", 0);
    hosted->prepareToPlay(kSampleRate, kBlockSize);
    hosted->loadPlugin(stubDescription(), backend_);
    ASSERT_TRUE(pumpUntil([hosted] { return hosted->hasInstance(); }));

    synth::CardLayout layout;
    synth::CardSlot slot;
    slot.paramId = "cutoff";
    layout.slots.push_back(slot);
    hosted->setCardLayoutOverride(layout.toVar());

    TimelineDoc timeline;
    synth::PatchDocument patchDocument;
    MacroSet macros;
    MidiRemoteProjectDoc midiRemote;
    auto dir = root_.getChildFile(juce::String("CardLayout") + ProjectBundle::kBundleExtension);
    ASSERT_TRUE(ProjectBundle::save(dir, graph, timeline, patchDocument, macros, midiRemote).ok);

    juce::AudioProcessorGraph freshGraph;
    TimelineDoc freshTimeline;
    synth::PatchDocument freshPatchDoc;
    MacroSet freshMacros;
    MidiRemoteProjectDoc freshMidiRemote;
    ASSERT_TRUE(ProjectBundle::load(dir, freshGraph, freshTimeline, freshPatchDoc, freshMacros, freshMidiRemote).ok);

    ASSERT_EQ(freshGraph.getNumNodes(), 1);
    auto* freshModule = dynamic_cast<HostedPluginModule*>(freshGraph.getNodes()[0]->getProcessor());
    ASSERT_NE(freshModule, nullptr);
    ASSERT_FALSE(freshModule->getCardLayoutOverride().isVoid());

    const auto parsed = synth::CardLayout::fromVar(freshModule->getCardLayoutOverride());
    ASSERT_EQ(parsed.status, synth::CardLayout::ParseStatus::Ok);
    ASSERT_EQ(parsed.layout.slots.size(), 1u);
    EXPECT_EQ(parsed.layout.slots[0].paramId, "cutoff");
}
