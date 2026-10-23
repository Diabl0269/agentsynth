// MixerRowMoveAndRevealTests.cpp (docs/mixer/row-motion.md, docs/mixer/edit-on-canvas.md): the keyboard move of a
// focused insert or send row (Alt+Up / Alt+Down, the rebindable "mixerMoveRowUp" / "mixerMoveRowDown") and the
// "Show on canvas" button on insert rows and menu item on send rows, on a real off-screen MainComponent.
#include "../../TestSettingsHelpers.h"
#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Mixer/MixerSendList.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::MixerRowKind;
using synth::ui::MixerRowRef;
using NodeID = juce::AudioProcessorGraph::NodeID;

class MockProviderMRMT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMRMT"; }
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
juce::KeyPress altDown() { return key(juce::KeyPress::downKey, juce::ModifierKeys::altModifier); }
juce::KeyPress altUp() { return key(juce::KeyPress::upKey, juce::ModifierKeys::altModifier); }

/** Two tracks, two buses, and two sends on the first track (slot 0 -> busA, slot 1 -> busB). */
struct MoveRig {
    synth::test::PersistedKeysGuard guard{{"bottomDockVisible", "bottomDockActiveTab", "bottomDockTabOrder",
                                           "mixerSectionInsertsHidden", "mixerSectionSendsHidden",
                                           "mixerSectionEqHidden"}};
    MainComponent mc{std::make_unique<MockProviderMRMT>()};
    NodeID busA, busB;

    MoveRig() {
        mc.setSize(1400, 900);
        mc.getBottomDock().getMixerPanel().setVisible(true);
        mc.getAudioEngine().suspendDeviceCallback();
        mc.newPatchForTest();
        mc.simulateAddAudioTrackClick();
        mc.simulateAddAudioTrackClick();
        busA = panel().createBus();
        busB = panel().createBus();
        panel().rebuild();
        panel().getStripColumnForTest(0)->getSendList().addSendTo(busA);
        panel().getStripColumnForTest(0)->getSendList().addSendTo(busB);
        panel().rebuild();
        for (const auto section :
             {synth::ui::MixerSection::Inserts, synth::ui::MixerSection::Sends, synth::ui::MixerSection::Eq})
            panel().getSectionLayout().setHidden(section, false);
        panel().setSize(1400, 700);
        panel().resized();
    }

    synth::ui::MixerPanelComponent& panel() { return mc.getBottomDock().getMixerPanel(); }
    synth::ui::MixerColumnComponent& column() { return *panel().getStripColumnForTest(0); }
    juce::AudioProcessorGraph& graph() { return mc.getAudioEngine().getGraph(); }

    void enterRows() {
        ASSERT_TRUE(panel().keyPressed(key(juce::KeyPress::rightKey)));
        ASSERT_TRUE(panel().keyPressed(key(juce::KeyPress::tabKey)));
    }

    int insertRows() { return column().getInsertList().getRowCount(); }

    void stepToFirstSend() {
        for (int i = 0; i < insertRows(); ++i)
            panel().keyPressed(key(juce::KeyPress::downKey));
        ASSERT_EQ(panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 0}));
    }

    juce::String insertUuid(int row) { return column().getInsertList().getRowUuid(row); }
};

} // namespace

TEST(MixerRowMoveTest, AltDownMovesTheFocusedInsertDownAndTheFocusFollowsIt) {
    MoveRig rig;
    rig.enterRows();
    ASSERT_GE(rig.insertRows(), 3) << "two default inserts and the + Insert row at least";
    const auto first = rig.insertUuid(0);
    const auto second = rig.insertUuid(1);
    ASSERT_FALSE(first.isEmpty());

    EXPECT_TRUE(rig.panel().keyPressed(altDown()));
    EXPECT_EQ(rig.insertUuid(0), second);
    EXPECT_EQ(rig.insertUuid(1), first);
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 1}));

    EXPECT_TRUE(rig.panel().keyPressed(altUp()));
    EXPECT_EQ(rig.insertUuid(0), first) << "and back";
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 0}));
}

TEST(MixerRowMoveTest, AMoveThatCannotHappenIsSwallowedAndChangesNothing) {
    MoveRig rig;
    rig.enterRows();
    const auto first = rig.insertUuid(0);
    EXPECT_TRUE(rig.panel().keyPressed(altUp())) << "consumed, so it never walks columns or reaches the window";
    EXPECT_EQ(rig.insertUuid(0), first);
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 0}));
}

