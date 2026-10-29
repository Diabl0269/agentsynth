// OnCardEditorUndoGlideTests.cpp -- Cmd+Z and Cmd+Shift+Z after moving a control in Edit Layout: the
// control glides back in ONE glide from where it was drawn. The card the restore rebuilds is re-synced inside the
// restore itself, so no frame shows the restored layout before the glide starts, and a second undo or redo
// mid-glide starts from where the control is drawn. Bug kinds: motion-over-time, preview-vs-commit.

#include "Modules/OscillatorModule.h"
#include "OnCardTestHelpers.h"

using namespace oncard_test;

namespace {

constexpr int kCommand = juce::ModifierKeys::commandModifier;

void drop(CardLayoutOnCardEditor& editor, const juce::String& key, juce::Point<int> by) {
    Pointer pointer(editor, key);
    pointer.moveBy(by, kCommand);
    pointer.release();
}

// What the app does on Cmd+Z / Cmd+Shift+Z: the editor sees the key and leaves it, then the app undoes or redoes.
// Nothing here runs the message loop, so only what the restore itself did is on screen afterwards.
bool pressUndo(OnCardRig& rig, CardLayoutOnCardEditor& editor, bool redo, const char* key = "cutoff") {
    const int mods = kCommand | (redo ? juce::ModifierKeys::shiftModifier : 0);
    EXPECT_FALSE(editor.getOutlineForTest(key)->keyPressed(juce::KeyPress('z', juce::ModifierKeys(mods), 0)));
    return redo ? rig.canvas.undo.redo() : rig.canvas.undo.undo();
}

struct Glide : OnCardRig {
    Glide() {
        synth::ui::setReducedMotionForTest(false);
        id = add(std::make_unique<FilterModule>());
        editor = openOnCard(id);
    }
    int drawnX() { return widgetOf(*card(id), "cutoff")->getX(); }
    juce::Rectangle<int> drawn() { return widgetOf(*card(id), "cutoff")->getBounds(); }

    NodeID id;
    CardLayoutOnCardEditor* editor = nullptr;
};

} // namespace

TEST(OnCardEditorUndoGlide, NoFrameShowsTheRestoredLayoutBeforeTheGlideStartsFromWhereTheControlWas) {
    Glide g;
    ASSERT_NE(g.editor, nullptr);
    const int restoredX = g.drawnX();
    drop(*g.editor, "cutoff", {40, 0});
    const int movedX = g.drawnX();
    ASSERT_NE(movedX, restoredX);
    g.editor->setForceAnimateForTest(true);

    ASSERT_TRUE(pressUndo(g, *g.editor, false));
    EXPECT_EQ(g.drawnX(), movedX) << "the first frame after Cmd+Z still shows the control where it was";
    EXPECT_TRUE(g.editor->isGlidingForTest());

    int previous = movedX;
    for (float ms = 0.0f; ms <= 160.0f; ms += 16.0f) {
        g.editor->stepGlideForTest(ms);
        const int x = g.drawnX();
        EXPECT_LE(x, previous) << "moves one way only, at " << ms << " ms";
        EXPECT_GE(x, restoredX);
        EXPECT_EQ(g.editor->getCellRectForTest("cutoff").getX(), x) << "the outline goes with the control";
        previous = x;
    }
    g.editor->stepGlideForTest(1000.0f);
    EXPECT_EQ(g.drawnX(), restoredX);
}

TEST(OnCardEditorUndoGlide, RedoGlidesForwardTheSameWayWithoutAFlash) {
    Glide g;
    ASSERT_NE(g.editor, nullptr);
    const int restoredX = g.drawnX();
    drop(*g.editor, "cutoff", {40, 0});
    const int movedX = g.drawnX();
    g.editor->setForceAnimateForTest(true);
    ASSERT_TRUE(pressUndo(g, *g.editor, false));
    g.editor->finishMotionForTest();
    ASSERT_EQ(g.drawnX(), restoredX);

    ASSERT_TRUE(pressUndo(g, *g.editor, true));
    EXPECT_EQ(g.drawnX(), restoredX) << "redo starts from the restored place";
    int previous = restoredX;
    for (float ms = 0.0f; ms <= 160.0f; ms += 16.0f) {
        g.editor->stepGlideForTest(ms);
        EXPECT_GE(g.drawnX(), previous) << ms;
        EXPECT_LE(g.drawnX(), movedX);
        previous = g.drawnX();
    }
    g.editor->stepGlideForTest(1000.0f);
    EXPECT_EQ(g.drawnX(), movedX);
}

