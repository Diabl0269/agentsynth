// RenameKeyTests.cpp -- Option+R (and F2) rename whatever name has focus: the defaults, a focused timeline track
// header's name editor (Return commits, Escape cancels, a text editor keeps the key), a focused mixer column's header
// editor (also inside a detached mixer window) and the selected module card.
#include "../Mixer/MixerZonesTestRig.h"
#include "MainComponent/WindowKeyForward.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/Layout/DetachablePanelHost/DetachedPanelWindow.h"
#include "UI/Layout/RenameKey.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>
#include <typeinfo>

namespace {

const juce::KeyPress kOptionR('r', juce::ModifierKeys::altModifier, 0);
const juce::KeyPress kF2(juce::KeyPress::F2Key, juce::ModifierKeys::noModifiers, 0);
const juce::KeyPress kRight(juce::KeyPress::rightKey, juce::ModifierKeys::noModifiers, 0);

void hideWelcome(MainComponent& mc) {
    if (auto* ws = mc.getWelcomeScreenForTest())
        ws->setVisible(false);
}

// For a failure message: which strip columns have their name editor open, and what holds the keyboard.
juce::String renameState(synth::ui::MixerPanelComponent& panel) {
    juce::String text = "editors open in columns:";
    for (int i = 0; auto* column = panel.getStripColumnForTest(i); ++i)
        if (column->getHeaderForTest().getNameLabelForTest().getCurrentTextEditor() != nullptr)
            text << " " << i;
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    text << "; focused: " << (focused == nullptr ? juce::String("nothing") : juce::String(typeid(*focused).name()));
    if (focused != nullptr)
        text << " '" << focused->getTitle() << "'";
    return text;
}

// What a macOS Option+R really delivers: key code 'r' with the symbol as its text character.
juce::KeyPress realOptionR() { return juce::KeyPress('r', juce::ModifierKeys::altModifier, juce::juce_wchar(0xAE)); }

} // namespace

TEST(RenameKeyDefaults, OptionRIsASecondRenameKeyAndNoOtherGraphActionHoldsIt) {
    ShortcutManager manager;
    EXPECT_EQ(manager.getBinding("renameSelectedModule"), kF2);
    EXPECT_EQ(manager.getBinding("renameSelectedModuleAlt"), kOptionR);
    EXPECT_EQ(ShortcutManager::getCategory("renameSelectedModuleAlt"), ShortcutCategory::Graph);
    EXPECT_EQ(AppCommands::getCommandForAction("renameSelectedModuleAlt"), AppCommands::renameSelectedModule);
    EXPECT_EQ(ShortcutManager::getActionDescription("renameSelectedModule"), "Rename");
    EXPECT_TRUE(manager.getConflictingAction("renameSelectedModuleAlt", kOptionR).isEmpty());
    for (const auto& id : manager.getActionIds())
        if (id != "renameSelectedModuleAlt" && ShortcutManager::getCategory(id) == ShortcutCategory::Graph)
            EXPECT_FALSE(ShortcutManager::keyPressMatches(manager.getBinding(id), realOptionR())) << id;
    EXPECT_TRUE(synth::ui::isRenameKeyPress(&manager, realOptionR())) << "the symbol text char does not matter";
    EXPECT_TRUE(synth::ui::isRenameKeyPress(&manager, kF2));
    EXPECT_FALSE(synth::ui::isRenameKeyPress(&manager, juce::KeyPress('r', juce::ModifierKeys::noModifiers, 0)));
}

TEST(RenameKeyTrackHeader, OptionROpensTheFocusedTracksNameEditorAndReturnCommitsEscapeCancels) {
    MixerZonesRig r(2);
    hideWelcome(r.mc);
    auto* header = r.mc.getTimelinePanel().getTrackHeaderAt(1);
    ASSERT_NE(header, nullptr);
    const auto original = header->getNameLabel().getText();

    EXPECT_TRUE(header->keyPressed(realOptionR()));
    auto* editor = header->getNameLabel().getCurrentTextEditor();
    ASSERT_NE(editor, nullptr);
    editor->setText("Cancelled", false);
    header->getNameLabel().hideEditor(true); // the cancel the Escape key ends in (Label's own contract)
    EXPECT_EQ(header->getNameLabel().getCurrentTextEditor(), nullptr);
    EXPECT_EQ(header->getNameLabel().getText(), original) << "Escape cancels";

    EXPECT_TRUE(header->keyPressed(kF2)) << "F2 does the same";
    editor = header->getNameLabel().getCurrentTextEditor();
    ASSERT_NE(editor, nullptr);
    editor->setText("Lead", false);
    header->getNameLabel().hideEditor(false); // the commit the Return key ends in (Label's own contract)
    EXPECT_EQ(r.mc.getTimelineDoc().getTracks()[1].name, "Lead") << "Return commits the track's name";
}

TEST(RenameKeyTrackHeader, ATextEditorKeepsTheKey) {
    MixerZonesRig r(1);
    hideWelcome(r.mc);
    auto* header = r.mc.getTimelinePanel().getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    header->getNameLabel().showEditor();
    auto* editor = header->getNameLabel().getCurrentTextEditor();
    ASSERT_NE(editor, nullptr);
    editor->grabKeyboardFocus();
    if (!editor->hasKeyboardFocus(false))
        GTEST_SKIP() << "no keyboard focus without a native peer";
    EXPECT_TRUE(synth::ui::isTextEditorFocused());
    EXPECT_FALSE(r.mc.getCommandManager().invokeDirectly(AppCommands::renameSelectedModule, false))
        << "the card command refuses while a text editor has focus";
}