TEST(MixerRowMoveTest, TheAddRowIsReachableByDownFromTheLastInsertAndIsNotMovable) {
    MoveRig rig;
    rig.enterRows();
    const int addRow = rig.column().getInsertList().getAddRowIndex();
    ASSERT_GE(addRow, 1);
    for (int i = 0; i < addRow; ++i)
        rig.panel().keyPressed(key(juce::KeyPress::downKey));
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, addRow}));
    EXPECT_TRUE(rig.panel().keyPressed(altUp()));
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, addRow}));
}

TEST(MixerRowMoveTest, AltDownMovesTheFocusedSendAndTheFocusFollowsIt) {
    MoveRig rig;
    rig.enterRows();
    rig.stepToFirstSend();
    const auto stripId = rig.column().getNodeId();
    ASSERT_EQ(synth::findSendTarget(rig.graph(), stripId, 0), rig.busA);

    EXPECT_TRUE(rig.panel().keyPressed(altDown()));
    EXPECT_EQ(synth::findSendTarget(rig.graph(), stripId, 0), rig.busB);
    EXPECT_EQ(synth::findSendTarget(rig.graph(), stripId, 1), rig.busA);
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 1}));

    EXPECT_TRUE(rig.panel().keyPressed(altDown())) << "already last: swallowed";
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 1}));
    EXPECT_EQ(synth::findSendTarget(rig.graph(), stripId, 1), rig.busA);
}

TEST(MixerRowMoveTest, TheMoveKeysAreRebindable) {
    MoveRig rig;
    ShortcutManager shortcuts;
    rig.panel().setShortcutManager(&shortcuts);
    rig.enterRows();
    const auto first = rig.insertUuid(0);
    shortcuts.setBinding("mixerMoveRowDown", juce::KeyPress('q', juce::ModifierKeys(), 0));
    rig.panel().keyPressed(altDown());
    EXPECT_EQ(rig.insertUuid(0), first) << "Alt+Down no longer moves";
    rig.panel().keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys(), 0)); // it only stepped a row
    rig.panel().keyPressed(juce::KeyPress('q', juce::ModifierKeys(), 0));
    EXPECT_NE(rig.insertUuid(0), first);
    rig.panel().setShortcutManager(nullptr);
}

// ---- Show on canvas ----

TEST(MixerRowRevealTest, TheInsertRowsButtonSelectsThatModuleNotTheChannelMacro) {
    MoveRig rig;
    auto& inserts = rig.column().getInsertList();
    const auto uuid = inserts.getRowUuid(0);
    ASSERT_FALSE(uuid.isEmpty());
    auto* button = inserts.getShowButtonForTest(0);
    ASSERT_NE(button, nullptr);
    button->onClick();

    auto& editor = rig.mc.getGraphEditor();
    const auto selected = editor.getSelectedNodes();
    ASSERT_EQ(selected.size(), 1u);
    auto* node = rig.graph().getNodeForId(selected.front());
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->properties["uuid"].toString(), uuid) << "the module itself";
    for (const auto& macro : editor.getMacros().getAll())
        EXPECT_FALSE(editor.getMacroController().isMacroSelected(macro.id));
}

TEST(MixerRowRevealTest, ItCentresTheModulesCardInTheCanvasView) {
    MoveRig rig;
    auto& editor = rig.mc.getGraphEditor();
    editor.setSize(1000, 700);
    const auto uuid = rig.column().getInsertList().getRowUuid(0);
    rig.panel().revealModuleOnCanvas(uuid);
    const auto selected = editor.getSelectedNodes();
    ASSERT_EQ(selected.size(), 1u);
    for (auto* card : editor.getModuleComponents())
        if (card != nullptr && card->getNodeId() == selected.front()) {
            const auto centre = editor.getLocalArea(card, card->getLocalBounds()).getCentre();
            EXPECT_NEAR(centre.x, editor.getLocalBounds().getCentreX(), 3);
            EXPECT_NEAR(centre.y, editor.getLocalBounds().getCentreY(), 3);
            return;
        }
    FAIL() << "no card for the selected module";
}

TEST(MixerRowRevealTest, AModuleInsideACollapsedMacroUnfoldsItFirst) {
    MoveRig rig;
    auto& editor = rig.mc.getGraphEditor();
    const auto uuid = rig.column().getInsertList().getRowUuid(0);
    const auto* macro = editor.getMacros().findByMember(uuid);
    ASSERT_NE(macro, nullptr) << "the channel's modules are boxed in a macro";
    const auto macroId = macro->id;
    editor.getMacroController().setMacroCollapsed(macroId, true);
    ASSERT_TRUE(editor.getMacros().find(macroId)->collapsed);

    rig.panel().revealModuleOnCanvas(uuid);
    EXPECT_FALSE(editor.getMacros().find(macroId)->collapsed) << "unfolded so the module has a card";
    ASSERT_EQ(editor.getSelectedNodes().size(), 1u);
}

