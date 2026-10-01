// MixerRowKeyboardTests.cpp -- the mixer panel's keyboard walk through a column's insert and send rows
// ("mixerEnterRows"), the EQ key ("mixerOpenEq") and the header controls as Tab stops. Every key goes
// through MixerPanelComponent::keyPressed with a real juce::KeyPress, on a real off-screen
// MainComponent, the same rig style MixerPanelKeyboardFocusTests.cpp uses.
#include "../../TestSettingsHelpers.h"
#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/ModuleBase.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/BottomDockComponent.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Mixer/MixerSendList.h"
#include <gtest/gtest.h>

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

    /** Runs what a button queued: juce::Button::triggerClick posts its click as a command message. */
    static void pumpMessages() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }

    float sendLevel() { return firstStrip()->getSendLevelParameter(0)->get(); }

    void setSendLevel(float db) {
        auto* level = firstStrip()->getSendLevelParameter(0);
        level->setValueNotifyingHost(level->getNormalisableRange().convertTo0to1(db));
    }

    /** Right once: focus on the first column; Tab: into its rows (first row is the first insert). */
    void enterFirstColumnRows() {
        ASSERT_TRUE(panel().keyPressed(rightKey()));
        ASSERT_TRUE(panel().keyPressed(tabKey()));
    }

    /** Down until the send row (after the insert rows). */
    void stepToSendRow() {
        for (int i = 0; i < insertCount(); ++i)
            panel().keyPressed(downKey());
        ASSERT_EQ(panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 0}));
    }
};

} // namespace

TEST(MixerRowKeyboardTest, TabEntersTheFocusedColumnsRowsInsertsFirst) {
    RowRig rig;
    ASSERT_TRUE(rig.panel().keyPressed(rightKey()));
    EXPECT_FALSE(rig.panel().getFocusedRowForTest().has_value());

    EXPECT_TRUE(rig.panel().keyPressed(tabKey()));
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 0}));
}

TEST(MixerRowKeyboardTest, TabWithNoColumnFocusedFallsThroughToTheRegionCycle) {
    RowRig rig;
    EXPECT_FALSE(rig.panel().keyPressed(tabKey())) << "unhandled, so the app-wide region cycle still gets it";
    EXPECT_FALSE(rig.panel().getFocusedRowForTest().has_value());
}

TEST(MixerRowKeyboardTest, TabFallsThroughWhenTheSectionsHoldingRowsAreHidden) {
    RowRig rig;
    rig.panel().keyPressed(rightKey());
    rig.panel().getSectionLayout().setHidden(synth::ui::MixerSection::Inserts, true);
    rig.panel().getSectionLayout().setHidden(synth::ui::MixerSection::Sends, true);
    EXPECT_FALSE(rig.panel().keyPressed(tabKey()));

    rig.panel().getSectionLayout().setHidden(synth::ui::MixerSection::Sends, false);
    EXPECT_TRUE(rig.panel().keyPressed(tabKey())) << "a shown section with rows is enough";
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 0}));
}

TEST(MixerRowKeyboardTest, TabFallsThroughOnAColumnWithNoRows) {
    RowRig rig;
    rig.panel().keyPressed(rightKey());
    rig.panel().keyPressed(rightKey()); // the second track: inserts, but no sends
    ASSERT_EQ(rig.panel().getFocusedColumnIndexForTest(), 1);
    ASSERT_EQ(rig.panel().getStripColumnForTest(1)->getSendList().getRowCount(), 0);
    rig.panel().getSectionLayout().setHidden(synth::ui::MixerSection::Inserts, true);
    EXPECT_FALSE(rig.panel().keyPressed(tabKey()));
}

TEST(MixerRowKeyboardTest, UpAndDownStepRowsClampedAtBothEnds) {
    RowRig rig;
    rig.enterFirstColumnRows();
    EXPECT_TRUE(rig.panel().keyPressed(upKey()));
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 0})) << "clamped at the top";
    EXPECT_TRUE(rig.panel().keyPressed(downKey()));
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 1}));
    for (int i = 2; i < rig.insertCount(); ++i)
        rig.panel().keyPressed(downKey());
    EXPECT_TRUE(rig.panel().keyPressed(downKey()));
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 0})) << "inserts, then sends";
    EXPECT_TRUE(rig.panel().keyPressed(downKey()));
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 0})) << "clamped at the bottom";
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

