// TimelineTrackDeleteConfirmTests.cpp
//
// Cmd+Backspace on a focused track row, driven through the row's real keyPressed() against a real MainComponent:
// the question is asked and nothing goes until it is confirmed, Cancel keeps the track, Delete removes it as one
// undo step, "Don't ask again" sticks and the Preferences switch brings the question back. The dialog window is
// answered through a hook; the preference lives in the shared settings file, so the fixture puts it back. Deleting a
// track's macro from the canvas macro menu asks the same question and runs the same track delete.

#include "AppUndoManager.h"
#include "MacroSet.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "TimelinePanel/TimelinePanelTestFixture.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
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

namespace {

// The macro menu's item, as a right-click on the macro card offers it.
void clickDeleteMacroItem(GraphEditor& editor, const juce::String& macroId) {
    const auto menu = editor.buildMacroMenu(macroId);
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().text == "Delete Macro && Modules") {
            ASSERT_TRUE(static_cast<bool>(it.getItem().action));
            it.getItem().action();
            return;
        }
    FAIL() << "the macro menu has no Delete Macro item";
}

class DeleteTrackMacroTest : public DeleteTrackConfirmTest {
protected:
    void SetUp() override {
        DeleteTrackConfirmTest::SetUp();
        mc().simulateAddAudioTrackClick();
        trackIndex = mc().getTimelineDoc().getTracks().size() - 1;
        const auto uuid = mc().getTimelineDoc().getTracks()[trackIndex].bindingUuid;
        const auto* macro = editor().getMacros().findByMember(uuid);
        ASSERT_NE(macro, nullptr);
        macroId = macro->id;
        trackUuid = uuid;
    }
    GraphEditor& editor() { return mc().getGraphEditor(); }
    int nodeCount() { return mc().getAudioEngine().getGraph().getNumNodes(); }
    bool hasNode(const juce::String& uuid) {
        for (auto* node : mc().getAudioEngine().getGraph().getNodes())
            if (node->properties["uuid"].toString() == uuid)
                return true;
        return false;
    }

    size_t trackIndex = 0;
    juce::String macroId, trackUuid;
};

} // namespace

TEST_F(DeleteTrackMacroTest, DeletingATracksMacroAsksAboutTheTrackAndChangesNothingUntilConfirmed) {
    const auto tracks = trackCount();
    const auto nodes = nodeCount();
    clickDeleteMacroItem(editor(), macroId);
    EXPECT_EQ(asked, 1);
    EXPECT_TRUE(lastText.message.contains("also deletes the track Audio 2")) << lastText.message.toStdString();
    EXPECT_TRUE(lastText.message.contains("modules only it uses"));
    EXPECT_EQ(trackCount(), tracks);
    EXPECT_EQ(nodeCount(), nodes);
    EXPECT_NE(editor().getMacros().find(macroId), nullptr);
}

TEST_F(DeleteTrackMacroTest, CancelChangesNothing) {
    const auto tracks = trackCount();
    const auto nodes = nodeCount();
    const auto macros = editor().getMacros().size();
    clickDeleteMacroItem(editor(), macroId);
    pendingAnswer(false, true);
    EXPECT_EQ(trackCount(), tracks);
    EXPECT_EQ(nodeCount(), nodes);
    EXPECT_EQ(editor().getMacros().size(), macros);
    EXPECT_TRUE(settings().getBoolValue(kKey, true));
}

TEST_F(DeleteTrackMacroTest, ConfirmingDeletesTheTrackItsMacroAndItsModulesAsOneUndoStep) {
    const auto tracks = trackCount();
    const auto nodes = nodeCount();
    const auto macros = editor().getMacros().size();
    mc().getUndoManager().beginNewTransaction();
    clickDeleteMacroItem(editor(), macroId);
    pendingAnswer(true, false);

    EXPECT_EQ(trackCount(), tracks - 1) << "the track goes with its macro";
    EXPECT_EQ(editor().getMacros().find(macroId), nullptr);
    EXPECT_FALSE(hasNode(trackUuid));
    EXPECT_LT(nodeCount(), nodes);
    EXPECT_EQ(editor().getMacros().size(), macros - 1);

    ASSERT_TRUE(mc().getUndoManager().undo()) << "one step";
    EXPECT_EQ(trackCount(), tracks);
    EXPECT_EQ(nodeCount(), nodes);
    EXPECT_EQ(editor().getMacros().size(), macros);
    EXPECT_TRUE(hasNode(trackUuid));
}

TEST_F(DeleteTrackMacroTest, DontAskAgainSharesTheTrackDeleteSetting) {
    clickDeleteMacroItem(editor(), macroId);
    pendingAnswer(true, true);
    EXPECT_FALSE(settings().getBoolValue(kKey, true));

    mc().simulateAddAudioTrackClick();
    const auto uuid = mc().getTimelineDoc().getTracks().back().bindingUuid;
    const auto id = editor().getMacros().findByMember(uuid)->id;
    const auto tracks = trackCount();
    clickDeleteMacroItem(editor(), id);
    EXPECT_EQ(asked, 1) << "no second question";
    EXPECT_EQ(trackCount(), tracks - 1);
}

TEST_F(DeleteTrackMacroTest, AMacroThatIsNoTracksStillDeletesWithoutAsking) {
    auto addAt = [this](std::unique_ptr<juce::AudioProcessor> processor, int x) {
        auto node = mc().getAudioEngine().getGraph().addNode(std::move(processor));
        node->properties.set("uuid", juce::Uuid().toDashedString());
        node->properties.set("x", x);
        node->properties.set("y", 700);
        return node->nodeID;
    };
    const auto a = addAt(std::make_unique<OscillatorModule>(), 100);
    const auto b = addAt(std::make_unique<FilterModule>(), 500);
    editor().updateComponents();
    editor().setSelectedNodes({a, b});
    const auto groupId = editor().getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(groupId.isEmpty());
    const auto tracks = trackCount();

    clickDeleteMacroItem(editor(), groupId);

    EXPECT_EQ(asked, 0);
    EXPECT_EQ(trackCount(), tracks);
    EXPECT_EQ(editor().getMacros().find(groupId), nullptr);
    EXPECT_EQ(mc().getAudioEngine().getGraph().getNodeForId(a), nullptr);
    EXPECT_EQ(mc().getAudioEngine().getGraph().getNodeForId(b), nullptr);
}
