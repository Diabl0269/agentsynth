// OnCardEditorFollowTests.cpp -- Edit Layout when something other than the editor moves the card being edited:
// the session goes on and the canvas pans, eased, only as far as needed to keep the card on screen; and
// starting an edit on a second module ends the first one's, its changes kept. Real undo path, real menu click.
// Bug kinds: undo-gap, motion-over-time.

#include "OnCardTestHelpers.h"
#include "Project/ViewDoc.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardEditorOwner.h"

using namespace oncard_test;

namespace {

constexpr int kCommand = juce::ModifierKeys::commandModifier;

void drop(CardLayoutOnCardEditor& editor, const juce::String& key, juce::Point<int> by) {
    Pointer pointer(editor, key);
    pointer.moveBy(by, kCommand);
    pointer.release();
}

// The module moved on the canvas, as one undo step (an auto-arrange or a drag records the same).
void recordModuleMove(OnCardRig& rig, NodeID id, int x, int y) {
    auto& graph = rig.canvas.engine.getGraph();
    rig.canvas.undo.recordStructuralChange(graph, [&] {
        auto* node = graph.getNodeForId(id);
        node->properties.set("x", x);
        node->properties.set("y", y);
    });
    rig.canvas.editor.updateComponents();
}

// What the app does on Cmd+Z with the editor open: the editor sees the key and leaves it, the app undoes.
bool pressUndo(OnCardRig& rig, CardLayoutOnCardEditor& editor, const juce::String& focused) {
    EXPECT_FALSE(editor.getOutlineForTest(focused)->keyPressed(juce::KeyPress('z', juce::ModifierKeys(kCommand), 0)));
    return rig.canvas.undo.undo();
}

bool shows(OnCardRig& rig, NodeID id) {
    return rig.canvas.editor.getVisibleCanvasRect().contains(rig.card(id)->getBounds().toFloat());
}

float panX(OnCardRig& rig) { return rig.canvas.editor.getViewDoc().panX; }

/** The card at (100, 100) is moved to (3000, 100), off to the right of a 2400 px wide view; the view is centred on it
 *  there and the editor opened; an undo then puts the card back at (100, 100), off to the left of the view. */
struct FarAway : OnCardRig {
    FarAway() {
        id = add(std::make_unique<FilterModule>(), 100, 100);
        recordModuleMove(*this, id, 3000, 100);
        canvas.editor.centreViewOn(card(id)->getBounds().toFloat().getCentre());
        editor = openOnCard(id);
    }

    NodeID id;
    CardLayoutOnCardEditor* editor = nullptr;
};

} // namespace

TEST(OnCardEditorFollow, AnUndoThatMovesTheEditedCardKeepsTheEditorOpenAndPansTheViewToTheCard) {
    FarAway far;
    ASSERT_NE(far.editor, nullptr);
    ASSERT_TRUE(shows(far, far.id));
    const int outlines = far.editor->getOutlineCountForTest();
    const auto zoom = far.canvas.editor.getViewDoc().zoom;

    ASSERT_TRUE(pressUndo(far, *far.editor, "cutoff"));
    far.editor->runQueuedSyncForTest();
    EXPECT_EQ(far.card(far.id)->getX(), 100) << "the undo did move the card";
    EXPECT_FALSE(far.editor->isClosed()) << "editing goes on";
    EXPECT_EQ(far.editor->getOutlineCountForTest(), outlines);
    EXPECT_TRUE(shows(far, far.id)) << "and the view follows the card";
    EXPECT_FLOAT_EQ(far.canvas.editor.getViewDoc().zoom, zoom) << "only a pan, never a zoom";
    EXPECT_EQ(far.editor->getBounds().getPosition(), far.card(far.id)->getBounds().getPosition())
        << "the overlay is still over the card";
}

TEST(OnCardEditorFollow, TheViewPansOnlyWhenTheCardWouldLeaveTheVisibleArea) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>(), 100, 100);
    recordModuleMove(rig, id, 600, 300);
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto before = rig.canvas.editor.getViewDoc();

    ASSERT_TRUE(pressUndo(rig, *editor, "cutoff"));
    editor->runQueuedSyncForTest();
    EXPECT_EQ(rig.card(id)->getX(), 100);
    EXPECT_FLOAT_EQ(panX(rig), before.panX) << "the card stayed on screen: the view does not move";
    EXPECT_FLOAT_EQ(rig.canvas.editor.getViewDoc().panY, before.panY);
    EXPECT_FALSE(editor->isFollowingForTest());
}

