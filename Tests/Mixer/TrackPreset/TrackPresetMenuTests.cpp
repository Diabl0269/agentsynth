// TrackPresetMenuTests.cpp — FRO229: headless coverage for the "+ Track" menu's grouped
// saved-preset submenus and "Insert Track Preset from File..." (TimelinePanelTrackHeaders.cpp),
// exercised through the same two seams TrackPresetDefaultsTests.cpp already uses for the per-type
// default: TimelinePanelComponent::applyAddTrackMenuChoice (a menu that never runs in a test
// process) and, new here, MainComponent::insertTrackPresetFromFileForTest (no real FileChooser
// headlessly either -- see MainComponentTrackPresets.cpp's own comment on insertTrackPresetFromFile).
//
// Like TrackPresetDefaultsTests.cpp, this exercises the real, process-global default track-presets
// directory (TrackPresetManager::getDefaultTrackPresetsDirectory() -- hardcoded at every call site,
// no injectable override), so it follows the same posture: distinctive names, deleted in SetUp AND
// TearDown.

#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../ChannelFlow/ChannelFlowTestFixture.h"
#include "TrackPresetTestFixture.h"

#include <gtest/gtest.h>

namespace {

// Names no real user or other test is plausibly using -- same "can never be confused with real
// saved data" reasoning as TrackPresetDefaultsTests.cpp's kTestPresetName.
constexpr const char* kAudioPresetName = "__FRO229_Test_Audio_Preset__";
constexpr const char* kInstrumentPresetName = "__FRO229_Test_Instrument_Preset__";

// Builds a minimal single-track rig (Track In -> Oscillator -> Filter, boxed via "Make Channel")
// and extracts it as a track preset var under `name`/`kind` -- does NOT write it to disk (callers
// that want it listed/loaded call TrackPresetManager::saveTrackPreset themselves, same division
// TrackPresetDefaultsTests.cpp's own test body uses inline). A void var means the rig failed to
// box into a channel macro.
juce::var buildDistinctiveTrackPresetVarMFT(synth::TrackPresetKind kind, const juce::String& name) {
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    const auto rig = buildSimpleTrackRigCFT(editor, patch.engine, patch.output);
    if (rig.macro == nullptr)
        return {};
    return synth::TrackPresetManager::extractTrackPreset(patch.engine.getGraph(), editor.getMacros(), rig.macro->id,
                                                         kind, name);
}

// Minimal TrackHeaderHost that only records the two calls this file cares about
// (addTrackFromPreset/addTrackFromPresetFile) -- same "stub everything else inert" posture
// StubTrackHeaderHost (TimelineTrackHeaderTests.cpp) takes toward the rest of the interface.
class MenuStubHostMFT : public synth::ui::TrackHeaderHost {
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

    void addTrackFromPreset(const juce::String& presetName, synth::TrackPresetKind kind) override {
        ++addTrackFromPresetCalls;
        lastPresetName = presetName;
        lastKind = kind;
    }
    void addTrackFromPresetFile() override { ++addTrackFromPresetFileCalls; }

    int addTrackFromPresetCalls = 0;
    juce::String lastPresetName;
    synth::TrackPresetKind lastKind = synth::TrackPresetKind::Audio;
    int addTrackFromPresetFileCalls = 0;
};

} // namespace

class TrackPresetMenuTest : public MainComponentTest {
protected:
    void deleteTestPresets() {
        const auto dir = synth::TrackPresetManager::getDefaultTrackPresetsDirectory();
        synth::TrackPresetManager::deleteTrackPreset(dir, kAudioPresetName);
        synth::TrackPresetManager::deleteTrackPreset(dir, kInstrumentPresetName);
    }
    void SetUp() override {
        MainComponentTest::SetUp();
        deleteTestPresets();
    }
    void TearDown() override {
        deleteTestPresets();
        MainComponentTest::TearDown();
    }
};

// ---- Grouped listing ----

