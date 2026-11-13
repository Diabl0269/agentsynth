// MixerRowKeyboardTests.cpp -- the mixer panel's keyboard walk through a column's insert, send and EQ rows
// ("mixerEnterRows", Down), the EQ key ("mixerOpenEq") and the header controls as Tab stops. Every key goes
// through MixerPanelComponent::keyPressed with a real juce::KeyPress, on a real off-screen
// MainComponent, the same rig style MixerPanelKeyboardFocusTests.cpp uses.
#include "../../TestSettingsHelpers.h"
#include "../../TestWait.h"
#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "MixerKeyWalk.h"
#include "MixerNativeHost.h"
#include "MixerZonesTestRig.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/ModuleBase.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerEqThumbnail.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Mixer/MixerSendList.h"
#include <gtest/gtest.h>
#include <iostream>
#include <map>

namespace {

using synth::ui::MixerRowKind;
using synth::ui::MixerRowRef;
using NodeID = juce::AudioProcessorGraph::NodeID;

class MockProviderMRKT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMRKT"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
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

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }
juce::KeyPress tabKey() { return key(juce::KeyPress::tabKey); }
juce::KeyPress rightKey() { return key(juce::KeyPress::rightKey); }
juce::KeyPress upKey() { return key(juce::KeyPress::upKey); }
juce::KeyPress downKey() { return key(juce::KeyPress::downKey); }
juce::KeyPress escKey() { return key(juce::KeyPress::escapeKey); }
juce::KeyPress deleteKey() { return key(juce::KeyPress::deleteKey); }
juce::KeyPress returnKey() { return key(juce::KeyPress::returnKey); }
juce::KeyPress letterKey(juce::juce_wchar c) { return juce::KeyPress(c, juce::ModifierKeys(), 0); }

/** Two tracks and a bus; the first track has its default insert chain (which includes a Parametric EQ)
 *  and one send to the bus, so its column holds the insert rows then one send row. Every section is
 *  shown. */
struct RowRig {
    synth::test::PersistedKeysGuard guard{{"bottomDockVisible", "bottomDockActiveTab", "bottomDockTabOrder",
                                           "mixerSectionInsertsHidden", "mixerSectionSendsHidden",
                                           "mixerSectionEqHidden"}};
    MainComponent mc{std::make_unique<MockProviderMRKT>()};
    NodeID bus;

    RowRig() {
        mc.setSize(1400, 900);
        mc.getBottomDock().getMixerPanel().setVisible(true); // a hidden mixer defers its rebuilds
        mc.getAudioEngine().suspendDeviceCallback();
        mc.newPatchForTest();
        mc.simulateAddAudioTrackClick();
        mc.simulateAddAudioTrackClick();
        bus = panel().createBus();
        panel().rebuild();
        panel().getStripColumnForTest(0)->getSendList().addSendTo(bus);
        panel().rebuild();
        showSections();
        panel().setSize(1400, 700);
        panel().resized();
    }

    synth::ui::MixerPanelComponent& panel() { return mc.getBottomDock().getMixerPanel(); }
    synth::ui::MixerColumnComponent& firstColumn() { return *panel().getStripColumnForTest(0); }

    void showSections() {
        for (const auto section :
             {synth::ui::MixerSection::Inserts, synth::ui::MixerSection::Sends, synth::ui::MixerSection::Eq})
            panel().getSectionLayout().setHidden(section, false);
    }

    ChannelStripModule* firstStrip() {
        auto* node = mc.getAudioEngine().getGraph().getNodeForId(firstColumn().getNodeId());
        return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
    }

    int insertCount() { return firstColumn().getInsertList().getRowCount(); }

    /** Presses Right until the last column, which is Master. */
    void focusMasterColumn() {
        for (int i = 0; i < panel().getColumnCount(); ++i)
            panel().keyPressed(rightKey());
    }

    float sendLevel() { return firstStrip()->getSendLevelParameter(0)->get(); }

    void setSendLevel(float db) {
        auto* level = firstStrip()->getSendLevelParameter(0);
        level->setValueNotifyingHost(level->getNormalisableRange().convertTo0to1(db));
    }

    /** Right once: focus on the first column; Down: into its rows (first row is the first insert). */
    void enterFirstColumnRows() {
        ASSERT_TRUE(panel().keyPressed(rightKey()));
        ASSERT_TRUE(panel().keyPressed(downKey()));
    }

    /** Down until the first send row (after the insert rows, before the EQ row). */
    void stepToSendRow() {
        for (int i = 0; i <= insertCount() && (!panel().getFocusedRowForTest().has_value() ||
                                               panel().getFocusedRowForTest()->kind == MixerRowKind::Insert);
             ++i)
            panel().keyPressed(downKey());
        ASSERT_EQ(panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 0}));
    }
};

} // namespace

