// Concern: (docs/mixer/track-presets.md#a-third-kind-bus) -- the third TrackPresetKind, Bus:
// a bus's channel macro (no bound timeline track) saved and re-inserted with its effects/params
// intact, still classifying as a bus (synth::isBusStrip) on the far side even though the scrubbed
// saved JSON carries no "isBus"/"sends" (TrackPresetManager.cpp's own scrub, unchanged by this
// ticket -- docs/mixer/track-presets.md#scrubbed-keys), listTrackPresets grouping, and the "+ Track"
// menu's own Bus submenu (TimelinePanelTrackHeaders.cpp), driven through the same real
// menu-result handler (TimelinePanelComponent::applyAddTrackMenuChoice) the Audio/Instrument menu
// tests use, not TrackPresetManager internals directly.

#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../ChannelFlow/ChannelFlowTestFixture.h"
#include "TrackPresetTestFixture.h"

#include <gtest/gtest.h>

namespace {

constexpr const char* kBusPresetName = "__FRO297_Test_Bus_Preset__";

// Same "stub everything else inert" posture MenuStubHostMFT (TrackPresetMenuTests.cpp) takes --
// duplicated rather than shared across translation units, matching that file's own posture.
class BusMenuStubHostFBT : public synth::ui::TrackHeaderHost {
public:
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(synth::TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(synth::TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(synth::TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }

    void addBusFromPreset(const juce::String& presetName) override {
        ++addBusFromPresetCalls;
        lastPresetName = presetName;
    }

    int addBusFromPresetCalls = 0;
    juce::String lastPresetName;
};

} // namespace

class TrackPresetBusTest : public MainComponentTest {
protected:
    void deleteTestPreset() {
        synth::TrackPresetManager::deleteTrackPreset(synth::TrackPresetManager::getDefaultTrackPresetsDirectory(),
                                                     kBusPresetName);
    }
    void SetUp() override {
        MainComponentTest::SetUp();
        deleteTestPreset();
    }
    void TearDown() override {
        deleteTestPreset();
        MainComponentTest::TearDown();
    }
};

// ---- Round trip: extract + insert ----

TEST(TrackPresetBus, RoundTripPreservesEffectsAndClassifiesAsBus) {
    HostedPatchCFT reference;
    GraphEditor referenceEditor(reference.engine);
    const auto rig = buildSimpleBusRigCFT(referenceEditor, reference.engine, "Reverb Bus");
    ASSERT_NE(rig.macro, nullptr);
    ASSERT_NE(rig.strip, nullptr);
    auto* stripModule = dynamic_cast<ChannelStripModule*>(rig.strip->getProcessor());
    ASSERT_NE(stripModule, nullptr);
    ASSERT_TRUE(stripModule->isBus()); // buildBusChannel already flags it, same as "Add bus"

    // A real setting to prove the effect chain's PARAMETERS round-trip too, not just extra state --
    // shape can't be used for this (ChannelStripModule::setShape locks after channel creation, same
    // as any other track preset's strip), so this drives the strip's own "gain" AudioParameterFloat
    // instead, the same way a saved fader position must survive.
    juce::AudioProcessorParameterWithID* gainParam = nullptr;
    for (auto* p : stripModule->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p);
            withId != nullptr && withId->paramID == "gain")
            gainParam = withId;
    ASSERT_NE(gainParam, nullptr);
    gainParam->setValueNotifyingHost(0.75f);

    auto preset = synth::TrackPresetManager::extractTrackPreset(
        reference.engine.getGraph(), referenceEditor.getMacros(), rig.macro->id, synth::TrackPresetKind::Bus, "RTBus");
    ASSERT_TRUE(preset.isObject());
    EXPECT_EQ(synth::TrackPresetManager::getPresetKind(preset), synth::TrackPresetKind::Bus);

