// MixerRowBypassTests.cpp (docs/mixer/panel.md#bypassing-a-row): every insert row and send row has a bypass icon
// button, and with a row focused the rebindable "mixerToggleRowBypass" key (B) does the same. An insert maps to its
// module's bypass, a send to its own persisted bypass bit; both are one undo step, bypassed rows read dimmed, and the
// buttons carry a name that states the state and a tooltip that names the key. A real off-screen MainComponent, every
// key through MixerPanelComponent::keyPressed with a real juce::KeyPress.
#include "MixerHeaderTestRig.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerInsertList.h"
#include "UI/Mixer/MixerSendList.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>

namespace {

using mixer_header_test::clickThroughMouse;
using mixer_header_test::HeaderRig;
using synth::ui::MixerRowKind;
using synth::ui::MixerRowRef;

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }
juce::KeyPress letterKey(juce::juce_wchar c) { return juce::KeyPress(c, juce::ModifierKeys(), 0); }

/** Whether insert row `row` of the first strip's column is bypassed in the graph itself (not in any list's cache). */
bool insertBypassed(HeaderRig& rig, int row) {
    const auto snapshot =
        synth::buildMixerSnapshot(rig.mc.getAudioEngine().getGraph(), rig.doc(), rig.mc.getGraphEditor().getMacros());
    for (const auto& column : snapshot.columns)
        if (column.nodeId == rig.firstColumn().getNodeId())
            return column.inserts.at((size_t)row).bypassed;
    ADD_FAILURE() << "no snapshot column for the first strip";
    return false;
}

bool sendBypassed(HeaderRig& rig) { return rig.strip(rig.firstColumn().getNodeId())->isSendBypassed(0); }

/** Right once: focus on the first column; Tab: into its rows (the first row is the first insert). */
void enterRows(HeaderRig& rig) {
    ASSERT_TRUE(rig.panel().keyPressed(key(juce::KeyPress::rightKey)));
    ASSERT_TRUE(rig.panel().keyPressed(key(juce::KeyPress::tabKey)));
}

void stepToSendRow(HeaderRig& rig) {
    const int inserts = rig.firstColumn().getInsertList().getRowCount();
    for (int i = 0; i < inserts; ++i)
        rig.panel().keyPressed(key(juce::KeyPress::downKey));
    ASSERT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 0}));
}

} // namespace

TEST(MixerRowBypassTest, EveryInsertAndSendRowHasANamedBypassButtonWhoseTooltipNamesTheKey) {
    HeaderRig rig;
    auto& inserts = rig.firstColumn().getInsertList();
    ASSERT_GE(inserts.getRowCount(), 2);
    for (int row = 0; row < inserts.getRowCount(); ++row) {
        auto* button = inserts.getBypassButtonForTest(row);
        ASSERT_NE(button, nullptr) << "insert row " << row;
        EXPECT_TRUE(button->isVisible());
        EXPECT_TRUE(button->getWantsKeyboardFocus());
        EXPECT_TRUE(button->getTitle().contains(" bypass, ")) << button->getTitle();
        EXPECT_TRUE(button->getTitle().endsWith(insertBypassed(rig, row) ? "on" : "off")) << button->getTitle();
        EXPECT_TRUE(button->getTooltip().contains("(B)")) << "the tooltip names the key: " << button->getTooltip();
        EXPECT_TRUE(inserts.getRowBounds(row).contains(button->getBounds())) << "it sits inside its own row";
    }
    auto* send = rig.firstColumn().getSendList().getBypassButtonForTest(0);
    ASSERT_NE(send, nullptr);
    EXPECT_TRUE(send->getTitle().startsWith("Bypass send to "));
    EXPECT_TRUE(send->getTitle().endsWith(", off"));
    EXPECT_TRUE(send->getTooltip().contains("(B)")) << send->getTooltip();
    EXPECT_TRUE(send->getWantsKeyboardFocus());
}

TEST(MixerRowBypassTest, TheBypassButtonsAlsoActOnSpace) {
    HeaderRig rig;
    const juce::KeyPress space(juce::KeyPress::spaceKey);
    EXPECT_TRUE(rig.firstColumn().getInsertList().getBypassButtonForTest(0)->isRegisteredForShortcut(space));
    EXPECT_TRUE(rig.firstColumn().getSendList().getBypassButtonForTest(0)->isRegisteredForShortcut(space));
}

TEST(MixerRowBypassTest, TheTooltipFollowsARebindOfTheKeyWithoutARebuild) {
    HeaderRig rig;
    ShortcutManager shortcuts;
    rig.panel().setShortcutManager(&shortcuts);
    rig.panel().rebuild();
    auto* button = rig.firstColumn().getInsertList().getBypassButtonForTest(0);
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->getTooltip().contains("(B)"));

    shortcuts.setBinding("mixerToggleRowBypass", letterKey('q'));
    EXPECT_TRUE(button->getTooltip().contains("(Q)")) << button->getTooltip();
    EXPECT_FALSE(button->getTooltip().contains("(B)"));
    rig.panel().setShortcutManager(nullptr);
}