TEST(MixerRowKeyboardTest, DownEntersTheFocusedColumnsRowsInsertsFirst) {
    RowRig rig;
    ASSERT_TRUE(rig.panel().keyPressed(rightKey()));
    EXPECT_FALSE(rig.panel().getFocusedRowForTest().has_value());

    EXPECT_TRUE(rig.panel().keyPressed(downKey()));
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 0}));
}

TEST(MixerRowKeyboardTest, TabNeverEntersTheRowsSoTheRegionCycleAlwaysGetsIt) {
    RowRig rig;
    rig.panel().keyPressed(rightKey());
    EXPECT_FALSE(rig.panel().keyPressed(tabKey())) << "unhandled, so the app-wide region cycle still gets it";
    EXPECT_FALSE(rig.panel().getFocusedRowForTest().has_value());
}

TEST(MixerRowKeyboardTest, DownFallsThroughWhenTheSectionsHoldingRowsAreHidden) {
    RowRig rig;
    rig.panel().keyPressed(rightKey());
    for (const auto section :
         {synth::ui::MixerSection::Inserts, synth::ui::MixerSection::Sends, synth::ui::MixerSection::Eq})
        rig.panel().getSectionLayout().setHidden(section, true);
    EXPECT_FALSE(rig.panel().keyPressed(downKey()));

    rig.panel().getSectionLayout().setHidden(synth::ui::MixerSection::Sends, false);
    EXPECT_TRUE(rig.panel().keyPressed(downKey())) << "a shown section with rows is enough";
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 0}));
}

TEST(MixerRowKeyboardTest, DownFallsThroughOnAColumnWithNoRows) {
    RowRig rig;
    rig.panel().keyPressed(rightKey());
    rig.panel().keyPressed(rightKey()); // the second track: inserts, but no sends
    ASSERT_EQ(rig.panel().getFocusedColumnIndexForTest(), 1);
    ASSERT_EQ(rig.panel().getStripColumnForTest(1)->getSendList().getRowCount(), 0);
    rig.panel().getSectionLayout().setHidden(synth::ui::MixerSection::Inserts, true);
    rig.panel().getSectionLayout().setHidden(synth::ui::MixerSection::Eq, true);
    EXPECT_FALSE(rig.panel().keyPressed(downKey()));
}

TEST(MixerRowKeyboardTest, UpAndDownStepInsertsThenSendsThenTheEqAndUpAtTheTopLeaves) {
    RowRig rig;
    ASSERT_FALSE(rig.firstColumn().getEqNodeUuid().isEmpty()) << "a default track has an EQ insert";
    rig.enterFirstColumnRows();
    EXPECT_TRUE(rig.panel().keyPressed(upKey()));
    EXPECT_FALSE(rig.panel().getFocusedRowForTest().has_value()) << "Up at the top returns to the column";
    EXPECT_EQ(rig.panel().getFocusedColumnIndexForTest(), 0);
    ASSERT_TRUE(rig.panel().keyPressed(downKey()));
    EXPECT_TRUE(rig.panel().keyPressed(downKey()));
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 1}));
    for (int i = 2; i < rig.insertCount(); ++i)
        rig.panel().keyPressed(downKey());
    EXPECT_TRUE(rig.panel().keyPressed(downKey()));
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 0})) << "inserts, then sends";
    EXPECT_TRUE(rig.panel().keyPressed(downKey()));
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Eq, 0})) << "then the EQ";
    EXPECT_TRUE(rig.panel().getFocusedRowDescriptionForTest().contains("EQ curve"));
    EXPECT_FALSE(rig.panel().getFocusedRowBoundsForTest().isEmpty()) << "the EQ row gets the focus ring";
    EXPECT_TRUE(rig.panel().keyPressed(downKey()));
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Eq, 0})) << "clamped at the bottom";
    EXPECT_TRUE(rig.panel().keyPressed(upKey()));
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 0})) << "and back the same way";
    EXPECT_EQ(rig.panel().getFocusedColumnIndexForTest(), 0) << "stepping rows never walks columns";
}

TEST(MixerRowKeyboardTest, ArrowsOnAnInsertRowDoNotWalkColumns) {
    RowRig rig;
    rig.enterFirstColumnRows();
    EXPECT_TRUE(rig.panel().keyPressed(rightKey()));
    EXPECT_TRUE(rig.panel().keyPressed(key(juce::KeyPress::leftKey)));
    EXPECT_EQ(rig.panel().getFocusedColumnIndexForTest(), 0);
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 0}));
}

TEST(MixerRowKeyboardTest, RightRaisesAndLeftLowersTheFocusedSendByOneDb) {
    RowRig rig;
    rig.setSendLevel(-6.0f);
    rig.enterFirstColumnRows();
    rig.stepToSendRow();

    EXPECT_TRUE(rig.panel().keyPressed(rightKey()));
    EXPECT_NEAR(rig.sendLevel(), -5.0f, 0.01f);
    EXPECT_TRUE(rig.panel().keyPressed(key(juce::KeyPress::leftKey)));
    EXPECT_TRUE(rig.panel().keyPressed(key(juce::KeyPress::leftKey)));
    EXPECT_NEAR(rig.sendLevel(), -7.0f, 0.01f);
}

