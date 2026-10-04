// TimelineTrackDeleteConfirmTests.cpp
//
// Cmd+Backspace on a focused track row, driven through the row's real keyPressed() against a real MainComponent:
// the question is asked and nothing goes until it is confirmed, Cancel keeps the track, Delete removes it as one
// undo step, "Don't ask again" sticks and the Preferences switch brings the question back. The dialog window is
// answered through a hook; the preference lives in the shared settings file, so the fixture puts it back.

#include "TimelinePanel/TimelinePanelTestFixture.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"
#include "UI/Timeline/DeleteTrackConfirm.h"

namespace {

juce::KeyPress cmdBackspace() {
    return juce::KeyPress(juce::KeyPress::backspaceKey, juce::ModifierKeys::commandModifier, 0);
}

constexpr const char* kKey = "timelineAskBeforeDeletingTrack";

class DeleteTrackConfirmTest : public TimelinePanelIntegrationTest {
protected:
    void SetUp() override {
        TimelinePanelIntegrationTest::SetUp();
        mcPtr = std::make_unique<MainComponent>(std::make_unique<MockProviderTL>());
        mc().setSize(1600, 1000);
        mc().simulateToggleBottomPanelClick();
        mc().getAudioEngine().getDeviceManager().closeAudioDevice();
        mc().simulateAddMidiTrackClick();
        settings().removeValue(kKey);
        // Holds the answer back until the test gives it, as a window that is still open would.
        synth::ui::test_hooks::deleteTrackConfirmHookForTest() = [this](const auto& text, auto done) {
            ++asked;
            lastText = text;
            pendingAnswer = std::move(done);
        };
    }

    void TearDown() override {
        synth::ui::test_hooks::deleteTrackConfirmHookForTest() = nullptr;
        settings().removeValue(kKey);
        settings().saveIfNeeded();
        mcPtr.reset(); // before the base class puts the panel keys back, so nothing it writes on teardown outlasts them
        TimelinePanelIntegrationTest::TearDown();
    }

    MainComponent& mc() { return *mcPtr; }
    juce::PropertiesFile& settings() { return *mc().getAppPropertiesForTest().getUserSettings(); }
    size_t trackCount() { return mc().getTimelineDoc().getTracks().size(); }
    bool pressOnFirstRow(const juce::KeyPress& key = cmdBackspace()) {
        auto* header = mc().getTimelinePanel().getTrackHeaderAt(0);
        return header != nullptr && header->keyPressed(key);
    }

    std::unique_ptr<MainComponent> mcPtr;
    int asked = 0;
    synth::ui::DeleteTrackConfirmText lastText;
    std::function<void(bool, bool)> pendingAnswer;
};

} // namespace

TEST_F(DeleteTrackConfirmTest, AsksFirstAndDeletesNothingUntilConfirmed) {
    ASSERT_EQ(trackCount(), 1u);
    EXPECT_TRUE(pressOnFirstRow());
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(lastText.title, "Delete Track 1?");
    EXPECT_EQ(trackCount(), 1u) << "the dialog is still open";
    ASSERT_TRUE(pendingAnswer);
}

TEST_F(DeleteTrackConfirmTest, CancelDeletesNothingAndLeavesTheQuestionOn) {
    pressOnFirstRow();
    pendingAnswer(false, true);
    EXPECT_EQ(trackCount(), 1u);
    EXPECT_TRUE(settings().getBoolValue(kKey, true)) << "a ticked box on Cancel is not a choice to stop asking";
}

TEST_F(DeleteTrackConfirmTest, ConfirmingDeletesTheTrackAsOneUndoStep) {
    const auto nodes = mc().getAudioEngine().getGraph().getNumNodes();
    pressOnFirstRow();
    pendingAnswer(true, false);
    EXPECT_EQ(trackCount(), 0u);
    EXPECT_TRUE(settings().getBoolValue(kKey, true));

    ASSERT_TRUE(mc().getUndoManager().undo());
    EXPECT_EQ(trackCount(), 1u) << "one Undo brings the track back";
    EXPECT_EQ(mc().getAudioEngine().getGraph().getNumNodes(), nodes) << "and its node with it";
}

TEST_F(DeleteTrackConfirmTest, DontAskAgainPersistsAndTheNextPressDeletesAtOnce) {
    pressOnFirstRow();
    pendingAnswer(true, true);
    EXPECT_EQ(trackCount(), 0u);
    EXPECT_FALSE(settings().getBoolValue(kKey, true));

    mc().simulateAddMidiTrackClick();
    ASSERT_EQ(trackCount(), 1u);
    EXPECT_TRUE(pressOnFirstRow());
    EXPECT_EQ(asked, 1) << "no second question";
    EXPECT_EQ(trackCount(), 0u);
}

TEST_F(DeleteTrackConfirmTest, ThePreferencesSwitchBringsTheQuestionBack) {
    settings().setValue(kKey, "0");
    PreferencesSettingsTab tab(mc().getAppPropertiesForTest());
    ASSERT_FALSE(tab.isAskBeforeDeletingTrackEnabled());
    tab.setAskBeforeDeletingTrackEnabled(true);

    pressOnFirstRow();
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(trackCount(), 1u);
}

TEST_F(DeleteTrackConfirmTest, OtherKeysAreNotClaimed) {
    EXPECT_FALSE(pressOnFirstRow(juce::KeyPress(juce::KeyPress::backspaceKey)));
    EXPECT_FALSE(pressOnFirstRow(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(asked, 0);
    EXPECT_EQ(trackCount(), 1u);
}
