// OnCardGroupHitTests.cpp -- a group's title row in Edit Layout reached the way a real mouse reaches it: the component
// JUCE's hit-testing finds under the pointer gets the press, drag and double-click, and Return on the focused handle.

#include "Modules/OscillatorModule.h"
#include "OnCardTestHelpers.h"

using namespace oncard_test;

namespace {

struct Oscillator {
    OnCardRig rig;
    NodeID id = rig.add(std::make_unique<OscillatorModule>(), 100, 100);
    CardLayoutOnCardEditor* editor = rig.openOnCard(id);

    /** The component under `p` (in the editor's coordinates), found from the editor's parent as a real click is. */
    juce::Component* under(juce::Point<int> p) {
        auto* parent = editor->getParentComponent();
        return parent->getComponentAt(parent->getLocalPoint(editor, p));
    }

    /** The title row's centre in the editor's coordinates. */
    juce::Point<int> titleCentre(const juce::String& sectionId) {
        auto* handle = editor->getGroupHandleForTest(sectionId);
        return handle->getPosition() + handle->getTitleRowArea().getCentre();
    }

    /** The event a real mouse at `p` (editor coordinates) sends to `target`. */
    juce::MouseEvent eventAt(juce::Component& target, juce::Point<int> p, int clicks = 1) {
        return cardbody_test::mouseAt(target, target.getLocalPoint(editor, p).toFloat(),
                                      juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), clicks);
    }
};

/** Lets every glide the editor started run to its end. */
void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(500); }

/** Every title the card draws, found from the drawn header component (not from the handle): the component under
 *  it must be that group's own handle. */
void expectDrawnTitlesReachTheirHandles(Oscillator& osc, const juce::String& when) {
    int checked = 0;
    for (const auto& section : osc.rig.card(osc.id)->getCardBody()->getPlan().sections) {
        if (!section.hasHeader())
            continue;
        auto* handle = osc.editor->getGroupHandleForTest(section.id);
        ASSERT_NE(handle, nullptr) << when << ": " << section.id;
        const auto drawn = osc.editor->getLocalArea(section.header->getParentComponent(), section.header->getBounds());
        const auto row = handle->getTitleRowArea().translated(handle->getX(), handle->getY());
        EXPECT_TRUE(row.intersects(drawn))
            << when << ": " << section.id << " handle row " << row.toString() << " drawn " << drawn.toString();
        EXPECT_EQ(osc.under(drawn.getCentre()), handle) << when << ": " << section.id;
        ++checked;
    }
    EXPECT_GE(checked, 3);
}

} // namespace

TEST(OnCardGroupHit, ThePointerOnTheTitleRowFindsTheGroupHandleAndNotACell) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    auto* handle = osc.editor->getGroupHandleForTest("unison");
    ASSERT_NE(handle, nullptr);
    const auto title = osc.titleCentre("unison");
    auto* found = osc.under(title);
    EXPECT_EQ(found, handle) << "got " << (found != nullptr ? found->getName() : juce::String("null"));
    const auto row = handle->getTitleRowArea().translated(handle->getX(), handle->getY());
    for (int y = row.getY(); y < row.getBottom(); ++y)
        for (int x = row.getX() + 2; x < row.getRight() - 2; x += 7)
            EXPECT_EQ(osc.under({x, y}), handle) << "title row pixel " << x << "," << y;
    EXPECT_EQ(osc.under(title - juce::Point<int>(handle->getWidth() / 2 - 8, 0)), handle) << "left end";
    EXPECT_EQ(osc.under(title + juce::Point<int>(handle->getWidth() / 2 - 8, 0)), handle) << "right end";
}

TEST(OnCardGroupHit, AControlCellBelowTheTitleStaysTheControlsOwn) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    for (const char* key : {"unison", "detune", "pulseWidth"}) {
        auto* outline = osc.editor->getOutlineForTest(key);
        if (outline == nullptr)
            continue;
        EXPECT_EQ(osc.under(osc.editor->getCellRectForTest(key).getCentre()), outline) << key;
    }
}

// Regression test for FRO704: a real press on the title moved nothing because a cell outline's padding covered
// the title row's lower edge and took the press; the group handle must win everywhere on the title row.
TEST(OnCardGroupHit, PressDragOnTheTitleThroughTheRealHierarchyMovesTheGroup) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    const auto blocksBefore = osc.editor->getGroupBlockForTest("output");
    const auto unison = osc.editor->getGroupBlockForTest("unison");
    const auto row = osc.editor->getGroupHandleForTest("unison")->getTitleRowArea().translated(
        osc.editor->getGroupHandleForTest("unison")->getX(), osc.editor->getGroupHandleForTest("unison")->getY());
    const int serial = osc.rig.canvas.undo.getEditSerial();
    // Press low on the title row, where the cells' padding reaches.
    const juce::Point<int> start(row.getCentreX(), row.getBottom() - 2);
    auto* target = osc.under(start);
    ASSERT_NE(target, nullptr);
    ASSERT_EQ(target, osc.editor->getGroupHandleForTest("unison"));
    target->mouseDown(osc.eventAt(*target, start));
    const int dy = blocksBefore.getBottom() - unison.getBottom();
    for (int i = 1; i <= 8; ++i) {
        const juce::Point<int> p(start.x, start.y + dy * i / 8);
        target->mouseDrag(osc.eventAt(*target, p));
    }
    target->mouseUp(osc.eventAt(*target, {start.x, start.y + dy}));
    const auto layout = osc.rig.storedLayout(osc.id);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(osc.rig.canvas.undo.getEditSerial(), serial + 1);
    EXPECT_EQ(layout->sections[3].id, juce::String("unison")) << "Unison moved below Output";
}

TEST(OnCardGroupHit, DoubleClickOnTheTitleFoundByHitTestingStartsTheRename) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    const auto p = osc.titleCentre("unison") + juce::Point<int>(0, 6);
    auto* target = osc.under(p);
    ASSERT_EQ(target, osc.editor->getGroupHandleForTest("unison"));
    target->mouseDoubleClick(osc.eventAt(*target, p, 2));
    EXPECT_NE(osc.editor->getRenameLabelForTest().getCurrentTextEditor(), nullptr);
}

TEST(OnCardGroupHit, ReturnOnTheFocusedHandleStartsTheRename) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    auto* handle = osc.editor->getGroupHandleForTest("unison");
    EXPECT_TRUE(handle->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_NE(osc.editor->getRenameLabelForTest().getCurrentTextEditor(), nullptr);
}

// Regression test for FRO704: a title drawn on the card must be backed by its own group's handle, at open and
// after a move, an undo and a redo.
TEST(OnCardGroupHit, EveryDrawnTitleIsBackedByItsOwnHandleAtOpenAndAfterMoveUndoRedo) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    expectDrawnTitlesReachTheirHandles(osc, "open");
    auto* unison = osc.editor->getGroupHandleForTest("unison");
    ASSERT_TRUE(unison->keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::altModifier, 0)));
    settle();
    expectDrawnTitlesReachTheirHandles(osc, "after Alt+Down");
    ASSERT_TRUE(osc.rig.canvas.undo.undo());
    osc.editor->runQueuedSyncForTest();
    settle();
    expectDrawnTitlesReachTheirHandles(osc, "after undo");
    ASSERT_TRUE(osc.rig.canvas.undo.redo());
    osc.editor->runQueuedSyncForTest();
    settle();
    expectDrawnTitlesReachTheirHandles(osc, "after redo");
}