TEST(MixerRowKeyboardTest, ShiftMovesTheSendLevelByPointOneDb) {
    RowRig rig;
    rig.setSendLevel(-6.0f);
    rig.enterFirstColumnRows();
    rig.stepToSendRow();

    EXPECT_TRUE(rig.panel().keyPressed(key(juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier)));
    EXPECT_NEAR(rig.sendLevel(), -5.9f, 0.01f);
    EXPECT_TRUE(rig.panel().keyPressed(key(juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier)));
    EXPECT_NEAR(rig.sendLevel(), -6.0f, 0.01f);
}

TEST(MixerRowKeyboardTest, EachSendLevelPressIsOneUndoStep) {
    RowRig rig;
    rig.setSendLevel(-6.0f);
    rig.enterFirstColumnRows();
    rig.stepToSendRow();
    rig.panel().keyPressed(rightKey());
    rig.panel().keyPressed(rightKey());
    ASSERT_NEAR(rig.sendLevel(), -4.0f, 0.01f);

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    rig.panel().rebuild();
    EXPECT_NEAR(rig.sendLevel(), -5.0f, 0.01f) << "one press undone, not both";
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    rig.panel().rebuild();
    EXPECT_NEAR(rig.sendLevel(), -6.0f, 0.01f);
}

TEST(MixerRowKeyboardTest, DeleteRemovesTheFocusedSendAndUndoRestoresIt) {
    RowRig rig;
    rig.enterFirstColumnRows();
    rig.stepToSendRow();
    ASSERT_TRUE(rig.firstStrip()->isSendActive(0));

    EXPECT_TRUE(rig.panel().keyPressed(deleteKey()));
    EXPECT_FALSE(rig.firstStrip()->isSendActive(0));
    EXPECT_EQ(rig.firstColumn().getSendList().getRowCount(), 0);
    ASSERT_TRUE(rig.panel().getFocusedRowForTest().has_value()) << "focus moves to a surviving row";
    EXPECT_EQ(rig.panel().getFocusedRowForTest()->kind, MixerRowKind::Insert);

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    rig.panel().rebuild();
    EXPECT_TRUE(rig.firstStrip()->isSendActive(0));
}

TEST(MixerRowKeyboardTest, DeleteRemovesTheFocusedInsertAndUndoRestoresIt) {
    RowRig rig;
    rig.enterFirstColumnRows();
    const int before = rig.insertCount();
    ASSERT_GE(before, 2);

    EXPECT_TRUE(rig.panel().keyPressed(deleteKey()));
    EXPECT_EQ(rig.insertCount(), before - 1);
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 0}));

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    rig.panel().rebuild();
    EXPECT_EQ(rig.insertCount(), before);
}

// With every insert and send gone, the column's one row is "+ Insert": deleting the last send lands there, and Delete
// on "+ Insert" itself removes nothing and keeps it.
TEST(MixerRowKeyboardTest, DeletingTheLastSendLandsOnPlusInsert) {
    RowRig rig;
    while (rig.firstColumn().getInsertList().getEntryCountForTest() > 0) {
        rig.firstColumn().getInsertList().removeRow(0);
        rig.panel().rebuild();
    }
    rig.panel().keyPressed(rightKey());
    ASSERT_TRUE(rig.panel().keyPressed(downKey()));
    ASSERT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 0})) << "+ Insert";
    ASSERT_EQ(rig.firstColumn().getInsertList().getAddRowIndex(), 0);
    ASSERT_TRUE(rig.panel().keyPressed(downKey())) << "the send row is still there";
    ASSERT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 0}));

    EXPECT_TRUE(rig.panel().keyPressed(deleteKey()));
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 0}));
    EXPECT_TRUE(rig.panel().keyPressed(deleteKey()));
    EXPECT_EQ(rig.firstColumn().getInsertList().getAddRowIndex(), 0) << "+ Insert cannot be removed";
}

TEST(MixerRowKeyboardTest, EscapeReturnsToColumnMode) {
    RowRig rig;
    rig.enterFirstColumnRows();
    EXPECT_TRUE(rig.panel().keyPressed(escKey()));
    EXPECT_FALSE(rig.panel().getFocusedRowForTest().has_value());
    EXPECT_EQ(rig.panel().getFocusedColumnIndexForTest(), 0);
    EXPECT_TRUE(rig.panel().keyPressed(rightKey())) << "the arrows walk columns again";
    EXPECT_EQ(rig.panel().getFocusedColumnIndexForTest(), 1);
}