TEST(RenameKeyMixer, OptionROpensTheFocusedColumnsNameEditorAndCommitsTheRename) {
    MixerZonesRig r(2);
    hideWelcome(r.mc);
    r.panel->keyPressed(kRight);
    r.panel->keyPressed(kRight);
    ASSERT_EQ(r.panel->getFocusedColumnIndexForTest(), 1);
    auto* column = r.panel->getStripColumnForTest(1);
    ASSERT_NE(column, nullptr);

    juce::String committed;
    auto& header = column->getHeaderForTest();
    const auto original = header.onNameEdited;
    header.onNameEdited = [&](juce::String name) { // the column's own handler still runs
        committed = name;
        original(name);
    };

    EXPECT_TRUE(r.panel->keyPressed(realOptionR()));
    auto* editor = column->getHeaderForTest().getNameLabelForTest().getCurrentTextEditor();
    ASSERT_NE(editor, nullptr);
    editor->setText("Pad bus", false);
    header.getNameLabelForTest().hideEditor(false); // the commit the Return key ends in (Label's own contract)
    EXPECT_EQ(committed, "Pad bus") << "the typed name reaches the column's rename handler";
    // The renamed column is rebuilt (its shown name changed), so the old pointer is not used past this point.
    ASSERT_EQ(r.panel->getFocusedColumnIndexForTest(), 1) << "the rename keeps the focus on the column";
    auto* renamed = r.panel->getStripColumnForTest(1);
    ASSERT_NE(renamed, nullptr);

    EXPECT_TRUE(r.panel->keyPressed(kF2));
    EXPECT_NE(renamed->getHeaderForTest().getNameLabelForTest().getCurrentTextEditor(), nullptr)
        << "F2 does the same; " << renameState(*r.panel);
}

TEST(RenameKeyMixer, WorksInsideADetachedMixerWindow) {
    MixerZonesRig r(1);
    hideWelcome(r.mc);
    r.mc.getBottomDock().getMixerHost().setDetached(true);
    auto* window = r.mc.getBottomDock().getMixerHost().getDetachedWindowForTest();
    ASSERT_NE(window, nullptr);
    ASSERT_TRUE(window->getContentComponent()->isParentOf(r.panel));
    r.panel->keyPressed(kRight);
    ASSERT_EQ(r.panel->getFocusedColumnIndexForTest(), 0);

    EXPECT_TRUE(r.panel->keyPressed(realOptionR())) << "the focused panel inside the window answers the key";
    EXPECT_NE(r.panel->getStripColumnForTest(0)->getHeaderForTest().getNameLabelForTest().getCurrentTextEditor(),
              nullptr);
    r.mc.getBottomDock().getMixerHost().setDetached(false);
}

// Regression test: a click lands focus on a widget INSIDE a column (its colour dot, a fader, a mute button), which
// never moves the arrow-key column, so Option+R found no column to rename.
TEST(RenameKeyMixer, TheColumnHoldingFocusIsFoundFromAnyWidgetInsideIt) {
    MixerZonesRig r(2);
    hideWelcome(r.mc);
    auto* second = r.panel->getStripColumnForTest(1);
    ASSERT_NE(second, nullptr);
    auto& label = second->getHeaderForTest().getNameLabelForTest();
    ASSERT_TRUE(second->isParentOf(&label));
    EXPECT_EQ(r.panel->getColumnIndexContainingForTest(&label), 1) << "a widget deep in the column";
    EXPECT_EQ(r.panel->getColumnIndexContainingForTest(second), 1) << "the column itself";
    EXPECT_EQ(r.panel->getColumnIndexContainingForTest(&r.mc), r.panel->getFocusedColumnIndexForTest())
        << "focus outside the panel falls back to the arrow-key column";
    EXPECT_EQ(r.panel->getColumnIndexContainingForTest(nullptr), r.panel->getFocusedColumnIndexForTest());
}

// Regression test: with focus on nothing but the window (right after launch, or once an in-place editor closed),
// a key landed on the window and died, so Cmd+2 did nothing. The window forwards it to MainComponent.
TEST(RenameKeyWindowForward, AKeyOnTheWindowRunsTheShortcutTable) {
    MixerZonesRig r(1);
    hideWelcome(r.mc);
    auto& dock = r.mc.getBottomDock();
    const auto second = dock.tabAtKeyPosition(2);
    ASSERT_TRUE(second.has_value());
    ASSERT_NE(dock.getActiveTab(), *second);
    const juce::KeyPress cmd2('2', juce::ModifierKeys::commandModifier, '2');
    EXPECT_TRUE(synth::forwardWindowKeyToMainComponent(&r.mc, cmd2));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    EXPECT_EQ(dock.getActiveTab(), *second);
    juce::Component notMain;
    EXPECT_FALSE(synth::forwardWindowKeyToMainComponent(&notMain, cmd2));
}