TEST(OnCardEditorUndoGlide, ARepeatedUndoOrRedoMidGlideContinuesFromWhereTheControlIsDrawn) {
    Glide g;
    ASSERT_NE(g.editor, nullptr);
    const int restoredX = g.drawnX();
    drop(*g.editor, "cutoff", {40, 0});
    const int movedX = g.drawnX();
    g.editor->setForceAnimateForTest(true);

    ASSERT_TRUE(pressUndo(g, *g.editor, false));
    g.editor->stepGlideForTest(60.0f);
    const int midway = g.drawnX();
    ASSERT_GT(midway, restoredX);
    ASSERT_LT(midway, movedX);

    ASSERT_TRUE(pressUndo(g, *g.editor, true)); // Cmd+Shift+Z while the control is still on its way back
    EXPECT_EQ(g.drawnX(), midway) << "no jump: the redo starts where the control is drawn";
    EXPECT_EQ(g.editor->getCellRectForTest("cutoff").getX(), midway);
    int previous = midway;
    for (float ms = 0.0f; ms <= 160.0f; ms += 16.0f) {
        g.editor->stepGlideForTest(ms);
        EXPECT_GE(g.drawnX(), previous) << "one glide towards the moved place, at " << ms << " ms";
        previous = g.drawnX();
    }
    g.editor->stepGlideForTest(1000.0f);
    EXPECT_EQ(g.drawnX(), movedX);

    ASSERT_TRUE(pressUndo(g, *g.editor, false));
    g.editor->stepGlideForTest(60.0f);
    ASSERT_TRUE(pressUndo(g, *g.editor, true));
    ASSERT_TRUE(pressUndo(g, *g.editor, false)); // three in a row, each one restore
    g.editor->finishMotionForTest();
    EXPECT_EQ(g.drawnX(), restoredX);
    EXPECT_FALSE(g.editor->isClosed());
}

TEST(OnCardEditorUndoGlide, ARepeatTriggerForTheSameChangeWhileItGlidesDoesNotRestartIt) {
    Glide g;
    ASSERT_NE(g.editor, nullptr);
    const int restoredX = g.drawnX();
    drop(*g.editor, "cutoff", {40, 0});
    g.editor->setForceAnimateForTest(true);
    ASSERT_TRUE(pressUndo(g, *g.editor, false));
    g.editor->stepGlideForTest(80.0f);
    const int midway = g.drawnX();
    ASSERT_GT(midway, restoredX);

    // The card reports its conditions applied (the editor's own hook), queueing another re-sync of the same restore.
    ASSERT_TRUE(static_cast<bool>(g.card(g.id)->getCardBody()->onConditionsApplied));
    g.card(g.id)->getCardBody()->onConditionsApplied();
    g.editor->runQueuedSyncForTest();
    EXPECT_EQ(g.drawnX(), midway) << "the glide in flight is left alone";
    EXPECT_TRUE(g.editor->isGlidingForTest());
    g.editor->stepGlideForTest(1000.0f);
    EXPECT_EQ(g.drawnX(), restoredX);
}

namespace {

// An Oscillator card with Glide dropped on a row of its own below the last one: the card grows by a row.
struct GrowingMove : OnCardRig {
    GrowingMove() {
        synth::ui::setReducedMotionForTest(false);
        id = add(std::make_unique<OscillatorModule>(), 100, 100);
        editor = openOnCard(id);
        canvas.editor.getCardGlideForTest().setForceAnimateForTest(
            true); // not on screen: a card slide arms only forced
    }
    juce::Rectangle<int> glideWidget() {
        auto* widget = widgetOf(*card(id), "glide");
        return widget != nullptr ? widget->getBounds() : juce::Rectangle<int>();
    }
    int cardHeight() { return card(id)->getHeight(); }

    // The editor is on the card, sized to it with the strip under it, showing every outline, nothing hiding it.
    void expectAttached(const char* when) {
        SCOPED_TRACE(when);
        ASSERT_NE(editor, nullptr);
        auto* c = card(id);
        ASSERT_NE(c, nullptr);
        EXPECT_TRUE(editor->isVisible());
        EXPECT_EQ(editor->getAlpha(), 1.0f);
        EXPECT_EQ(c->getAlpha(), 1.0f) << "the card is drawn as itself, not hidden behind a sliding snapshot of it";
        EXPECT_EQ(canvas.editor.getCardGlideForTest().offsetFor(id.uid), juce::Point<float>())
            << "the canvas does not slide the card under its editor";
        EXPECT_EQ(editor->getParentComponent(), c->getParentComponent());
        EXPECT_EQ(editor->getBounds(),
                  c->getBounds().withHeight(c->getHeight() + CardLayoutOnCardEditor::kAddStripHeight));
        EXPECT_NE(editor->getOutlineForTest("level"), nullptr) << "the outlines are there";
        auto* parent = c->getParentComponent();
        EXPECT_GT(parent->getIndexOfChildComponent(editor), parent->getIndexOfChildComponent(c))
            << "the overlay sits above the card";
    }