    // Scrub still applies to a Bus-kind capture exactly as it does for Audio/Instrument
    // (docs/mixer/track-presets.md#scrubbed-keys) -- the saved JSON must carry no "isBus"/"sends"
    // regardless of the strip's own live isBus() being true.
    auto* root = preset.getDynamicObject();
    ASSERT_NE(root, nullptr);
    auto* nodes = root->getProperty("nodes").getArray();
    ASSERT_NE(nodes, nullptr);
    bool sawStripState = false;
    for (auto& n : *nodes) {
        auto* nObj = n.getDynamicObject();
        if (nObj == nullptr || nObj->getProperty("type").toString() != "Channel Strip")
            continue;
        auto* state = nObj->getProperty("state").getDynamicObject();
        ASSERT_NE(state, nullptr);
        EXPECT_FALSE(state->hasProperty("isBus"));
        EXPECT_FALSE(state->hasProperty("sends"));
        sawStripState = true;
    }
    EXPECT_TRUE(sawStripState);

    juce::AudioProcessorGraph target;
    std::vector<synth::Macro> outMacros;
    const auto added = synth::TrackPresetManager::insertTrackPreset(preset, target, {0, 0}, &outMacros);
    ASSERT_FALSE(added.empty());

    juce::AudioProcessorGraph::Node* insertedStrip = nullptr;
    for (const auto id : added) {
        auto* node = target.getNodeForId(id);
        if (node != nullptr && dynamic_cast<ChannelStripModule*>(node->getProcessor()) != nullptr)
            insertedStrip = node;
    }
    ASSERT_NE(insertedStrip, nullptr);

    // Fresh off insertTrackPreset the strip is NOT yet re-flagged -- that is
    // MainComponent::insertBusFromPresetVar's own job (the same "Add bus" mechanism), not
    // TrackPresetManager's. The gain parameter must already have round-tripped, though.
    auto* insertedModule = dynamic_cast<ChannelStripModule*>(insertedStrip->getProcessor());
    ASSERT_NE(insertedModule, nullptr);
    juce::AudioProcessorParameterWithID* insertedGainParam = nullptr;
    for (auto* p : insertedModule->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p);
            withId != nullptr && withId->paramID == "gain")
            insertedGainParam = withId;
    ASSERT_NE(insertedGainParam, nullptr);
    EXPECT_NEAR(insertedGainParam->getValue(), 0.75f, 1.0e-4f);
    EXPECT_FALSE(insertedModule->isBus()) << "the scrub means a bare insertTrackPreset never re-flags isBus on its own";

    // Applying the same re-flag insertBusFromPresetVar applies makes it classify as a bus again.
    insertedModule->setIsBus(true);
    EXPECT_TRUE(synth::isBusStrip(target, insertedStrip->nodeID));
}

TEST(TrackPresetBus, InsertingTwiceProducesTwoIndependentCopies) {
    HostedPatchCFT source;
    GraphEditor sourceEditor(source.engine);
    const auto rig = buildSimpleBusRigCFT(sourceEditor, source.engine, "Delay Bus");
    ASSERT_NE(rig.macro, nullptr);

    auto preset = synth::TrackPresetManager::extractTrackPreset(source.engine.getGraph(), sourceEditor.getMacros(),
                                                                rig.macro->id, synth::TrackPresetKind::Bus, "RTBus2");
    ASSERT_TRUE(preset.isObject());

    juce::AudioProcessorGraph target;
    const auto first = synth::TrackPresetManager::insertTrackPreset(preset, target, {0, 0});
    const auto second = synth::TrackPresetManager::insertTrackPreset(preset, target, {900, 0});

    ASSERT_FALSE(first.empty());
    EXPECT_EQ(second.size(), first.size());
    EXPECT_EQ(countNodesOfTypeCFT(target, ModuleType::ChannelStrip), 2);
    for (auto a : first)
        for (auto b : second)
            EXPECT_NE(a, b) << "the second insert must not collide with (or overwrite) the first";
}

// ---- listTrackPresets groups Bus separately from Audio/Instrument ----