TEST(MixerRowKeyboardTest, DeletingTheLastRowLeavesRowMode) {
    RowRig rig;
    while (rig.insertCount() > 0) {
        rig.firstColumn().getInsertList().removeRow(0);
        rig.panel().rebuild();
    }
    rig.panel().keyPressed(rightKey());
    ASSERT_TRUE(rig.panel().keyPressed(tabKey())) << "the send row is still there";
    ASSERT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 0}));

    EXPECT_TRUE(rig.panel().keyPressed(deleteKey()));
    EXPECT_FALSE(rig.panel().getFocusedRowForTest().has_value());
    EXPECT_EQ(rig.panel().getFocusedColumnIndexForTest(), 0) << "back on the column";
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

    EXPECT_FALSE(rig.panel().keyPressed(tabKey())) << "Tab no longer enters the rows";
    EXPECT_TRUE(rig.panel().keyPressed(letterKey('w')));
    EXPECT_TRUE(rig.panel().getFocusedRowForTest().has_value());
    rig.panel().setShortcutManager(nullptr);
}

TEST(MixerRowKeyboardTest, EnterRowsDefaultsToTabInTheMixerCategory) {
    ShortcutManager shortcuts;
    EXPECT_EQ(ShortcutManager::getCategory("mixerEnterRows"), ShortcutCategory::Mixer);
    EXPECT_EQ(shortcuts.getBinding("mixerEnterRows"), tabKey());
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
    RowRig::pumpMessages();
    EXPECT_EQ(rig.panel().getColumnCount(), before + 1) << "the existing create-bus path ran";
}

TEST(MixerHeaderControlsTest, ReturnOnResetMetersRunsItsAction) {
    RowRig rig;
    int runs = 0;
    auto& toolbar = rig.panel().getToolbarForTest();
    toolbar.onResetMeters = [&] { ++runs; };
    EXPECT_TRUE(static_cast<juce::Component&>(toolbar.getResetMetersButtonForTest()).keyPressed(returnKey()));
    RowRig::pumpMessages();
    EXPECT_EQ(runs, 1);
}

TEST(MixerHeaderControlsTest, ASectionToggleActsOnReturnAndFollowsTheLayout) {
    RowRig rig;
    auto& toggle = rig.panel().getToolbarForTest().getSectionToggle(synth::ui::MixerSection::Sends);
    ASSERT_TRUE(rig.panel().getSectionLayout().isHidden(synth::ui::MixerSection::Sends) == false);
    EXPECT_TRUE(static_cast<juce::Component&>(toggle).keyPressed(returnKey()));
    RowRig::pumpMessages();
    EXPECT_TRUE(rig.panel().getSectionLayout().isHidden(synth::ui::MixerSection::Sends));
}

TEST(MixerHeaderControlsTest, HeaderButtonsAlsoActOnSpace) {
    // juce::Button reacts to Space through keyStateChanged against the physical key state, which a
    // test cannot press; the registered shortcut is what makes Space count.
    RowRig rig;
    const juce::KeyPress space(juce::KeyPress::spaceKey);
    auto& toolbar = rig.panel().getToolbarForTest();
    for (const auto section :
         {synth::ui::MixerSection::Inserts, synth::ui::MixerSection::Sends, synth::ui::MixerSection::Eq})
        EXPECT_TRUE(toolbar.getSectionToggle(section).isRegisteredForShortcut(space));
    EXPECT_TRUE(toolbar.getAddBusButtonForTest().isRegisteredForShortcut(space));
    EXPECT_TRUE(toolbar.getResetMetersButtonForTest().isRegisteredForShortcut(space));
    for (const auto chip : {synth::ui::MixerZonesPane::Chip::All, synth::ui::MixerZonesPane::Chip::Tracks,
                            synth::ui::MixerZonesPane::Chip::Buses})
        EXPECT_TRUE(rig.panel().getZonesPaneForTest().getChipForTest(chip).isRegisteredForShortcut(space));
}
