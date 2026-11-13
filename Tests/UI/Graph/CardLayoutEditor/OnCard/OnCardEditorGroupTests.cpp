// OnCardEditorGroupTests.cpp -- a hidden control shown again returns to its own group, and a control dragged
// onto another group moves into it (one undo step), through the real panel rows and mouse handlers.

#include "Modules/OscillatorModule.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardCells.h"

using namespace oncard_test;

namespace {

constexpr int kCommand = juce::ModifierKeys::commandModifier;

/** The id of the layout section that lists `paramId`, or empty. */
juce::String sectionIdOf(const synth::CardLayout& layout, const juce::String& paramId) {
    for (const auto& section : layout.sections)
        for (const auto& item : section.items)
            if (const auto* param = std::get_if<synth::CardParamItem>(&item);
                param != nullptr && param->paramId == paramId)
                return section.id;
    return {};
}

struct Oscillator {
    OnCardRig rig;
    NodeID id = rig.add(std::make_unique<OscillatorModule>(), 100, 100);
    CardLayoutOnCardEditor* editor = rig.openOnCard(id);
};

} // namespace

// Regression test for FRO706: Add control put a hidden Detune in the Output group.
TEST(OnCardEditorGroups, ADetuneHiddenAndAddedBackIsInTheUnisonGroupAgain) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    hideThroughPanel(osc.rig, *osc.editor, "detune");
    ASSERT_EQ(osc.editor->getOutlineForTest("detune"), nullptr);

    auto* panel = openAddPanel(osc.rig, *osc.editor);
    ASSERT_NE(panel, nullptr);
    clickRow(*panel->getRowForTest("detune"));

    const auto layout = osc.rig.storedLayout(osc.id);
    ASSERT_TRUE(layout.has_value());
    EXPECT_FALSE(layout->hidden.contains("detune"));
    EXPECT_EQ(sectionIdOf(*layout, "detune"), "unison");
    ASSERT_NE(osc.editor->getOutlineForTest("detune"), nullptr);
    EXPECT_EQ(osc.editor->getCellRectForTest("detune").getY(), osc.editor->getCellRectForTest("unison").getY())
        << "on the Unison row";
}

TEST(OnCardEditorGroups, DraggingLevelFromOutputOntoUnisonMovesItIntoThatGroupAsOneUndoStep) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    const auto level = osc.editor->getCellRectForTest("level");
    const auto unison = osc.editor->getCellRectForTest("unison");
    const auto detune = osc.editor->getCellRectForTest("detune");
    ASSERT_LT(unison.getY(), level.getY());
    const int serial = osc.rig.canvas.undo.getEditSerial();

    Pointer pointer(*osc.editor, "level");
    pointer.moveBy({detune.getRight() + 20 - level.getCentreX(), unison.getCentreY() - level.getCentreY()}, kCommand);
    EXPECT_TRUE(osc.editor->isDraggingForTest());
    pointer.release();

    const auto layout = osc.rig.storedLayout(osc.id);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(sectionIdOf(*layout, "level"), "unison") << "now an item of the Unison group";
    EXPECT_NE(sectionIdOf(*layout, "pan"), "unison") << "Pan stayed in Output";
    const auto* item = storedItem(*layout, "level");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->at.has_value()) << "at the drop position";
    EXPECT_EQ(osc.rig.canvas.undo.getEditSerial(), serial + 1) << "one write";
    EXPECT_EQ(osc.editor->getLastAnnouncementForTest(), "Level moved to Unison");
    const auto landed = osc.editor->getCellRectForTest("level");
    for (const auto* other : {"unison", "detune"})
        EXPECT_FALSE(synth::ui::oncard::tooClose(landed, osc.editor->getCellRectForTest(other))) << other;

    ASSERT_TRUE(osc.rig.canvas.undo.undo());
    osc.editor->runQueuedSyncForTest();
    EXPECT_FALSE(osc.rig.storedLayout(osc.id).has_value()) << "undo puts everything back";
    EXPECT_EQ(osc.editor->getCellRectForTest("level"), level);
}