TEST(MixerRowBypassTest, ClickingAnInsertsBypassButtonTogglesItsModuleAsOneUndoStep) {
    HeaderRig rig;
    const bool before = insertBypassed(rig, 0);

    clickThroughMouse(*rig.firstColumn().getInsertList().getBypassButtonForTest(0));
    EXPECT_EQ(insertBypassed(rig, 0), !before);
    EXPECT_EQ(rig.firstColumn().getInsertList().describeRow(0).endsWith(", bypassed"), !before)
        << rig.firstColumn().getInsertList().describeRow(0);
    EXPECT_EQ(rig.firstColumn().getInsertList().getBypassButtonForTest(0)->getToggleState(), !before)
        << "the rebuilt row shows the new state";

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    EXPECT_EQ(insertBypassed(rig, 0), before) << "one undo step puts the module back";
}

TEST(MixerRowBypassTest, ReturnOnAFocusedBypassButtonTogglesTooAndTheButtonLeavesTheOtherKeysToThePanel) {
    HeaderRig rig;
    const bool before = insertBypassed(rig, 1);
    auto* button = rig.firstColumn().getInsertList().getBypassButtonForTest(1);
    ASSERT_NE(button, nullptr);

    // A juce::Button swallows only Return and Space; B and the arrows bubble up to the mixer panel's own key handling.
    EXPECT_FALSE(static_cast<juce::Component&>(*button).keyPressed(letterKey('b')));
    EXPECT_FALSE(static_cast<juce::Component&>(*button).keyPressed(key(juce::KeyPress::upKey)));
    EXPECT_FALSE(static_cast<juce::Component&>(*button).keyPressed(key(juce::KeyPress::leftKey)));
    EXPECT_EQ(insertBypassed(rig, 1), before) << "none of those acted on the button";

    EXPECT_TRUE(static_cast<juce::Component&>(*button).keyPressed(key(juce::KeyPress::returnKey)));
    HeaderRig::pumpMessages();
    EXPECT_EQ(insertBypassed(rig, 1), !before);
}

TEST(MixerRowBypassTest, BTogglesTheFocusedInsertRowAndUndoRestoresIt) {
    HeaderRig rig;
    enterRows(rig);
    ASSERT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 0}));
    const bool before = insertBypassed(rig, 0);

    EXPECT_TRUE(rig.panel().keyPressed(letterKey('b')));
    EXPECT_EQ(insertBypassed(rig, 0), !before);
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Insert, 0})) << "still in row mode";
    EXPECT_EQ(rig.panel().getFocusedRowDescriptionForTest().endsWith(", bypassed"), !before)
        << "the row's screen-reader text follows: " << rig.panel().getFocusedRowDescriptionForTest();

    EXPECT_TRUE(rig.panel().keyPressed(letterKey('b')));
    EXPECT_EQ(insertBypassed(rig, 0), before) << "B again toggles it back";

    EXPECT_TRUE(rig.panel().keyPressed(letterKey('b')));
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    EXPECT_EQ(insertBypassed(rig, 0), before) << "each press is one undo step";
}

TEST(MixerRowBypassTest, BTogglesTheFocusedSendRowAndUndoRestoresIt) {
    HeaderRig rig;
    enterRows(rig);
    stepToSendRow(rig);
    ASSERT_FALSE(sendBypassed(rig));

    EXPECT_TRUE(rig.panel().keyPressed(letterKey('b')));
    EXPECT_TRUE(sendBypassed(rig));
    EXPECT_FALSE(rig.strip(rig.firstColumn().getNodeId())->isSendMuted(0)) << "bypass is not mute";
    EXPECT_EQ(rig.panel().getFocusedRowForTest(), (MixerRowRef{MixerRowKind::Send, 0}));
    EXPECT_TRUE(rig.panel().getFocusedRowDescriptionForTest().endsWith(", bypassed"))
        << rig.panel().getFocusedRowDescriptionForTest();
    EXPECT_TRUE(rig.firstColumn().getSendList().getBypassButtonForTest(0)->getTitle().endsWith(", on"));

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    EXPECT_FALSE(sendBypassed(rig)) << "one undo step";
    rig.panel().rebuild();
    EXPECT_TRUE(rig.firstColumn().getSendList().getBypassButtonForTest(0)->getTitle().endsWith(", off"));
}