/** The row menu a single click on a send row's name opens, captured through the menu hook instead of a real popup. */
juce::PopupMenu captureSendMenu(MoveRig& rig, int row) {
    juce::PopupMenu captured;
    auto& sends = rig.column().getSendListForTest();
    sends.setShowMenuHookForTest([&captured](juce::PopupMenu& menu) { captured = menu; });
    const auto point = juce::Point<float>(10.0f, (float)sends.getRowTopForTest(row) + 5.0f);
    const auto time = juce::Time::getCurrentTime();
    const juce::ModifierKeys mods(juce::ModifierKeys::leftButtonModifier);
    const juce::MouseEvent down(juce::Desktop::getInstance().getMainMouseSource(), point, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                                0.0f, &sends, &sends, time, point, time, 1, false);
    sends.mouseDown(down);
    sends.mouseUp(down);
    sends.setShowMenuHookForTest(nullptr);
    return captured;
}

TEST(MixerRowRevealTest, TheSendRowMenuShowsTheBusItFeedsOnTheCanvas) {
    MoveRig rig;
    auto& sends = rig.column().getSendListForTest();
    ASSERT_EQ(sends.getRowCountForTest(), 2);
    auto menu = captureSendMenu(rig, 1);
    juce::PopupMenu::MenuItemIterator it(menu);
    bool found = false;
    while (it.next()) {
        const auto& item = it.getItem();
        if (item.text.startsWith("Show ") && item.text.endsWith(" on canvas")) {
            found = true;
            EXPECT_TRUE(item.isEnabled);
            ASSERT_TRUE(item.action != nullptr);
            item.action();
        }
    }
    ASSERT_TRUE(found) << "the row menu has a Show <bus> on canvas item";

    const auto selected = rig.mc.getGraphEditor().getSelectedNodes();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected.front(), rig.busB) << "send row 1 feeds bus B";
}

TEST(MixerRowRevealTest, ASendRowHasNoShowButtonAndTheNameKeepsItsWidth) {
    MoveRig rig;
    auto& sends = rig.column().getSendListForTest();
    ASSERT_LE(sends.getWidth(), 140) << "at most the fixed mixer column width";
    // The name runs from the left edge to the first control; the controls take the remove, PRE/POST, M, bypass and
    // two knob widths (14 + 26 + 16 + 16 + 18 + 18) and nothing else.
    constexpr int kControlsWidth = 14 + 26 + 16 + 16 + 18 + 18;
    for (int row = 0; row < 2; ++row) {
        std::vector<juce::Rectangle<int>> controls{
            sends.getBypassButtonForTest(row)->getBounds(), sends.getMuteButtonForTest(row)->getBounds(),
            sends.getKnobForTest(row)->getBounds(), sends.getPanKnobForTest(row)->getBounds()};
        int leftmost = sends.getWidth();
        for (size_t a = 0; a < controls.size(); ++a) {
            EXPECT_FALSE(controls[a].isEmpty()) << row << "/" << a;
            EXPECT_GE(controls[a].getX(), 0);
            EXPECT_LE(controls[a].getRight(), sends.getWidth());
            leftmost = std::min(leftmost, controls[a].getX());
            for (size_t b = a + 1; b < controls.size(); ++b)
                EXPECT_FALSE(controls[a].intersects(controls[b])) << row << ": " << a << " vs " << b;
        }
        // Each control is inset by 1 px, so the leftmost starts one pixel inside the controls' strip.
        EXPECT_EQ(leftmost, sends.getWidth() - kControlsWidth + 1) << "the name area is " << leftmost << " px wide";
    }
}

TEST(MixerRowRevealTest, AnInsertRowsShowAndBypassButtonsDoNotOverlap) {
    MoveRig rig;
    auto& inserts = rig.column().getInsertList();
    int checked = 0;
    for (int row = 0; row < inserts.getRowCount(); ++row) {
        auto* show = inserts.getShowButtonForTest(row);
        auto* bypass = inserts.getBypassButtonForTest(row);
        if (show == nullptr || bypass == nullptr)
            continue;
        ++checked;
        EXPECT_FALSE(show->getBounds().isEmpty());
        EXPECT_FALSE(show->getBounds().intersects(bypass->getBounds())) << "row " << row;
        EXPECT_GE(show->getX(), 0);
        EXPECT_LE(bypass->getRight(), inserts.getWidth());
    }
    EXPECT_GE(checked, 1);
}