TEST(OnCardEditorFollow, ACardHalfOffTheEdgeIsPannedInByTheLeastThatShowsItWhole) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>(), 100, 100);
    recordModuleMove(rig, id, 2300, 100); // runs past the right edge of the 2400 px view
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    ASSERT_FALSE(shows(rig, id));
    // Move the card back in with an undo, then forward again with a redo: the redo puts it past the edge.
    ASSERT_TRUE(pressUndo(rig, *editor, "cutoff"));
    editor->runQueuedSyncForTest();
    ASSERT_TRUE(shows(rig, id));
    const float panBefore = panX(rig);

    ASSERT_TRUE(rig.canvas.undo.redo());
    editor->runQueuedSyncForTest();
    EXPECT_EQ(rig.card(id)->getX(), 2300);
    EXPECT_TRUE(shows(rig, id));
    const auto visible = rig.canvas.editor.getVisibleCanvasRect();
    const auto card = rig.card(id)->getBounds().toFloat();
    EXPECT_LT(panX(rig), panBefore) << "the canvas moved left";
    EXPECT_NEAR(visible.getRight() - card.getRight(), 24.0f, 1.0f) << "a small margin, no more than needed";
}

TEST(OnCardEditorFollow, ThePanEasesWhenOnScreenAndLandsAtOnceUnderReduceMotion) {
    {
        FarAway far;
        ASSERT_NE(far.editor, nullptr);
        synth::ui::setReducedMotionForTest(false);
        far.editor->setForceAnimateForTest(true);
        const float start = panX(far);
        ASSERT_TRUE(pressUndo(far, *far.editor, "cutoff"));
        far.editor->runQueuedSyncForTest();
        ASSERT_TRUE(far.editor->isFollowingForTest());
        EXPECT_FLOAT_EQ(panX(far), start) << "the first frame has not moved yet";
        far.editor->stepFollowForTest(0.0f);
        float previous = panX(far);
        for (float ms = 16.0f; ms <= 400.0f; ms += 16.0f) {
            far.editor->stepFollowForTest(ms);
            EXPECT_GE(panX(far), previous) << "pans one way only (the card is to the left), at " << ms << " ms";
            previous = panX(far);
        }
        far.editor->stepFollowForTest(1.0e6f);
        EXPECT_TRUE(shows(far, far.id));
        EXPECT_GT(panX(far), start);
    }
    FarAway reduced;
    ASSERT_NE(reduced.editor, nullptr);
    synth::ui::setReducedMotionForTest(true);
    reduced.editor->setForceAnimateForTest(true);
    ASSERT_TRUE(pressUndo(reduced, *reduced.editor, "cutoff"));
    reduced.editor->runQueuedSyncForTest();
    EXPECT_TRUE(shows(reduced, reduced.id)) << "no tween under Reduce Motion";
    EXPECT_FALSE(reduced.editor->isFollowingForTest());
}

TEST(OnCardEditorFollow, StartingAnEditOnASecondModuleEndsTheFirstAndKeepsItsChanges) {
    OnCardRig rig;
    ModuleComponent::setCardLayoutEditorLauncherForTest(nullptr); // the real owner, as the app has (the rig's
                                                                  // destructor clears the seam either way)
    const auto first = rig.add(std::make_unique<FilterModule>(), 100, 100);
    const auto second = rig.add(std::make_unique<FilterModule>(), 700, 100);
    const auto editorOver = [&rig](NodeID id) -> CardLayoutOnCardEditor* {
        for (auto* comp : rig.canvas.editor.getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == id)
                if (auto* parent = comp->getParentComponent())
                    for (auto* sibling : parent->getChildren())
                        if (auto* e = dynamic_cast<CardLayoutOnCardEditor*>(sibling))
                            if (!e->isClosed() && e->getBounds().getPosition() == comp->getPosition())
                                return e;
        return nullptr;
    };

    rig.card(first)->showCardLayoutEditor();
    auto* a = editorOver(first);
    ASSERT_NE(a, nullptr);
    drop(*a, "cutoff", {40, 0});
    ASSERT_TRUE(rig.storedLayout(first).has_value());
    const auto kept = rig.storedLayout(first);

    rig.card(second)->showCardLayoutEditor();
    EXPECT_TRUE(a->isClosed()) << "the first module's edit ended, no dialog";
    EXPECT_EQ(rig.storedLayout(first), kept) << "its change stays";
    auto* b = editorOver(second);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(editorOver(first), nullptr);

    ASSERT_TRUE(rig.canvas.undo.undo());
    EXPECT_FALSE(rig.storedLayout(first).has_value()) << "one Cmd+Z takes the first module's move back";
    synth::ui::closeOnCardLayoutEditor(rig.canvas.editor); // ends the session while the canvas is alive
}