TEST_F(TrackPresetMenuTest, SubmenuListsSavedPresetsGroupedByType) {
    const auto dir = synth::TrackPresetManager::getDefaultTrackPresetsDirectory();
    const auto audioPreset = buildDistinctiveTrackPresetVarMFT(synth::TrackPresetKind::Audio, kAudioPresetName);
    ASSERT_TRUE(audioPreset.isObject());
    ASSERT_TRUE(synth::TrackPresetManager::saveTrackPreset(dir, kAudioPresetName, audioPreset));
    const auto instrumentPreset =
        buildDistinctiveTrackPresetVarMFT(synth::TrackPresetKind::Instrument, kInstrumentPresetName);
    ASSERT_TRUE(instrumentPreset.isObject());
    ASSERT_TRUE(synth::TrackPresetManager::saveTrackPreset(dir, kInstrumentPresetName, instrumentPreset));

    synth::ui::TimelinePanelComponent panel;
    const auto menu = panel.buildAddTrackMenu();

    const auto* audioGroup = findMenuItemByTextCFT(menu, "Audio Track from Preset");
    ASSERT_NE(audioGroup, nullptr) << "a saved Audio preset must produce the grouped submenu";
    ASSERT_NE(audioGroup->subMenu, nullptr);
    EXPECT_NE(findMenuItemByTextCFT(*audioGroup->subMenu, kAudioPresetName), nullptr);
    EXPECT_EQ(findMenuItemByTextCFT(*audioGroup->subMenu, kInstrumentPresetName), nullptr)
        << "the Audio group must not also list the Instrument preset";

    const auto* instrumentGroup = findMenuItemByTextCFT(menu, "Instrument Track from Preset");
    ASSERT_NE(instrumentGroup, nullptr) << "a saved Instrument preset must produce the grouped submenu";
    ASSERT_NE(instrumentGroup->subMenu, nullptr);
    EXPECT_NE(findMenuItemByTextCFT(*instrumentGroup->subMenu, kInstrumentPresetName), nullptr);
    EXPECT_EQ(findMenuItemByTextCFT(*instrumentGroup->subMenu, kAudioPresetName), nullptr)
        << "the Instrument group must not also list the Audio preset";
}

// ---- Menu-open-time snapshot ----

TEST_F(TrackPresetMenuTest, MenuChoiceResolvesAgainstTheSnapshotTakenWhenTheMenuWasBuilt) {
    const auto dir = synth::TrackPresetManager::getDefaultTrackPresetsDirectory();
    const auto preset = buildDistinctiveTrackPresetVarMFT(synth::TrackPresetKind::Audio, kAudioPresetName);
    ASSERT_TRUE(preset.isObject());
    ASSERT_TRUE(synth::TrackPresetManager::saveTrackPreset(dir, kAudioPresetName, preset));

    synth::ui::TimelinePanelComponent panel;
    MenuStubHostMFT host;
    panel.setTrackHeaderHost(&host);

    const auto menu = panel.buildAddTrackMenu();
    const auto* item = findMenuItemByTextCFT(menu, kAudioPresetName);
    ASSERT_NE(item, nullptr);
    const int menuId = item->itemID;

    // Mutate the REAL on-disk state after the menu was built but before the click resolves -- the
    // click must still resolve against what buildAddTrackMenu() actually showed, not a re-listing
    // of the directory (see kAddTrackPresetAudioMenuIdBase's own comment).
    ASSERT_TRUE(synth::TrackPresetManager::deleteTrackPreset(dir, kAudioPresetName));

    panel.applyAddTrackMenuChoice(menuId);

    EXPECT_EQ(host.addTrackFromPresetCalls, 1);
    EXPECT_EQ(host.lastPresetName, kAudioPresetName);
    EXPECT_EQ(host.lastKind, synth::TrackPresetKind::Audio);
}

// ---- Choosing an entry actually inserts a track (end-to-end, mirrors TrackPresetDefaultsTests.cpp) ----

