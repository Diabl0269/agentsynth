// OnCardEditorHideMotionTests.cpp -- "Drop to hide" motion: the dragged control hovers on the zone while
// the pointer is over it, shrinks into the zone's centre on release, and an undo brings it back rising out of the
// zone. Under Reduce Motion nothing moves and the control only fades where it is. Real mouse events on the outline,
// the real Cmd+Z path. Bug kinds: preview-vs-commit, motion-over-time.

#include "OnCardTestHelpers.h"

using namespace oncard_test;

namespace {

constexpr int kCommand = juce::ModifierKeys::commandModifier;

juce::Point<int> centreOf(juce::Rectangle<int> r) { return r.getCentre(); }

struct Hide : OnCardRig {
    explicit Hide(bool reduced) {
        id = add(std::make_unique<FilterModule>(), 200, 200);
        editor = openOnCard(id);
        synth::ui::setReducedMotionForTest(reduced);
        if (editor != nullptr)
            editor->setForceAnimateForTest(true);
    }

    juce::Point<int> zoneCentre() const { return centreOf(editor->getHideZoneForTest().getBounds()); }

    /** How far down the pointer must go from `key`'s outline centre to be `below` px under the card's edge. */
    int dropBelowCard(const juce::String& key, int below = 12) const {
        const int cardBottom = editor->getHeight() - CardLayoutOnCardEditor::kAddStripHeight;
        return cardBottom - editor->getOutlineForTest(key)->getBounds().getCentreY() + below;
    }

    NodeID id;
    CardLayoutOnCardEditor* editor = nullptr;
};

void expectNear(juce::Point<int> a, juce::Point<int> b, const char* what) {
    EXPECT_LE(std::abs(a.x - b.x), 1) << what << " x " << a.x << " vs " << b.x;
    EXPECT_LE(std::abs(a.y - b.y), 1) << what << " y " << a.y << " vs " << b.y;
}

constexpr float kPushMs = 160.0f;

struct Glide {
    explicit Glide(bool reduced = false) {
        id = addRowOscillator(rig);
        editor = rig.openOnCard(id);
        synth::ui::setReducedMotionForTest(reduced);
        if (editor != nullptr) {
            editor->setForceAnimateForTest(true);
            unison = editor->getCellRectForTest("unison");
            detune = editor->getCellRectForTest("detune");
            dx = detune.getCentreX() - unison.getCentreX() + 6; // past Detune's centre: the two swap places
        }
    }
    ~Glide() { synth::ui::setReducedMotionForTest(false); }

    juce::Rectangle<int> cell(const juce::String& key) const { return editor->getCellRectForTest(key); }
    int detuneX() const { return cell("detune").getX(); }

    OnCardRig rig;
    NodeID id;
    CardLayoutOnCardEditor* editor = nullptr;
    juce::Rectangle<int> unison, detune;
    int dx = 0;
};

} // namespace

TEST(OnCardEditorHideMotion, TheDragPreviewSitsOnTheZoneWhileThePointerIsOverIt) {
    Hide h(false);
    ASSERT_NE(h.editor, nullptr);
    const auto start = h.editor->getCellRectForTest("cutoff");

    Pointer pointer(*h.editor, "cutoff");
    pointer.moveBy({0, h.dropBelowCard("cutoff")}, kCommand);
    ASSERT_TRUE(h.editor->isHidingDragForTest());
    h.editor->stepGlideForTest(1.0e6f); // the snap onto the zone is a short glide: land it
    const auto onZone = h.editor->getCellRectForTest("cutoff");
    EXPECT_EQ(onZone.withZeroOrigin(), start.withZeroOrigin()) << "the control keeps its size";
    expectNear(centreOf(onZone), h.zoneCentre(), "preview centre vs zone centre");
    EXPECT_FALSE(h.storedLayout(h.id).has_value()) << "nothing is written until the release";
}

