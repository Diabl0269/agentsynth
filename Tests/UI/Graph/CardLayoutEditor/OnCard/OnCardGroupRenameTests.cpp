// OnCardGroupRenameTests.cpp -- editing a group's title in place in Edit Layout: double-click, Return or the
// menu's Rename group opens the inline editor; Return keeps, Esc cancels, an empty text reverts; each rename is one
// undo step.

#include "Modules/OscillatorModule.h"
#include "OnCardTestHelpers.h"

using namespace oncard_test;

namespace {

struct Oscillator {
    OnCardRig rig;
    NodeID id = rig.add(std::make_unique<OscillatorModule>(), 100, 100);
    CardLayoutOnCardEditor* editor = rig.openOnCard(id);

    int serial() { return rig.canvas.undo.getEditSerial(); }

    /** The text of the title row the card draws for section `sectionId`. */
    juce::String shownTitle(const juce::String& sectionId) {
        for (const auto& section : rig.card(id)->getCardBody()->getPlan().sections)
            if (section.id == sectionId)
                if (const auto* label = dynamic_cast<juce::Label*>(section.header))
                    return label->getText();
        return {};
    }

    /** Opens the editor the way a double-click on the title does. */
    juce::TextEditor* doubleClickTitle(const juce::String& sectionId) {
        auto* handle = editor->getGroupHandleForTest(sectionId);
        handle->mouseDoubleClick(cardbody_test::mouseAt(*handle, handle->getTitleRowArea().getCentre().toFloat(),
                                                        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 2));
        return editor->getRenameLabelForTest().getCurrentTextEditor();
    }
};

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

} // namespace

TEST(OnCardGroupRename, DoubleClickTypeAndEnterRenamesTheGroupAsOneUndoStep) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    const int serial = osc.serial();
    auto* text = osc.doubleClickTitle("unison");
    ASSERT_NE(text, nullptr) << "the inline editor opens over the title";
    EXPECT_EQ(text->getText(), "Unison");
    EXPECT_EQ(osc.editor->getRenameLabelForTest().getBounds(),
              osc.rig.card(osc.id)->getCardBody()->getPlan().sections[2].header->getBounds())
        << "right over the title";

    text->setText("Voices", false);
    EXPECT_TRUE(text->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    pump();

    EXPECT_EQ(osc.shownTitle("unison"), "Voices");
    EXPECT_EQ(osc.editor->getGroupHandleForTest("unison")->getTitle(), "Voices group");
    EXPECT_EQ(osc.serial(), serial + 1);
    EXPECT_EQ(osc.editor->getRenameLabelForTest().getCurrentTextEditor(), nullptr);
    EXPECT_FALSE(osc.editor->getRenameLabelForTest().isVisible());
    const auto layout = osc.rig.storedLayout(osc.id);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->sections[2].title, juce::String("Voices"));

    ASSERT_TRUE(osc.rig.canvas.undo.undo());
    osc.editor->runQueuedSyncForTest();
    EXPECT_EQ(osc.shownTitle("unison"), "Unison") << "one Cmd+Z puts the name back";
    EXPECT_EQ(osc.editor->getGroupHandleForTest("unison")->getTitle(), "Unison group");
    ASSERT_TRUE(osc.rig.canvas.undo.redo());
    osc.editor->runQueuedSyncForTest();
    EXPECT_EQ(osc.shownTitle("unison"), "Voices");
}

TEST(OnCardGroupRename, EscapeCancelsAndAnEmptyTextRevertsWithNothingWritten) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    const int serial = osc.serial();

    auto* text = osc.doubleClickTitle("unison");
    ASSERT_NE(text, nullptr);
    text->setText("Nonsense", false);
    EXPECT_TRUE(text->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    pump();
    EXPECT_EQ(osc.editor->getRenameLabelForTest().getCurrentTextEditor(), nullptr);
    EXPECT_EQ(osc.shownTitle("unison"), "Unison");
    EXPECT_EQ(osc.serial(), serial);

    text = osc.doubleClickTitle("unison");
    ASSERT_NE(text, nullptr);
    text->setText("   ", false);
    EXPECT_TRUE(text->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    pump();
    EXPECT_EQ(osc.shownTitle("unison"), "Unison") << "an empty name reverts";
    EXPECT_EQ(osc.serial(), serial);

    text = osc.doubleClickTitle("unison");
    ASSERT_NE(text, nullptr);
    EXPECT_TRUE(text->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    pump();
    EXPECT_EQ(osc.serial(), serial) << "keeping the same name writes nothing";
    EXPECT_FALSE(osc.rig.storedLayout(osc.id).has_value());
}

TEST(OnCardGroupRename, ReturnOnAFocusedTitleAndTheMenusRenameGroupOpenTheSameEditor) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    auto* handle = osc.editor->getGroupHandleForTest("output");
    EXPECT_TRUE(handle->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    ASSERT_NE(osc.editor->getRenameLabelForTest().getCurrentTextEditor(), nullptr);
    osc.editor->getRenameLabelForTest().hideEditor(true);

    const auto menu = handle->buildMenu();
    const auto* item = cardbody_test::menuItem(menu, "Rename group");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->isEnabled);
    item->action();
    auto* text = osc.editor->getRenameLabelForTest().getCurrentTextEditor();
    ASSERT_NE(text, nullptr);
    text->setText("Master", false);
    EXPECT_TRUE(text->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    pump();
    EXPECT_EQ(osc.shownTitle("output"), "Master");
    EXPECT_EQ(osc.editor->getLastAnnouncementForTest(), "Group renamed to Master");
}

TEST(OnCardGroupRename, ARenamedGroupStillMovesAndKeepsItsNewName) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    auto* text = osc.doubleClickTitle("unison");
    ASSERT_NE(text, nullptr);
    text->setText("Voices", false);
    text->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    pump();
    osc.editor->getGroupHandleForTest("unison")->keyPressed(
        juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys(juce::ModifierKeys::altModifier), 0));
    EXPECT_EQ(osc.editor->getLastAnnouncementForTest(), "Voices moved below Output");
    EXPECT_EQ(osc.shownTitle("unison"), "Voices");
}