TEST_F(TrackPresetMenuTest, ChoosingAPresetEntryInsertsATrackBuiltFromIt) {
    const auto dir = synth::TrackPresetManager::getDefaultTrackPresetsDirectory();
    const auto preset = buildDistinctiveTrackPresetVarMFT(synth::TrackPresetKind::Audio, kAudioPresetName);
    ASSERT_TRUE(preset.isObject());
    ASSERT_TRUE(synth::TrackPresetManager::saveTrackPreset(dir, kAudioPresetName, preset));

    auto mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
    mc->setSize(1600, 900);
    mc->getAudioEngine().suspendDeviceCallback();

    const auto tracksBefore = mc->getTimelineDoc().getTracks().size();
    // Same falsifiability concern TrackPresetDefaultsTests.cpp documents: the starter patch already
    // has its own Oscillator, so count before/after and require exactly one more.
    const auto oscillatorsBefore = countNodesOfTypeCFT(mc->getAudioEngine().getGraph(), ModuleType::Oscillator);

    const auto menu = mc->getTimelinePanel().buildAddTrackMenu();
    const auto* item = findMenuItemByTextCFT(menu, kAudioPresetName);
    ASSERT_NE(item, nullptr) << "the saved preset must be listed in the real app's own menu";

    mc->getTimelinePanel().applyAddTrackMenuChoice(item->itemID);

    EXPECT_EQ(mc->getTimelineDoc().getTracks().size(), tracksBefore + 1);
    EXPECT_EQ(countNodesOfTypeCFT(mc->getAudioEngine().getGraph(), ModuleType::Oscillator), oscillatorsBefore + 1)
        << "the chosen preset's Oscillator must be what the menu choice actually inserted";
}

// ---- "Insert Track Preset from File..." ----

TEST_F(TrackPresetMenuTest, InsertTrackPresetFromFileInsertsFromTheGivenFile) {
    // The name baked into the file is irrelevant here (never shown, never used to resolve
    // anything) -- getPresetKind() reads "trackPresetKind" back off the var itself.
    const auto preset = buildDistinctiveTrackPresetVarMFT(synth::TrackPresetKind::Instrument, "file-insert-source");
    ASSERT_TRUE(preset.isObject());
    const auto file = tempRoot.getChildFile(juce::String("FileInsert") + synth::TrackPresetManager::kFileExtension);
    ASSERT_TRUE(file.replaceWithText(juce::JSON::toString(preset)));

    auto mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
    mc->setSize(1600, 900);
    mc->getAudioEngine().suspendDeviceCallback();

    const auto tracksBefore = mc->getTimelineDoc().getTracks().size();
    const auto oscillatorsBefore = countNodesOfTypeCFT(mc->getAudioEngine().getGraph(), ModuleType::Oscillator);

    const auto trackName = mc->insertTrackPresetFromFileForTest(file);

    EXPECT_TRUE(trackName.isNotEmpty());
    EXPECT_EQ(mc->getTimelineDoc().getTracks().size(), tracksBefore + 1);
    EXPECT_EQ(countNodesOfTypeCFT(mc->getAudioEngine().getGraph(), ModuleType::Oscillator), oscillatorsBefore + 1);
}

TEST_F(TrackPresetMenuTest, InsertTrackPresetFromFileFailsWithoutInsertingOnABadFile) {
    const auto malformedFile =
        tempRoot.getChildFile(juce::String("Malformed") + synth::TrackPresetManager::kFileExtension);
    ASSERT_TRUE(malformedFile.replaceWithText("not valid json {{{"));
    const auto missingFile =
        tempRoot.getChildFile(juce::String("DoesNotExist") + synth::TrackPresetManager::kFileExtension);

    auto mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
    mc->setSize(1600, 900);
    mc->getAudioEngine().suspendDeviceCallback();

    const auto tracksBefore = mc->getTimelineDoc().getTracks().size();

    EXPECT_TRUE(mc->insertTrackPresetFromFileForTest(malformedFile).isEmpty());
    EXPECT_TRUE(mc->insertTrackPresetFromFileForTest(missingFile).isEmpty());
    EXPECT_EQ(mc->getTimelineDoc().getTracks().size(), tracksBefore)
        << "neither an unparsable nor a missing file may insert anything";
}