TEST(OnCardEditorHideMotion, TheSnapOntoTheZoneGlidesInsteadOfJumping) {
    Hide h(false);
    ASSERT_NE(h.editor, nullptr);
    Pointer pointer(*h.editor, "cutoff");
    pointer.moveBy({0, h.dropBelowCard("cutoff")}, kCommand);
    ASSERT_TRUE(h.editor->isGlidingForTest());
    h.editor->stepGlideForTest(0.0f);
    const int first = centreOf(h.editor->getCellRectForTest("cutoff")).y;
    const int towards = h.zoneCentre().y >= first ? 1 : -1;
    ASSERT_NE(first, h.zoneCentre().y) << "it does not start on the zone";
    int previous = first;
    for (float ms = 16.0f; ms <= 160.0f; ms += 16.0f) {
        h.editor->stepGlideForTest(ms);
        const int y = centreOf(h.editor->getCellRectForTest("cutoff")).y;
        EXPECT_GE((y - previous) * towards, 0) << "moves one way only, at " << ms << " ms";
        previous = y;
    }
    EXPECT_EQ(previous, h.zoneCentre().y);
}

TEST(OnCardEditorHideMotion, ReleasingShrinksTheControlIntoTheCentreOfTheZone) {
    Hide h(false);
    ASSERT_NE(h.editor, nullptr);
    Pointer pointer(*h.editor, "cutoff");
    pointer.moveBy({0, h.dropBelowCard("cutoff")}, kCommand);
    h.editor->stepGlideForTest(1.0e6f);
    pointer.release();

    ASSERT_EQ(h.editor->getShrinkGhostCountForTest(), 1);
    expectNear(centreOf(h.editor->getShrinkGhostBoundsForTest(0)), h.zoneCentre(), "shrinking picture centre");
    h.editor->setShrinkGhostProgressForTest(1.0f);
    expectNear(centreOf(h.editor->getShrinkGhostBoundsForTest(0)), h.zoneCentre(), "picture ends at the zone centre");
    EXPECT_TRUE(h.editor->hasPendingHideForTest()) << "the layout keeps the control until the picture has gone";
    h.editor->finishMotionForTest();
    ASSERT_TRUE(h.storedLayout(h.id).has_value());
    EXPECT_TRUE(h.storedLayout(h.id)->hidden.contains("cutoff"));
}

TEST(OnCardEditorHideMotion, UndoBringsTheControlBackRisingOutOfTheZone) {
    Hide h(false);
    ASSERT_NE(h.editor, nullptr);
    Pointer pointer(*h.editor, "cutoff");
    pointer.moveBy({0, h.dropBelowCard("cutoff")}, kCommand);
    h.editor->stepGlideForTest(1.0e6f);
    pointer.release();
    h.editor->finishMotionForTest();
    ASSERT_EQ(h.editor->getOutlineForTest("cutoff"), nullptr);

    ASSERT_TRUE(h.canvas.undo.undo());
    h.editor->runQueuedSyncForTest();
    auto* widget = widgetOf(*h.card(h.id), "cutoff");
    ASSERT_NE(widget, nullptr);
    ASSERT_NE(h.editor->getOutlineForTest("cutoff"), nullptr);
    h.editor->applyAddFrameForTest(0.0f);
    EXPECT_FLOAT_EQ(widget->getAlpha(), 0.0f);
    const auto drawnCentre = widget->getBounds().getCentre().toFloat().transformedBy(widget->getTransform());
    expectNear(drawnCentre.roundToInt(), h.zoneCentre(), "the enter animation starts at the zone");

    h.editor->applyAddFrameForTest(1.0f);
    const auto landed = widget->getBounds().getCentre().toFloat().transformedBy(widget->getTransform());
    expectNear(landed.roundToInt(), widget->getBounds().getCentre(), "and ends at the control's own place");
    h.editor->finishMotionForTest();
    EXPECT_TRUE(widget->getTransform().isIdentity());
}

TEST(OnCardEditorHideMotion, AControlHiddenAnotherWayStillComesBackFromItsOwnPlace) {
    Hide h(false);
    ASSERT_NE(h.editor, nullptr);
    hideThroughPanel(h, *h.editor, "drive");
    h.editor->finishMotionForTest();
    ASSERT_EQ(h.editor->getOutlineForTest("drive"), nullptr);
    ASSERT_TRUE(h.canvas.undo.undo());
    h.editor->runQueuedSyncForTest();
    auto* widget = widgetOf(*h.card(h.id), "drive");
    ASSERT_NE(widget, nullptr);
    h.editor->applyAddFrameForTest(0.0f);
    const auto start = widget->getBounds().getCentre().toFloat().transformedBy(widget->getTransform());
    expectNear(start.roundToInt(), widget->getBounds().getCentre(), "grows in place, not from the zone");
}

