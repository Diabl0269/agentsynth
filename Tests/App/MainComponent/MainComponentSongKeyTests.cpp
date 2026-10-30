// Concern: the song key in the app: saved with the project, one undo step per change, reset by New Patch, shown in the
// transport bar and opened by the Set Song Key command.
#include "../../TestWait.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "Music/SongKey.h"
#include "ShortcutManager/AppCommands.h"
#include "Transport/TransportDoc.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"
#include "UI/Timeline/SongKeyField.h"

namespace {

constexpr juce::uint32 kStart = 1000;
constexpr juce::uint32 kHeld = 400; // past the 300 ms debounce

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }

synth::MusicalScale dDorian() { return synth::makeScale(2, synth::scalePresetIndex("Dorian")); }

} // namespace

TEST_F(MainComponentTest, SongKeySurvivesSaveAndReopenAndTheLoadLeavesTheDocumentClean) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.getAudioEngine().getTransport().setSongKey(dDorian());

    const auto bundleDir = tempRoot.getChildFile("SongKey.agsproj");
    ASSERT_TRUE(mc.saveProjectForTest(bundleDir));

    MainComponent reloaded(std::make_unique<MockProvider>());
    reloaded.setSize(1600, 900);
    reloaded.getAudioEngine().suspendDeviceCallback();
    ASSERT_FALSE(reloaded.getAudioEngine().getTransport().getDocumentState().songKey.has_value());
    ASSERT_TRUE(reloaded.openProjectForTest(bundleDir));
    const auto& key = reloaded.getAudioEngine().getTransport().getDocumentState().songKey;
    ASSERT_TRUE(key.has_value());
    EXPECT_TRUE(synth::sameKey(*key, dDorian()));

    reloaded.pollTransportEditsForTest(kStart);
    reloaded.pollTransportEditsForTest(kStart + kHeld);
    settle();
    EXPECT_FALSE(reloaded.getUndoManager().canUndo());
    EXPECT_FALSE(reloaded.isProjectDirty());

    auto& field = reloaded.getTimelinePanel().getTransportBar().getSongKeyField();
    field.refreshFromTransport();
    EXPECT_EQ(field.getText(), "D Dorian");
}

TEST_F(MainComponentTest, ChangingTheSongKeyIsOneUndoStepAndUndoPutsItBack) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.stopTimer();
    auto& transport = mc.getAudioEngine().getTransport();

    transport.setSongKey(dDorian());
    mc.pollTransportEditsForTest(kStart);
    mc.pollTransportEditsForTest(kStart + kHeld);
    synth::test::waitUntil([&] { return mc.getUndoManager().canUndo(); });
    ASSERT_TRUE(mc.getUndoManager().canUndo());
    EXPECT_TRUE(mc.isProjectDirty());

    mc.getUndoManager().undo();
    settle();
    EXPECT_FALSE(transport.getDocumentState().songKey.has_value());
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "the change was exactly one undo step";

    mc.getUndoManager().redo();
    synth::test::waitUntil([&] { return transport.getDocumentState().songKey.has_value(); });
    ASSERT_TRUE(transport.getDocumentState().songKey.has_value());
    EXPECT_TRUE(synth::sameKey(*transport.getDocumentState().songKey, dDorian()));
}

TEST_F(MainComponentTest, NewPatchStartsWithNoKeyAndRecordsNoEdit) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.stopTimer();
    auto& transport = mc.getAudioEngine().getTransport();
    transport.setSongKey(dDorian());
    mc.pollTransportEditsForTest(kStart);
    mc.pollTransportEditsForTest(kStart + kHeld);

    mc.newPatchForTest();
    EXPECT_FALSE(transport.getDocumentState().songKey.has_value());
    mc.pollTransportEditsForTest(kStart + 2 * kHeld);
    mc.pollTransportEditsForTest(kStart + 3 * kHeld);
    settle();
    EXPECT_FALSE(mc.getUndoManager().canUndo());
    EXPECT_FALSE(mc.isProjectDirty());
}

TEST_F(MainComponentTest, SetSongKeyCommandOpensThePickerFromTheTransportBar) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& field = mc.getTimelinePanel().getTransportBar().getSongKeyField();
    std::unique_ptr<juce::Component> launched;
    field.pickerLauncher = [&launched](std::unique_ptr<juce::Component> picker, juce::Rectangle<int>) {
        launched = std::move(picker);
    };
    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::setSongKey, false));
    ASSERT_NE(launched, nullptr);
    EXPECT_EQ(static_cast<juce::Component*>(field.getOpenPicker()), launched.get());
    EXPECT_EQ(AppCommands::getCommandForAction("setSongKey"), AppCommands::setSongKey);
}