    NodeID id;
    CardLayoutOnCardEditor* editor = nullptr;
};

} // namespace

// Regression test for FRO721: an undo that shrank the card showed the bare card for two frames, the control already
// home.
TEST(OnCardEditorUndoGlide, AnUndoThatShrinksTheCardKeepsTheEditorOnItAndGlidesFromTheDropSpot) {
    GrowingMove g;
    ASSERT_NE(g.editor, nullptr);
    ASSERT_NE(g.editor->getOutlineForTest("glide"), nullptr);
    ASSERT_NE(g.editor->getOutlineForTest("level"), nullptr);
    ASSERT_FALSE(g.glideWidget().isEmpty());
    const auto home = g.editor->getCellRectForTest("glide");
    const auto homeWidget = g.glideWidget();
    const int homeHeight = g.cardHeight();
    const auto lowest = g.editor->getCellRectForTest("level");
    {
        Pointer pointer(*g.editor, "glide");
        pointer.moveBy({0, lowest.getBottom() + 14 - home.getCentreY()}, kCommand);
        pointer.release();
    }
    const auto moved = g.editor->getCellRectForTest("glide");
    g.editor->finishMotionForTest();
    ASSERT_GT(g.cardHeight(), homeHeight) << "the drop added a row";
    ASSERT_NE(moved, home);
    const auto drawnMoved = g.glideWidget();
    g.editor->setForceAnimateForTest(true);

    ASSERT_TRUE(pressUndo(g, *g.editor, false, "glide"));
    ASSERT_EQ(g.cardHeight(), homeHeight);
    g.expectAttached("right after Cmd+Z returns");
    EXPECT_TRUE(g.editor->isGlidingForTest());
    EXPECT_EQ(g.editor->getCellRectForTest("glide").getPosition(), moved.getPosition())
        << "the outline starts at the drop";
    EXPECT_NE(g.glideWidget(), homeWidget) << "not already at home";
    EXPECT_EQ(g.glideWidget().getPosition(), drawnMoved.getPosition()) << "starts from the dropped spot";

    g.editor->stepGlideForTest(80.0f);
    g.expectAttached("mid glide");
    g.editor->stepGlideForTest(1000.0f);
    EXPECT_EQ(g.editor->getCellRectForTest("glide"), home);
    g.expectAttached("landed");

    // Redo grows the card again.
    ASSERT_TRUE(pressUndo(g, *g.editor, true, "glide"));
    ASSERT_GT(g.cardHeight(), homeHeight);
    g.expectAttached("right after Cmd+Shift+Z returns");
    EXPECT_TRUE(g.editor->isGlidingForTest());
    EXPECT_EQ(g.editor->getCellRectForTest("glide").getPosition(), home.getPosition());
    EXPECT_EQ(g.glideWidget().getPosition(), homeWidget.getPosition()) << "redo starts from the restored place";
    g.editor->stepGlideForTest(1000.0f);
    EXPECT_EQ(g.editor->getCellRectForTest("glide"), moved);
    g.expectAttached("redo landed");
}

TEST(OnCardEditorUndoGlide, AnUndoOfADropToHideThatGrowsTheCardBackKeepsTheEditorOnIt) {
    GrowingMove g;
    ASSERT_NE(g.editor, nullptr);
    const int fullHeight = g.cardHeight();
    // Hiding the only control of a row closes the row up: the card shrinks.
    hideThroughPanel(g, *g.editor, "glide");
    g.editor->finishMotionForTest();
    const int hiddenHeight = g.cardHeight();
    g.editor->setForceAnimateForTest(true);

    ASSERT_TRUE(pressUndo(g, *g.editor, false, "level"));
    g.expectAttached("right after undoing the hide");
    EXPECT_EQ(g.cardHeight(), fullHeight);
    ASSERT_TRUE(pressUndo(g, *g.editor, true, "level"));
    g.expectAttached("right after redoing the hide");
    EXPECT_EQ(g.cardHeight(), hiddenHeight);
    g.editor->finishMotionForTest();
    g.expectAttached("landed");
}