TEST(OnCardEditorHideMotion, UnderReduceMotionNothingMovesAndTheControlOnlyFades) {
    Hide h(true);
    ASSERT_NE(h.editor, nullptr);
    Pointer pointer(*h.editor, "cutoff");
    pointer.moveBy({0, h.dropBelowCard("cutoff")}, kCommand);
    ASSERT_TRUE(h.editor->isHidingDragForTest());
    const auto stays = h.editor->getCellRectForTest("cutoff");
    EXPECT_GT(centreOf(stays).y, 0);
    EXPECT_LT(centreOf(stays).y, h.editor->getHeight() - CardLayoutOnCardEditor::kAddStripHeight)
        << "the control stays inside the card: no movement onto the zone";
    EXPECT_FALSE(h.editor->isGlidingForTest());
    pointer.release();

    ASSERT_EQ(h.editor->getShrinkGhostCountForTest(), 1);
    expectNear(centreOf(h.editor->getShrinkGhostBoundsForTest(0)), centreOf(stays),
               "the fade is where the control was");
    h.editor->finishMotionForTest();
    ASSERT_TRUE(h.canvas.undo.undo());
    h.editor->runQueuedSyncForTest();
    auto* widget = widgetOf(*h.card(h.id), "cutoff");
    ASSERT_NE(widget, nullptr);
    h.editor->applyAddFrameForTest(0.0f);
    EXPECT_TRUE(widget->getTransform().isIdentity()) << "a plain fade: no movement from the zone either";
}

// ---- Neighbours of a dragged control glide aside (kind motion-over-time) ----
// While a control is dragged, the controls it pushes aside glide to their planned places (kPushMs, from where they
// are drawn), never jump; the release goes on from there; Esc and dragging away bring them home the same way.

TEST(OnCardEditorNeighbourGlide, ANeighbourPushedAsideIsStrictlyBetweenItsOldAndNewPlaceAtQuarterTime) {
    Glide g;
    ASSERT_NE(g.editor, nullptr);
    Pointer pointer(*g.editor, "unison");
    pointer.moveBy({g.dx, 0}, kCommand);
    ASSERT_TRUE(g.editor->isGlidingForTest());
    EXPECT_EQ(g.detuneX(), g.detune.getX()) << "the first frame is where it stood: no jump";

    g.editor->stepGlideForTest(0.25f * kPushMs);
    const int quarter = g.detuneX();
    g.editor->stepGlideForTest(1.0e6f);
    const int landed = g.detuneX();
    ASSERT_NE(landed, g.detune.getX()) << "Detune does make room";
    EXPECT_NE(quarter, g.detune.getX()) << "already moving at 25 %";
    EXPECT_LT(std::abs(quarter - g.detune.getX()), std::abs(landed - g.detune.getX())) << "and not there yet";
    EXPECT_GT((quarter - g.detune.getX()) * (landed - g.detune.getX()), 0) << "on the way to the new place";
    EXPECT_NE(g.cell("unison").getX(), g.unison.getX()) << "the dragged control itself is already out at the pointer";
    pointer.release();
}

TEST(OnCardEditorNeighbourGlide, DraggingAwayMidGlideTurnsTheNeighbourBackFromWhereItIsDrawn) {
    Glide g;
    ASSERT_NE(g.editor, nullptr);
    Pointer pointer(*g.editor, "unison");
    pointer.moveBy({g.dx, 0}, kCommand);
    g.editor->stepGlideForTest(0.5f * kPushMs);
    const int mid = g.detuneX();
    ASSERT_NE(mid, g.detune.getX());

    pointer.moveBy({-g.dx, 0}, kCommand); // back over its own place: Detune is to go home
    EXPECT_EQ(g.detuneX(), mid) << "the turn starts from the drawn place, no jump";
    g.editor->stepGlideForTest(1.0e6f);
    EXPECT_EQ(g.detuneX(), g.detune.getX());
    pointer.release();
}