TEST(MixerRowKeyboardTest, ReturnOnAnInsertSelectsItsChannelOnTheCanvas) {
    RowRig rig;
    auto& macroController = rig.mc.getGraphEditor().getMacroController();
    const auto macros = rig.mc.getGraphEditor().getMacros().getAll();
    const auto anySelected = [&] {
        return std::any_of(macros.begin(), macros.end(),
                           [&](const auto& macro) { return macroController.isMacroSelected(macro.id); });
    };
    ASSERT_FALSE(anySelected());
    rig.enterFirstColumnRows();

    EXPECT_TRUE(rig.panel().keyPressed(returnKey()));
    EXPECT_TRUE(anySelected());
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 0})) << "still in row mode";
}

TEST(MixerRowKeyboardTest, TheFocusedRowIsAnnouncedOnThePanel) {
    RowRig rig;
    rig.setSendLevel(-6.0f);
    EXPECT_TRUE(rig.panel().getFocusedRowDescriptionForTest().isEmpty());

    rig.enterFirstColumnRows();
    EXPECT_EQ(rig.panel().getFocusedRowDescriptionForTest(), rig.firstColumn().getInsertList().describeRow(0));
    EXPECT_TRUE(rig.panel().getFocusedRowDescriptionForTest().startsWith("Insert 1, "));
    rig.panel().keyPressed(downKey());
    EXPECT_TRUE(rig.panel().getFocusedRowDescriptionForTest().startsWith("Insert 2, "));
    rig.stepToSendRow();
    const auto send = rig.panel().getFocusedRowDescriptionForTest();
    EXPECT_TRUE(send.startsWith("Send to ")) << send;
    EXPECT_TRUE(send.endsWith(", -6.0 dB")) << send;

    rig.panel().keyPressed(rightKey());
    EXPECT_TRUE(rig.panel().getFocusedRowDescriptionForTest().endsWith(", -5.0 dB")) << "the level change is read";

    rig.panel().keyPressed(escKey());
    EXPECT_TRUE(rig.panel().getFocusedRowDescriptionForTest().isEmpty());
}

TEST(MixerRowKeyboardTest, ABypassedInsertIsAnnouncedAsBypassed) {
    RowRig rig;
    auto* eq = rig.mc.getAudioEngine().getGraph().getNodeForId(rig.firstColumn().getEqNodeId());
    ASSERT_NE(eq, nullptr);
    auto* module = dynamic_cast<ModuleBase*>(eq->getProcessor());
    int eqRow = -1;
    const auto describe = [&] {
        rig.panel().rebuild();
        for (int i = 0; i < rig.insertCount(); ++i)
            if (rig.firstColumn().getInsertList().describeRow(i).contains("Parametric EQ"))
                eqRow = i;
        return rig.firstColumn().getInsertList().describeRow(eqRow);
    };
    module->setBypassed(false);
    EXPECT_FALSE(describe().endsWith(", bypassed"));
    module->setBypassed(true);
    EXPECT_TRUE(describe().endsWith(", bypassed"));

    rig.enterFirstColumnRows();
    for (int i = 0; i < eqRow; ++i)
        rig.panel().keyPressed(downKey());
    EXPECT_EQ(rig.panel().getFocusedRowDescriptionForTest(), rig.firstColumn().getInsertList().describeRow(eqRow));
}

TEST(MixerRowKeyboardTest, TheFocusedRowHasARingRectangleInsideItsSection) {
    RowRig rig;
    rig.enterFirstColumnRows();
    const auto first = rig.panel().getFocusedRowBoundsForTest();
    ASSERT_FALSE(first.isEmpty());
    EXPECT_TRUE(rig.panel().getLocalBounds().contains(first));

    rig.stepToSendRow();
    const auto send = rig.panel().getFocusedRowBoundsForTest();
    ASSERT_FALSE(send.isEmpty());
    EXPECT_GT(send.getY(), first.getY()) << "the send section sits under the insert section";
    EXPECT_LE(send.getHeight(), synth::ui::MixerSectionLayout::kSendRowHeight);
    EXPECT_GT(send.getHeight(), 0);
}

TEST(MixerRowKeyboardTest, RowFocusSurvivesARebuildOfTheSameColumn) {
    RowRig rig;
    rig.enterFirstColumnRows();
    rig.panel().keyPressed(downKey());
    rig.panel().rebuild();
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 1}));
    EXPECT_EQ(rig.firstColumn().getInsertList().getFocusedRow(), 1) << "the rebuilt list carries it";
}

TEST(MixerRowKeyboardTest, HidingTheFocusedRowsSectionMovesTheFocusToTheOtherSection) {
    RowRig rig;
    rig.enterFirstColumnRows();
    rig.panel().getSectionLayout().setHidden(synth::ui::MixerSection::Inserts, true);
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 0}));
    rig.panel().getSectionLayout().setHidden(synth::ui::MixerSection::Sends, true);
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Eq, 0})) << "the EQ row is still shown";
    rig.panel().getSectionLayout().setHidden(synth::ui::MixerSection::Eq, true);
    EXPECT_FALSE(rig.panel().getFocusedRowForTest().has_value());
}