TEST_F(TrackPresetBusTest, ListTrackPresetsReturnsBusPresetsUnderTheBusKind) {
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    const auto rig = buildSimpleBusRigCFT(editor, patch.engine, "Bus For Listing");
    ASSERT_NE(rig.macro, nullptr);
    auto preset = synth::TrackPresetManager::extractTrackPreset(
        patch.engine.getGraph(), editor.getMacros(), rig.macro->id, synth::TrackPresetKind::Bus, kBusPresetName);
    ASSERT_TRUE(preset.isObject());
    const auto dir = synth::TrackPresetManager::getDefaultTrackPresetsDirectory();
    ASSERT_TRUE(synth::TrackPresetManager::saveTrackPreset(dir, kBusPresetName, preset));

    const auto busList = synth::TrackPresetManager::listTrackPresets(dir, synth::TrackPresetKind::Bus);
    bool found = false;
    for (const auto& info : busList)
        if (info.name == kBusPresetName)
            found = true;
    EXPECT_TRUE(found) << "a saved Bus preset must be listed under TrackPresetKind::Bus";

    const auto audioList = synth::TrackPresetManager::listTrackPresets(dir, synth::TrackPresetKind::Audio);
    for (const auto& info : audioList)
        EXPECT_NE(info.name, kBusPresetName) << "a Bus preset must never also show up in the Audio group";
}

// ---- "+ Track" menu's Bus submenu ----

TEST_F(TrackPresetBusTest, PlusTrackMenuBuildsBusSubmenuFromSnapshot) {
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    const auto rig = buildSimpleBusRigCFT(editor, patch.engine, "Menu Bus");
    ASSERT_NE(rig.macro, nullptr);
    auto preset = synth::TrackPresetManager::extractTrackPreset(
        patch.engine.getGraph(), editor.getMacros(), rig.macro->id, synth::TrackPresetKind::Bus, kBusPresetName);
    ASSERT_TRUE(preset.isObject());
    const auto dir = synth::TrackPresetManager::getDefaultTrackPresetsDirectory();
    ASSERT_TRUE(synth::TrackPresetManager::saveTrackPreset(dir, kBusPresetName, preset));

    synth::ui::TimelinePanelComponent panel;
    BusMenuStubHostFBT host;
    panel.setTrackHeaderHost(&host);

    const auto menu = panel.buildAddTrackMenu();
    const auto* busGroup = findMenuItemByTextCFT(menu, "Bus from Preset");
    ASSERT_NE(busGroup, nullptr) << "a saved Bus preset must produce its own grouped submenu";
    ASSERT_NE(busGroup->subMenu, nullptr);
    const auto* item = findMenuItemByTextCFT(*busGroup->subMenu, kBusPresetName);
    ASSERT_NE(item, nullptr);

    panel.applyAddTrackMenuChoice(item->itemID);

    EXPECT_EQ(host.addBusFromPresetCalls, 1);
    EXPECT_EQ(host.lastPresetName, kBusPresetName);
}

// ---- End to end: choosing the menu entry inserts the bus chain with NO timeline track ----