TEST(MixerRowBypassTest, ASendsBypassSurvivesSaveAndLoadAndLeavesItsLevelAlone) {
    HeaderRig rig;
    auto* strip = rig.strip(rig.firstColumn().getNodeId());
    auto* level = strip->getSendLevelParameter(0);
    level->setValueNotifyingHost(level->getNormalisableRange().convertTo0to1(-9.0f));
    clickThroughMouse(*rig.firstColumn().getSendList().getBypassButtonForTest(0));
    ASSERT_TRUE(rig.strip(rig.firstColumn().getNodeId())->isSendBypassed(0));

    ChannelStripModule restored;
    restored.setExtraState(rig.strip(rig.firstColumn().getNodeId())->getExtraState());
    EXPECT_TRUE(restored.isSendBypassed(0));
    EXPECT_NEAR(rig.strip(rig.firstColumn().getNodeId())->getSendLevelParameter(0)->get(), -9.0f, 0.01f);
}

TEST(MixerRowBypassTest, ABypassedSendRowPaintsItsControlsDimmed) {
    HeaderRig rig;
    auto& sends = rig.firstColumn().getSendList();
    ASSERT_NE(sends.getKnobForTest(0), nullptr);
    EXPECT_FALSE(synth::theme::AppLookAndFeel::paintsDimmed(*sends.getKnobForTest(0)));

    clickThroughMouse(*sends.getBypassButtonForTest(0));
    auto& rebuilt = rig.firstColumn().getSendList();
    EXPECT_TRUE(synth::theme::AppLookAndFeel::paintsDimmed(*rebuilt.getKnobForTest(0)));
    EXPECT_TRUE(synth::theme::AppLookAndFeel::paintsDimmed(*rebuilt.getPanKnobForTest(0)));
    EXPECT_TRUE(synth::theme::AppLookAndFeel::paintsDimmed(*rebuilt.getMuteButtonForTest(0)));
    EXPECT_FALSE(synth::theme::AppLookAndFeel::paintsDimmed(*rebuilt.getBypassButtonForTest(0)))
        << "the bypass button itself stays full strength: it is how the row comes back";
    EXPECT_TRUE(rebuilt.getKnobForTest(0)->isEnabled()) << "dimmed, not disabled";
}

TEST(MixerRowBypassTest, BDoesNothingInColumnModeOrWithNothingFocused) {
    HeaderRig rig;
    const bool insertBefore = insertBypassed(rig, 0);
    EXPECT_FALSE(rig.panel().keyPressed(letterKey('b'))) << "nothing focused";

    ASSERT_TRUE(rig.panel().keyPressed(key(juce::KeyPress::rightKey)));
    EXPECT_FALSE(rig.panel().keyPressed(letterKey('b'))) << "a column is focused, but no row";
    EXPECT_EQ(insertBypassed(rig, 0), insertBefore);
    EXPECT_FALSE(sendBypassed(rig));
}

TEST(MixerRowBypassTest, TheKeyIsRebindableAndDefaultsToBInTheMixerCategory) {
    ShortcutManager shortcuts;
    EXPECT_EQ(ShortcutManager::getCategory("mixerToggleRowBypass"), ShortcutCategory::Mixer);
    EXPECT_EQ(shortcuts.getBinding("mixerToggleRowBypass"), letterKey('b'));
    EXPECT_NE(ShortcutManager::getActionDescription("mixerToggleRowBypass"), juce::String("mixerToggleRowBypass"));

    HeaderRig rig;
    rig.panel().setShortcutManager(&shortcuts);
    shortcuts.setBinding("mixerToggleRowBypass", letterKey('q'));
    enterRows(rig);
    const bool before = insertBypassed(rig, 0);

    EXPECT_FALSE(rig.panel().keyPressed(letterKey('b'))) << "B no longer bypasses";
    EXPECT_EQ(insertBypassed(rig, 0), before);
    EXPECT_TRUE(rig.panel().keyPressed(letterKey('q')));
    EXPECT_EQ(insertBypassed(rig, 0), !before);
    rig.panel().setShortcutManager(nullptr);
}

TEST(MixerRowBypassTest, TheMasterInsertRowsHaveBypassButtonsThatToggleTheirModule) {
    HeaderRig rig;
    ASSERT_NE(rig.panel().getMasterColumnForTest(), nullptr);
    rig.panel().getMasterColumnForTest()->getInsertList().addModule("Limiter");
    rig.panel().rebuild();
    auto& inserts = rig.panel().getMasterColumnForTest()->getInsertList();
    if (inserts.getRowCount() == 0)
        GTEST_SKIP() << "this rig's Master has no chain terminator to splice an insert into";

    auto* button = inserts.getBypassButtonForTest(0);
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->getTitle().endsWith(", off"));
    clickThroughMouse(*button);
    EXPECT_TRUE(rig.panel().getMasterColumnForTest()->getInsertList().describeRow(0).endsWith(", bypassed"));
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    rig.panel().rebuild();
    EXPECT_FALSE(rig.panel().getMasterColumnForTest()->getInsertList().describeRow(0).endsWith(", bypassed"));
}