TEST(MixerRowKeyboardTest, LosingFocusLeavesRowMode) {
    RowRig rig;
    rig.enterFirstColumnRows();
    rig.panel().focusLost(juce::Component::FocusChangeType::focusChangedDirectly);
    EXPECT_FALSE(rig.panel().getFocusedRowForTest().has_value());
    EXPECT_TRUE(rig.panel().getFocusedRowDescriptionForTest().isEmpty());
}

TEST(MixerRowKeyboardTest, EnterRowsIsRebindable) {
    ShortcutManager shortcuts;
    RowRig rig;
    rig.panel().setShortcutManager(&shortcuts);
    shortcuts.setBinding("mixerEnterRows", letterKey('w'));
    rig.panel().keyPressed(rightKey());

    EXPECT_FALSE(rig.panel().keyPressed(downKey())) << "Down no longer enters the rows";
    EXPECT_TRUE(rig.panel().keyPressed(letterKey('w')));
    EXPECT_TRUE(rig.panel().getFocusedRowForTest().has_value());
    rig.panel().setShortcutManager(nullptr);
}

TEST(MixerRowKeyboardTest, EnterRowsDefaultsToDownInTheMixerCategory) {
    ShortcutManager shortcuts;
    EXPECT_EQ(ShortcutManager::getCategory("mixerEnterRows"), ShortcutCategory::Mixer);
    EXPECT_EQ(shortcuts.getBinding("mixerEnterRows"), downKey());
    EXPECT_NE(ShortcutManager::getActionDescription("mixerEnterRows"), juce::String("mixerEnterRows"));
}

TEST(MixerOpenEqTest, EOpensTheFocusedStripsEqWindow) {
    RowRig rig;
    std::vector<NodeID> opened;
    rig.panel().onOpenEqWindow = [&](NodeID id) { opened.push_back(id); };
    rig.panel().keyPressed(rightKey());

    EXPECT_TRUE(rig.panel().keyPressed(letterKey('e')));
    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened.front(), rig.firstColumn().getEqNodeId());
    EXPECT_NE(opened.front(), NodeID{});
}

TEST(MixerOpenEqTest, ADifferentKeyAndAColumnWithoutAnEqDoNothing) {
    RowRig rig;
    int opened = 0;
    rig.panel().onOpenEqWindow = [&](NodeID) { ++opened; };
    rig.panel().keyPressed(rightKey());
    EXPECT_FALSE(rig.panel().keyPressed(letterKey('q')));
    EXPECT_EQ(opened, 0);

    rig.focusMasterColumn(); // Master has no EQ thumbnail
    EXPECT_FALSE(rig.panel().keyPressed(letterKey('e')));
    EXPECT_EQ(opened, 0);
}

TEST(MixerOpenEqTest, NothingFocusedOrTheEqSectionHiddenDoesNothing) {
    RowRig rig;
    int opened = 0;
    rig.panel().onOpenEqWindow = [&](NodeID) { ++opened; };
    EXPECT_FALSE(rig.panel().keyPressed(letterKey('e'))) << "no column focused";

    rig.panel().keyPressed(rightKey());
    rig.panel().getSectionLayout().setHidden(synth::ui::MixerSection::Eq, true);
    EXPECT_FALSE(rig.panel().keyPressed(letterKey('e')));
    EXPECT_EQ(opened, 0);
}

TEST(MixerOpenEqTest, ReturnStillSelectsTheColumnOnTheCanvas) {
    RowRig rig;
    int opened = 0;
    rig.panel().onOpenEqWindow = [&](NodeID) { ++opened; };
    rig.panel().keyPressed(rightKey());
    EXPECT_TRUE(rig.panel().keyPressed(returnKey()));
    EXPECT_EQ(opened, 0) << "Return keeps its select-on-canvas meaning";
}

TEST(MixerOpenEqTest, IsRebindableAndDefaultsToEInTheMixerCategory) {
    ShortcutManager shortcuts;
    EXPECT_EQ(ShortcutManager::getCategory("mixerOpenEq"), ShortcutCategory::Mixer);
    EXPECT_EQ(shortcuts.getBinding("mixerOpenEq"), letterKey('e'));

    RowRig rig;
    int opened = 0;
    rig.panel().onOpenEqWindow = [&](NodeID) { ++opened; };
    rig.panel().setShortcutManager(&shortcuts);
    shortcuts.setBinding("mixerOpenEq", letterKey('q'));
    rig.panel().keyPressed(rightKey());

    EXPECT_FALSE(rig.panel().keyPressed(letterKey('e')));
    EXPECT_EQ(opened, 0);
    EXPECT_TRUE(rig.panel().keyPressed(letterKey('q')));
    EXPECT_EQ(opened, 1);
    rig.panel().setShortcutManager(nullptr);
}