TEST_F(TrackPresetBusTest, ChoosingABusPresetEntryInsertsTheChainWithNoTimelineTrack) {
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    const auto rig = buildSimpleBusRigCFT(editor, patch.engine, "E2E Bus");
    ASSERT_NE(rig.macro, nullptr);
    auto preset = synth::TrackPresetManager::extractTrackPreset(
        patch.engine.getGraph(), editor.getMacros(), rig.macro->id, synth::TrackPresetKind::Bus, kBusPresetName);
    ASSERT_TRUE(preset.isObject());
    const auto dir = synth::TrackPresetManager::getDefaultTrackPresetsDirectory();
    ASSERT_TRUE(synth::TrackPresetManager::saveTrackPreset(dir, kBusPresetName, preset));

    auto mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
    mc->setSize(1600, 900);
    mc->getAudioEngine().suspendDeviceCallback();

    const auto tracksBefore = mc->getTimelineDoc().getTracks().size();
    const auto stripsBefore = countNodesOfTypeCFT(mc->getAudioEngine().getGraph(), ModuleType::ChannelStrip);

    const auto menu = mc->getTimelinePanel().buildAddTrackMenu();
    const auto* busGroup = findMenuItemByTextCFT(menu, "Bus from Preset");
    ASSERT_NE(busGroup, nullptr);
    ASSERT_NE(busGroup->subMenu, nullptr);
    const auto* item = findMenuItemByTextCFT(*busGroup->subMenu, kBusPresetName);
    ASSERT_NE(item, nullptr) << "the saved bus preset must be listed in the real app's own menu";

    mc->getTimelinePanel().applyAddTrackMenuChoice(item->itemID);

    // No timeline track at all -- a bus has none -- but the chain (and its strip) is there, flagged
    // as a bus again exactly the way "Add bus" flags a freshly built one.
    EXPECT_EQ(mc->getTimelineDoc().getTracks().size(), tracksBefore)
        << "inserting a Bus preset must create NO timeline track";
    EXPECT_EQ(countNodesOfTypeCFT(mc->getAudioEngine().getGraph(), ModuleType::ChannelStrip), stripsBefore + 1);

    // The starter patch has no bus of its own, so any isBus() strip found here is the one this
    // insert just created and re-flagged.
    juce::AudioProcessorGraph::Node* insertedStrip = nullptr;
    for (auto* node : mc->getAudioEngine().getGraph().getNodes()) {
        auto* module = dynamic_cast<ChannelStripModule*>(node->getProcessor());
        if (module != nullptr && module->isBus())
            insertedStrip = node;
    }
    ASSERT_NE(insertedStrip, nullptr) << "the inserted strip must be re-flagged as a bus, the same way Add bus does";
    EXPECT_TRUE(synth::isBusStrip(mc->getAudioEngine().getGraph(), insertedStrip->nodeID));
}

// A preset saved from "Bus 1" and inserted twice must not produce two columns with the same name:
// a captured macro name another macro already carries falls back to the numbered "Bus N" default.
TEST_F(TrackPresetBusTest, InsertingTheSamePresetTwiceGivesDistinctBusNames) {
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    const auto rig = buildSimpleBusRigCFT(editor, patch.engine, "Bus 1");
    ASSERT_NE(rig.macro, nullptr);
    auto preset = synth::TrackPresetManager::extractTrackPreset(
        patch.engine.getGraph(), editor.getMacros(), rig.macro->id, synth::TrackPresetKind::Bus, kBusPresetName);
    ASSERT_TRUE(preset.isObject());
    const auto dir = synth::TrackPresetManager::getDefaultTrackPresetsDirectory();
    ASSERT_TRUE(synth::TrackPresetManager::saveTrackPreset(dir, kBusPresetName, preset));

    auto mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
    mc->setSize(1600, 900);
    mc->getAudioEngine().suspendDeviceCallback();

    for (int i = 0; i < 2; ++i) {
        const auto menu = mc->getTimelinePanel().buildAddTrackMenu();
        const auto* busGroup = findMenuItemByTextCFT(menu, "Bus from Preset");
        ASSERT_NE(busGroup, nullptr);
        ASSERT_NE(busGroup->subMenu, nullptr);
        const auto* item = findMenuItemByTextCFT(*busGroup->subMenu, kBusPresetName);
        ASSERT_NE(item, nullptr);
        mc->getTimelinePanel().applyAddTrackMenuChoice(item->itemID);
    }

    std::vector<juce::String> busNames;
    auto& graph = mc->getAudioEngine().getGraph();
    for (auto* node : graph.getNodes()) {
        auto* module = dynamic_cast<ChannelStripModule*>(node->getProcessor());
        if (module == nullptr || !module->isBus())
            continue;
        if (const auto* macro = mc->getGraphEditor().getMacros().findByMember(node->properties["uuid"].toString()))
            busNames.push_back(macro->name);
    }
    ASSERT_EQ(busNames.size(), 2u);
    EXPECT_EQ(busNames[0], "Bus 1") << "the first insert keeps the preset's own captured name";
    EXPECT_NE(busNames[0], busNames[1]) << "the second insert must not reuse a name already in the project";
}
