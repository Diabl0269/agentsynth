// MixerRowBypassTests.cpp (docs/mixer/panel.md#bypassing-a-row): every insert row and send row has a bypass icon
// button, and with a row focused the rebindable "mixerToggleRowBypass" key (B) does the same. An insert maps to its
// module's bypass, a send to its own persisted bypass bit; both are one undo step, bypassed rows read dimmed, and the
// buttons carry a name that states the state and a tooltip that names the key. A real off-screen MainComponent, every
// key through MixerPanelComponent::keyPressed with a real juce::KeyPress.
#include "../../TestWait.h"
#include "MixerHeaderTestRig.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerInsertList/MixerInsertList.h"
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
    ASSERT_TRUE(rig.panel().keyPressed(key(juce::KeyPress::downKey)));
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
        if (row == inserts.getAddRowIndex())
            continue; // "+ Insert" adds, it has nothing to bypass
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
    EXPECT_TRUE(rig.firstColumn().getInsertList().getBypassButtonForTest(0)->keyPressed(space));
    EXPECT_TRUE(rig.firstColumn().getSendList().getBypassButtonForTest(0)->keyPressed(space));
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
    synth::test::waitUntil([&] { return insertBypassed(rig, 1) == !before; });
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
    if (inserts.getEntryCountForTest() == 0)
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

// MixerRebuildKeepsView (docs/mixer/panel.md#shared-sections): an edit in a strip's insert or send list
// rebuilds the column as new objects, and the new lists must not start at the top. Bug kind "rebuild-resets-view": the
// guard walks every scrollable mixer section (a track's inserts and sends, Master's inserts) through every edit kind
// (bypass, un-bypass, move, add, remove, undo, redo) via the real panel rebuild and holds the section's scroll where
// it was (clamped only when the list got shorter), then the keyboard path holds the focused row.
namespace {

using mixer_header_test::HeaderRig;
using synth::ui::MixerRowKind;
using synth::ui::MixerRowRef;
using synth::ui::MixerSection;
using synth::ui::MixerSectionViewport;

enum class Where { TrackInserts, TrackSends, MasterInserts };

const char* nameOf(Where where) {
    return where == Where::TrackInserts ? "track inserts"
           : where == Where::TrackSends ? "track sends"
                                        : "master inserts";
}

juce::KeyPress plainKey(int code) { return juce::KeyPress(code, juce::ModifierKeys(), 0); }

// One scrollable section of the rig's mixer, re-found on every call because an edit replaces the column's lists.
struct Surface {
    HeaderRig& rig;
    Where where;
    std::vector<juce::AudioProcessorGraph::NodeID> spareBuses;

    synth::ui::MixerInsertList& inserts() {
        return where == Where::MasterInserts ? rig.panel().getMasterColumnForTest()->getInsertList()
                                             : rig.firstColumn().getInsertList();
    }
    synth::ui::MixerSendList& sends() { return rig.firstColumn().getSendList(); }
    MixerSectionViewport& viewport() {
        if (where == Where::MasterInserts)
            return rig.panel().getMasterColumnForTest()->getInsertViewportForTest();
        return rig.firstColumn().getSectionViewportForTest(where == Where::TrackSends ? MixerSection::Sends
                                                                                      : MixerSection::Inserts);
    }
    int maxScroll() {
        auto& vp = viewport();
        return juce::jmax(0, vp.getViewedComponent()->getHeight() - vp.getHeight());
    }
    int lastRow() { return where == Where::TrackSends ? sends().getRowCount() - 1 : inserts().getAddRowIndex() - 1; }