TEST(MixerHeaderControlsTest, EveryHeaderControlIsATabStopWithANameAndATooltip) {
    RowRig rig;
    ShortcutManager shortcuts;
    rig.panel().setShortcutManager(&shortcuts);
    auto& toolbar = rig.panel().getToolbarForTest();
    std::vector<juce::Component*> controls{
        &toolbar.getSectionToggle(synth::ui::MixerSection::Inserts),
        &toolbar.getSectionToggle(synth::ui::MixerSection::Sends),
        &toolbar.getSectionToggle(synth::ui::MixerSection::Eq),
        &toolbar.getResetMetersButtonForTest(),
        &toolbar.getAddBusButtonForTest(),
        &rig.panel().getZonesPaneForTest().getFilterForTest(),
        &rig.panel().getZonesPaneForTest().getChipForTest(synth::ui::MixerZonesPane::Chip::All),
        &rig.panel().getZonesPaneForTest().getChipForTest(synth::ui::MixerZonesPane::Chip::Tracks),
        &rig.panel().getZonesPaneForTest().getChipForTest(synth::ui::MixerZonesPane::Chip::Buses)};
    for (auto* control : controls) {
        EXPECT_TRUE(control->getWantsKeyboardFocus()) << control->getTitle();
        EXPECT_TRUE(control->getTitle().isNotEmpty());
        auto* tip = dynamic_cast<juce::TooltipClient*>(control);
        ASSERT_NE(tip, nullptr);
        EXPECT_TRUE(tip->getTooltip().isNotEmpty()) << control->getTitle();
    }
    rig.panel().setShortcutManager(nullptr);
}

TEST(MixerHeaderControlsTest, TheSectionTogglesTooltipsNameTheirShortcuts) {
    RowRig rig;
    ShortcutManager shortcuts;
    rig.panel().setShortcutManager(&shortcuts);
    auto& toolbar = rig.panel().getToolbarForTest();
    for (const auto section :
         {synth::ui::MixerSection::Inserts, synth::ui::MixerSection::Sends, synth::ui::MixerSection::Eq}) {
        const auto display = ShortcutManager::keyPressToDisplayString(
            shortcuts.getBinding(synth::ui::MixerPanelComponent::sectionToggleActionId(section)));
        auto* tip = dynamic_cast<juce::TooltipClient*>(&toolbar.getSectionToggle(section));
        ASSERT_NE(tip, nullptr);
        EXPECT_TRUE(tip->getTooltip().contains(display)) << tip->getTooltip();
    }
    rig.panel().setShortcutManager(nullptr);
}

TEST(MixerHeaderControlsTest, ReturnOnPlusBusCreatesABus) {
    RowRig rig;
    const int before = rig.panel().getColumnCount();
    auto& addBus = static_cast<juce::Component&>(rig.panel().getToolbarForTest().getAddBusButtonForTest());
    EXPECT_TRUE(addBus.keyPressed(returnKey()));
    synth::test::waitUntil([&] { return rig.panel().getColumnCount() == before + 1; });
    EXPECT_EQ(rig.panel().getColumnCount(), before + 1) << "the existing create-bus path ran";
}

TEST(MixerHeaderControlsTest, ReturnOnResetMetersRunsItsAction) {
    RowRig rig;
    int runs = 0;
    auto& toolbar = rig.panel().getToolbarForTest();
    toolbar.onResetMeters = [&] { ++runs; };
    EXPECT_TRUE(static_cast<juce::Component&>(toolbar.getResetMetersButtonForTest()).keyPressed(returnKey()));
    synth::test::waitUntil([&] { return (runs) == (1); });
    EXPECT_EQ(runs, 1);
}

TEST(MixerHeaderControlsTest, ASectionToggleActsOnReturnAndFollowsTheLayout) {
    RowRig rig;
    auto& toggle = rig.panel().getToolbarForTest().getSectionToggle(synth::ui::MixerSection::Sends);
    ASSERT_TRUE(rig.panel().getSectionLayout().isHidden(synth::ui::MixerSection::Sends) == false);
    EXPECT_TRUE(static_cast<juce::Component&>(toggle).keyPressed(returnKey()));
    synth::test::waitUntil([&] { return rig.panel().getSectionLayout().isHidden(synth::ui::MixerSection::Sends); });
    EXPECT_TRUE(rig.panel().getSectionLayout().isHidden(synth::ui::MixerSection::Sends));
}