TEST(OnCardEditorNeighbourGlide, TheReleaseGoesOnFromTheDrawnPlaceWithNoSecondJump) {
    Glide g;
    ASSERT_NE(g.editor, nullptr);
    Pointer pointer(*g.editor, "unison");
    pointer.moveBy({g.dx, 0}, kCommand);
    g.editor->stepGlideForTest(0.25f * kPushMs);
    const int mid = g.detuneX();
    ASSERT_NE(mid, g.detune.getX());
    pointer.release();
    EXPECT_EQ(g.detuneX(), mid) << "the release's glide starts where the preview had drawn it";
    g.editor->finishMotionForTest();
    EXPECT_NE(g.detuneX(), g.detune.getX());
    EXPECT_NE(g.detuneX(), mid);
}

TEST(OnCardEditorNeighbourGlide, DroppingBackWhereItBeganLetsTheNeighbourFinishGoingHome) {
    Glide g;
    ASSERT_NE(g.editor, nullptr);
    Pointer pointer(*g.editor, "unison");
    pointer.moveBy({g.dx, 0}, kCommand);
    g.editor->stepGlideForTest(0.5f * kPushMs);
    pointer.moveBy({-g.dx, 0}, kCommand);
    pointer.release();
    g.editor->finishMotionForTest();
    EXPECT_EQ(g.detuneX(), g.detune.getX()) << "not left half way";
    EXPECT_EQ(g.cell("unison"), g.unison);
}

TEST(OnCardEditorNeighbourGlide, EscapeSendsTheNeighbourHomeFromWhereItIsDrawn) {
    Glide g;
    ASSERT_NE(g.editor, nullptr);
    Pointer pointer(*g.editor, "unison");
    pointer.moveBy({g.dx, 0}, kCommand);
    g.editor->stepGlideForTest(0.5f * kPushMs);
    const int mid = g.detuneX();
    ASSERT_NE(mid, g.detune.getX());
    ASSERT_TRUE(g.editor->sendEscapeToDragForTest());
    EXPECT_EQ(g.detuneX(), mid) << "Esc does not snap it";
    g.editor->finishMotionForTest();
    EXPECT_EQ(g.detuneX(), g.detune.getX());
    EXPECT_EQ(g.cell("unison"), g.unison);
    pointer.release();
}

TEST(OnCardEditorNeighbourGlide, UnderReduceMotionTheNeighbourMovesAtOnce) {
    Glide g(true);
    ASSERT_NE(g.editor, nullptr);
    Pointer pointer(*g.editor, "unison");
    pointer.moveBy({g.dx, 0}, kCommand);
    EXPECT_FALSE(g.editor->isGlidingForTest());
    EXPECT_NE(g.detuneX(), g.detune.getX()) << "it is already out of the way";
    pointer.release();
}

TEST(OnCardEditorNeighbourGlide, DraggingOnToTheHideZoneSendsTheNeighbourHomeByGlidingNotSnapping) {
    Glide g;
    ASSERT_NE(g.editor, nullptr);
    Pointer pointer(*g.editor, "unison");
    pointer.moveBy({g.dx, 0}, kCommand);
    g.editor->stepGlideForTest(0.5f * kPushMs);
    const int mid = g.detuneX();
    ASSERT_NE(mid, g.detune.getX());
    const int cardBottom = g.editor->getHeight() - CardLayoutOnCardEditor::kAddStripHeight;
    pointer.moveBy({0, cardBottom - g.editor->getOutlineForTest("unison")->getBounds().getCentreY() + 12}, kCommand);
    ASSERT_TRUE(g.editor->isHidingDragForTest());
    EXPECT_EQ(g.detuneX(), mid) << "still where it was drawn";
    g.editor->stepGlideForTest(1.0e6f);
    EXPECT_EQ(g.detuneX(), g.detune.getX());
    EXPECT_TRUE(g.editor->sendEscapeToDragForTest());
    pointer.release();
}