    void grow() {
        for (int i = 0; i < 30 && !viewport().isScrollable(); ++i)
            add();
    }
    void add() {
        if (where == Where::TrackSends) {
            ASSERT_FALSE(spareBuses.empty()) << "ran out of buses to send to";
            sends().addSendTo(spareBuses.back());
            spareBuses.pop_back();
        } else {
            inserts().addModule("Reverb");
        }
    }
    void toggleBypass() {
        if (where == Where::TrackSends)
            sends().toggleBypassForRow(lastRow());
        else
            inserts().toggleBypassForRow(lastRow());
    }
    void move() {
        const int row = lastRow();
        if (where == Where::TrackSends)
            ASSERT_TRUE(sends().moveRow(row, row - 1));
        else
            ASSERT_TRUE(inserts().moveRowTo(row, row - 1));
    }
    void remove() {
        if (where == Where::TrackSends)
            sends().removeRow(lastRow());
        else
            inserts().removeRow(lastRow());
    }
};

// The panel's own rebuild path after a restore (the structure-changed catch-up is queued; this is what it runs).
void undoOnce(HeaderRig& rig) {
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    rig.panel().rebuild();
}
void redo(HeaderRig& rig) {
    ASSERT_TRUE(rig.mc.getUndoManager().redo());
    rig.panel().rebuild();
}

void walkEveryEditKind(Where where, bool scrolledToBottom) {
    HeaderRig rig;
    rig.panel().setVisible(true); // a hidden mixer defers its rebuilds
    Surface surface{rig, where, {}};
    for (int i = 0; i < 24; ++i)
        surface.spareBuses.push_back(rig.panel().createBus());
    rig.panel().rebuild();
    if (where == Where::MasterInserts) {
        rig.panel().getMasterColumnForTest()->getInsertList().addModule("Reverb");
        if (rig.panel().getMasterColumnForTest()->getInsertList().getEntryCountForTest() == 0)
            GTEST_SKIP() << "this rig's Master has no chain terminator to splice an insert into";
    }
    surface.grow();
    ASSERT_TRUE(surface.viewport().isScrollable())
        << nameOf(where) << " never outgrew its frame: vp h=" << surface.viewport().getHeight()
        << " rows=" << (where == Where::TrackSends ? surface.sends().getRowCount() : surface.inserts().getRowCount())
        << " content h=" << surface.viewport().getViewedComponent()->getHeight();

    const std::pair<const char*, std::function<void()>> edits[] = {
        {"bypass", [&] { surface.toggleBypass(); }},
        {"un-bypass", [&] { surface.toggleBypass(); }},
        {"move", [&] { surface.move(); }},
        {"add", [&] { surface.add(); }},
        {"remove", [&] { surface.remove(); }},
        {"undo", [&] { undoOnce(rig); }},
        {"redo", [&] { redo(rig); }},
    };
    for (const auto& [editName, run] : edits) {
        SCOPED_TRACE(juce::String(nameOf(where)) + " / " + editName + (scrolledToBottom ? " / at the bottom" : ""));
        surface.viewport().scrollToY(scrolledToBottom ? surface.maxScroll() : surface.maxScroll() / 2 + 1);
        const int before = surface.viewport().getViewPositionY();
        ASSERT_GT(before, 0) << "the section is scrolled before the edit";
        run();
        const int expected = juce::jmin(before, surface.maxScroll());
        EXPECT_EQ(surface.viewport().getViewPositionY(), expected)
            << "the edit rebuilt the column and the list jumped (was " << before << ")";
    }
}

} // namespace

TEST(MixerRebuildKeepsView, TrackInsertsStayWhereTheyWereScrolledForEveryEdit) {
    walkEveryEditKind(Where::TrackInserts, false);
    walkEveryEditKind(Where::TrackInserts, true);
}

TEST(MixerRebuildKeepsView, TrackSendsStayWhereTheyWereScrolledForEveryEdit) {
    walkEveryEditKind(Where::TrackSends, false);
    walkEveryEditKind(Where::TrackSends, true);
}

TEST(MixerRebuildKeepsView, MasterInsertsStayWhereTheyWereScrolledForEveryEdit) {
    walkEveryEditKind(Where::MasterInserts, false);
    walkEveryEditKind(Where::MasterInserts, true);
}

// The keyboard path: Space or B on a focused row bypasses it, the rebuild keeps the row focused and the list where it
// was, and the row is still in view.
TEST(MixerRebuildKeepsView, AFocusedRowStaysFocusedAndInViewAfterABypassFromTheKeyboard) {
    for (const auto where : {Where::TrackInserts, Where::TrackSends}) {
        SCOPED_TRACE(nameOf(where));
        HeaderRig rig;
        rig.panel().setVisible(true);
        Surface surface{rig, where, {}};
        for (int i = 0; i < 24; ++i)
            surface.spareBuses.push_back(rig.panel().createBus());
        rig.panel().rebuild();
        surface.grow();
        ASSERT_TRUE(surface.viewport().isScrollable());

        ASSERT_TRUE(rig.panel().keyPressed(plainKey(juce::KeyPress::rightKey)));
        ASSERT_TRUE(rig.panel().keyPressed(plainKey(juce::KeyPress::downKey)));
        const auto wanted =
            MixerRowRef{where == Where::TrackSends ? MixerRowKind::Send : MixerRowKind::Insert, surface.lastRow()};
        for (int i = 0; i < 80 && rig.panel().getFocusedRowForTest() != wanted; ++i)
            rig.panel().keyPressed(plainKey(juce::KeyPress::downKey));
        ASSERT_EQ(rig.panel().getFocusedRowForTest(), wanted);
        surface.viewport().scrollToY(surface.maxScroll());
        const int before = surface.viewport().getViewPositionY();
        ASSERT_GT(before, 0) << "the section is scrolled";

        EXPECT_TRUE(rig.panel().keyPressed(juce::KeyPress('b', juce::ModifierKeys(), 0)));
        EXPECT_EQ(rig.panel().getFocusedRowForTest(), wanted) << "the focused row stays focused";
        EXPECT_EQ(surface.viewport().getViewPositionY(), before);
        const auto bounds = where == Where::TrackSends ? surface.sends().getRowBounds(wanted.index)
                                                       : surface.inserts().getRowBounds(wanted.index);
        const auto frame = surface.viewport().getViewArea();
        EXPECT_TRUE(frame.contains(bounds)) << "the focused row is still in view";
    }
}