TEST(MixerHeaderControlsTest, HeaderButtonsAlsoActOnSpace) {
    RowRig rig;
    const juce::KeyPress space(juce::KeyPress::spaceKey);
    auto& toolbar = rig.panel().getToolbarForTest();
    for (const auto section :
         {synth::ui::MixerSection::Inserts, synth::ui::MixerSection::Sends, synth::ui::MixerSection::Eq})
        EXPECT_TRUE(static_cast<juce::Component&>(toolbar.getSectionToggle(section)).keyPressed(space));
    EXPECT_TRUE(static_cast<juce::Component&>(toolbar.getAddBusButtonForTest()).keyPressed(space));
    EXPECT_TRUE(static_cast<juce::Component&>(toolbar.getResetMetersButtonForTest()).keyPressed(space));
    for (const auto chip : {synth::ui::MixerZonesPane::Chip::All, synth::ui::MixerZonesPane::Chip::Tracks,
                            synth::ui::MixerZonesPane::Chip::Buses})
        EXPECT_TRUE(
            static_cast<juce::Component&>(rig.panel().getZonesPaneForTest().getChipForTest(chip)).keyPressed(space));
}

// Keyboard reach (docs/mixer/panel.md#keyboard-navigation-and-accessibility): every control a click can
// operate in the Mixer region is reached with the region's own keys, delivered the way the OS delivers them (the real
// ComponentPeer of a native window holding the panel of an off-screen MainComponent). A column is reached with
// Left/Right, its insert, send and EQ rows with Down from it, the top bar with Cmd+Up then Right along its ring, and
// the side pane's channel list with Down once Cmd+Shift+B has opened it.
namespace {

using namespace synth::test::mixer;
using synth::ui::MixerRowKind;
using synth::ui::MixerSection;

// A track with a send (so there are send rows), a bus, every section shown, on a native window holding the keyboard.
struct ReachRig {
    MixerZonesRig r{2};
    NativeHost host{*r.panel};
    int unhandled = 0;

    ReachRig() {
        host.onUnhandledKey = [this](const juce::KeyPress&) {
            ++unhandled;
            return true;
        };
        const auto bus = r.panel->createBus();
        r.panel->rebuild();
        r.panel->getStripColumnForTest(0)->getSendList().addSendTo(bus);
        r.panel->rebuild();
        for (const auto section : {MixerSection::Inserts, MixerSection::Sends, MixerSection::Eq})
            r.panel->getSectionLayout().setHidden(section, false);
        r.panel->setSize(1400, 700);
        host.setSize(1400, 780);
        r.panel->resized();
        r.panel->grabKeyboardFocus();
    }
    bool hasKeyboard() const { return r.panel->hasKeyboardFocus(true); }
    KeyPresser presser() {
        return [this](const juce::KeyPress& key) { return pressThroughPeer(host, key); };
    }
};

bool onScreenInside(juce::Component& c, const juce::Component& root) {
    for (auto* p = &c; p != nullptr; p = p->getParentComponent()) {
        if (!p->isVisible() || p->getAlpha() <= 0.0f || p->getWidth() <= 0 || p->getHeight() <= 0)
            return false;
        if (p == &root)
            return true;
    }
    return false;
}

// What a click can operate: buttons, sliders, text fields, combo boxes and the EQ curve (a clickable painted control).
bool isOperable(juce::Component& c) {
    bool self = true, children = true;
    c.getInterceptsMouseClicks(self, children);
    if (!self || !c.isEnabled())
        return false;
    return dynamic_cast<juce::Button*>(&c) != nullptr || dynamic_cast<juce::Slider*>(&c) != nullptr ||
           dynamic_cast<juce::TextEditor*>(&c) != nullptr || dynamic_cast<juce::ComboBox*>(&c) != nullptr ||
           dynamic_cast<synth::ui::MixerEqThumbnail*>(&c) != nullptr;
}

void collect(juce::Component& root, juce::Component& node, std::vector<juce::Component*>& out) {
    for (auto* child : node.getChildren()) {
        if (!onScreenInside(*child, root))
            continue;
        if (isOperable(*child)) {
            out.push_back(child);
            continue;
        }
        collect(root, *child, out);
    }
}

std::string nameOf(juce::Component& c) {
    if (c.getTitle().isNotEmpty())
        return c.getTitle().toStdString();
    if (auto* b = dynamic_cast<juce::Button*>(&c); b != nullptr && b->getButtonText().isNotEmpty())
        return b->getButtonText().toStdString();
    return "unnamed";
}

// The controls a focused column's own keys operate (fader nudge, pan, M, S, C, Return): reached once the column is.
// The sources badge's own action (select the channel on the canvas) is the column's Return.
bool isColumnKeyControl(juce::Component& c) {
    const auto title = c.getTitle();
    return title.endsWith(" fader") || title.endsWith(" colour") || title.endsWith(" pan") || title.contains(" mute") ||
           title.contains(" solo") || title.startsWith("Plays into this channel") ||
           (title.startsWith("Edit ") && title.endsWith(" on canvas"));
}

} // namespace

// The guard for the keyboard-reach kind in the Mixer: every operable control is walked to with real key dispatch.
TEST(MixerKeyboardReachTest, EveryMixerControlIsReachedWithTheRegionsOwnKeysThroughTheRealDispatch) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    ReachRig rig;
    if (!rig.hasKeyboard())
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";
    auto& panel = *rig.r.panel;
    const auto press = rig.presser();

    std::vector<juce::Component*> controls;
    collect(panel, panel, controls);
    ASSERT_GT(controls.size(), 20u);

    std::map<std::string, int> how;
    for (auto* control : controls) {
        juce::Component::SafePointer<juce::Component> alive(control);
        const char* way = nullptr;
        if (walkTopBarTo(panel, *control, press))
            way = "top bar ring (Cmd+Up, Right)";
        else if (isColumnKeyControl(*control) && walkColumnsTo(panel, *control, press))
            way = "column keys (Left/Right)";
        else if (walkRowsTo(panel, *control, press))
            way = "rows (Down)";
        else if (walkZonesListTo(panel, *control, press))
            way = "side pane list (Cmd+Shift+B, Down)";
        leaveToColumn(panel, press);
        ASSERT_NE(alive.getComponent(), nullptr) << "a walk rebuilt the mixer";
        EXPECT_NE(way, nullptr) << "no key reaches " << nameOf(*control);
        if (way != nullptr)
            ++how[way];
        std::cout << "[ mixer reach ] " << nameOf(*control) << ": " << (way != nullptr ? way : "UNREACHED") << "\n";
    }
    EXPECT_GT(how["rows (Down)"], 0);
    EXPECT_GT(how["top bar ring (Cmd+Up, Right)"], 0);
    EXPECT_GT(how["column keys (Left/Right)"], 0);
    EXPECT_GT(how["side pane list (Cmd+Shift+B, Down)"], 0);
}

// Every row kind is a stop of the walk, in drawing order, and Up retraces it back to the column.
TEST(MixerKeyboardReachTest, DownWalksInsertsThenSendsThenTheEqAndUpWalksBackToTheColumn) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    ReachRig rig;
    if (!rig.hasKeyboard())
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";
    auto& panel = *rig.r.panel;
    const auto press = rig.presser();
    homeColumn(panel, press);
    ASSERT_EQ(panel.getFocusedColumnIndexForTest(), 0);

    std::vector<MixerRowKind> kinds;
    ASSERT_TRUE(press(walkKey(juce::KeyPress::downKey)));
    for (int guard = 0; guard < 64 && panel.getFocusedRowForTest().has_value(); ++guard) {
        const auto row = *panel.getFocusedRowForTest();
        if (kinds.empty() || kinds.back() != row.kind)
            kinds.push_back(row.kind);
        EXPECT_FALSE(panel.getFocusedRowBoundsForTest().isEmpty()) << "every row shows its focus ring";
        EXPECT_FALSE(panel.getFocusedRowDescriptionForTest().isEmpty()) << "every row is named for a screen reader";
        press(walkKey(juce::KeyPress::downKey));
        if (panel.getFocusedRowForTest() == row)
            break;
    }
    EXPECT_EQ(kinds, (std::vector<MixerRowKind>{MixerRowKind::Insert, MixerRowKind::Send, MixerRowKind::Eq}));

    for (int guard = 0; guard < 64 && panel.getFocusedRowForTest().has_value(); ++guard)
        press(walkKey(juce::KeyPress::upKey));
    EXPECT_FALSE(panel.getFocusedRowForTest().has_value()) << "Up from the first row is the column again";
    EXPECT_EQ(panel.getFocusedColumnIndexForTest(), 0);
    EXPECT_EQ(rig.unhandled, 0);
}

// Regression test for the split-row add affordances being mouse-only: Shift+F10 on a focused insert row opens that
// row's own menu, not the channel menu.
TEST(MixerRowKeyboardTest, ShiftF10OnAFocusedInsertRowOpensTheRowsOwnMenu) {
    RowRig rig;
    rig.enterFirstColumnRows();
    ASSERT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 0}));
    std::vector<juce::String> texts;
    synth::ui::MixerInsertList::menuHookForTest() = [&](juce::PopupMenu& menu, std::function<void(int)>) {
        juce::PopupMenu::MenuItemIterator it(menu);
        while (it.next())
            texts.push_back(it.getItem().text);
    };
    rig.mc.setFocusedComponentOverrideForTest(&rig.panel());
    const bool handled =
        rig.mc.keyPressed(juce::KeyPress(juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier, 0));
    synth::test::flushMessageQueue();
    synth::ui::MixerInsertList::menuHookForTest() = nullptr;
    EXPECT_TRUE(handled);
    EXPECT_NE(std::find(texts.begin(), texts.end(), "Remove"), texts.end()) << "the module row's menu, from the key";
}
