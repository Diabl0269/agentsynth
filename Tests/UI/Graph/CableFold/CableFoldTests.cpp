// A cable dragged over a card's More controls button opens the row; the row folds again when the cable leaves the
// card, is released elsewhere or is cancelled, unless it was open before the drag or the cable lands on one of its
// controls (docs/layout/module-card.md#the-more-row). Every case goes through GraphEditor's drag entry points and
// its Escape key, the same calls a mouse press, drag and release make.
//
// Kinds: preview-vs-commit (the card after a drag equals the card before it, unless the drop used the revealed
// part), motion-over-time (the refold animates).

#include "../../Layout/FadeVisibilityTestGuard.h"
#include "../CardBody/CardBodyTestHelpers.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"

using namespace cardbody_test;
using synth::ui::FadeVisibility;

namespace {

struct Rig {
    CardCanvas canvas;
    NodeID lfoId;
    NodeID oscId;
    ModuleComponent* lfo = nullptr;
    ModuleComponent* osc = nullptr;
    NodeID belowId; ///< With `withBelow`: a card just under the osc card, which an open row overlaps.
    ModuleComponent* below = nullptr;
    synth::CardBody* body = nullptr;

    explicit Rig(const juce::StringArray& hidden = {"level"}, bool withBelow = false) {
        OscillatorModule probe;
        lfoId = canvas.add(std::make_unique<LFOModule>(), 0, 0);
        oscId = canvas.add(std::make_unique<OscillatorModule>(), 600, 0, automaticLayoutHiding(probe, hidden));
        canvas.editor.updateComponents();
        lfo = canvas.card(lfoId);
        osc = canvas.card(oscId);
        body = osc->getCardBody();
        if (withBelow) {
            belowId = canvas.add(std::make_unique<LFOModule>(), 600, osc->getBottom() + 4);
            canvas.editor.updateComponents();
            lfo = canvas.card(lfoId);
            osc = canvas.card(oscId);
            below = canvas.card(belowId);
            body = osc->getCardBody();
        }
    }

    juce::Point<int> screen(juce::Point<int> cardLocal) { return osc->localPointToGlobal(cardLocal); }
    juce::Point<int> overMoreButton() { return screen(body->getMoreButton()->getBounds().getCentre()); }
    juce::Point<int> overHiddenKnob() { return screen(body->findWidget("level")->getBounds().getCentre()); }
    juce::Point<int> offTheCard() { return osc->localPointToGlobal(juce::Point<int>(-300, 40)); }
    juce::Point<int> overTitle() { return screen({40, 8}); }

    void begin() {
        canvas.editor.beginConnectionDrag(lfo, 0, false, false, lfo->localPointToGlobal(juce::Point<int>(0, 0)));
    }
    void moveTo(juce::Point<int> p) { canvas.editor.dragConnection(p); }
    void release(juce::Point<int> p) { canvas.editor.endConnectionDrag(p); }
    bool escape() { return canvas.editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)); }

    bool routed() {
        for (const auto& r : canvas.engine.getModulationRoutings())
            if (r.sourceNodeID == lfoId && r.destNodeID == oscId)
                return true;
        return false;
    }
    int height() { return osc->getHeight(); }
};

} // namespace

TEST(CableFold, HoverThenLeaveFoldsTheRowAfterAShortGrace) {
    Rig rig;
    const int folded = rig.height();
    rig.begin();
    rig.moveTo(rig.overMoreButton());
    ASSERT_TRUE(rig.body->isMoreUnfolded());
    EXPECT_TRUE(rig.body->isMoreOpenedByCableDrag());
    const int open = rig.height();
    ASSERT_GT(open, folded);

    rig.moveTo(rig.offTheCard());
    EXPECT_TRUE(rig.body->isMoreUnfolded())
        << "the grace keeps it open a moment, so crossing the edge does not flicker";
    EXPECT_TRUE(rig.body->cableFoldGraceRunningForTest());
    // Regression test for FRO1430: the row waited 250 ms after the cable left, which felt stuck.
    EXPECT_EQ(rig.body->cableFoldGraceMsForTest(), 120) << "long enough to cross the edge, short enough to feel prompt";

    rig.moveTo(rig.overTitle());
    EXPECT_FALSE(rig.body->cableFoldGraceRunningForTest()) << "coming back cancels the fold";
    rig.moveTo(rig.offTheCard());
    rig.body->endCableFoldGraceForTest();
    EXPECT_FALSE(rig.body->isMoreUnfolded());
    EXPECT_EQ(rig.height(), folded);
    rig.release(rig.offTheCard());
    EXPECT_FALSE(rig.routed());
}

TEST(CableFold, ARowOpenedByHandNeverFolds) {
    Rig rig;
    rig.body->setMoreUnfolded(true);
    const int open = rig.height();
    rig.begin();
    rig.moveTo(rig.overMoreButton());
    EXPECT_FALSE(rig.body->isMoreOpenedByCableDrag());
    rig.moveTo(rig.offTheCard());
    EXPECT_FALSE(rig.body->cableFoldGraceRunningForTest());
    rig.release(rig.offTheCard());
    EXPECT_TRUE(rig.body->isMoreUnfolded());
    EXPECT_EQ(rig.height(), open);
}

TEST(CableFold, ReleasingOnEmptyCanvasFoldsTheRow) {
    Rig rig;
    const int folded = rig.height();
    rig.begin();
    rig.moveTo(rig.overMoreButton());
    ASSERT_TRUE(rig.body->isMoreUnfolded());
    rig.release(rig.offTheCard());
    EXPECT_FALSE(rig.body->isMoreUnfolded());
    EXPECT_EQ(rig.height(), folded);
    EXPECT_FALSE(rig.body->cableFoldGraceRunningForTest());
}

TEST(CableFold, EscapeCancelsTheCableAndFoldsTheRow) {
    Rig rig;
    const int folded = rig.height();
    rig.begin();
    rig.moveTo(rig.overMoreButton());
    ASSERT_TRUE(rig.body->isMoreUnfolded());
    EXPECT_TRUE(rig.escape());
    EXPECT_FALSE(rig.body->isMoreUnfolded());
    EXPECT_EQ(rig.height(), folded);
    rig.release(rig.overMoreButton()); // the button comes up after Escape: nothing is left to drop
    EXPECT_FALSE(rig.routed());
    EXPECT_FALSE(rig.escape()) << "with no cable in hand Escape is the canvas's again";
}

TEST(CableFold, DroppingOnAHiddenKnobKeepsTheRowAndHandsItToThePerson) {
    Rig rig;
    rig.begin();
    rig.moveTo(rig.overMoreButton());
    ASSERT_TRUE(rig.body->isMoreUnfolded());
    const int open = rig.height();
    rig.moveTo(rig.overHiddenKnob());
    rig.release(rig.overHiddenKnob());
    EXPECT_TRUE(rig.routed()) << "the hidden Level knob took the cable";
    EXPECT_TRUE(rig.body->isMoreUnfolded());
    EXPECT_FALSE(rig.body->isMoreOpenedByCableDrag());
    EXPECT_EQ(rig.height(), open);

    // From here on it behaves as a row the person opened: a later drag passing over and away leaves it.
    rig.begin();
    rig.moveTo(rig.overTitle());
    rig.moveTo(rig.offTheCard());
    EXPECT_FALSE(rig.body->cableFoldGraceRunningForTest());
    rig.release(rig.offTheCard());
    EXPECT_TRUE(rig.body->isMoreUnfolded());
}

// Guard for the kind: whatever way a cable drag ends, the card is as it was before it began, unless the drop used
// the revealed part (a hidden knob), which is the one case where it is allowed to differ.
TEST(CableFold, ACardAfterACableDragEqualsTheCardBeforeUnlessTheDropUsedTheRevealedPart) {
    struct Case {
        const char* name;
        std::function<void(Rig&)> end;
        bool usesRevealedPart;
    };
    const std::vector<Case> cases = {
        {"release on empty canvas", [](Rig& r) { r.release(r.offTheCard()); }, false},
        {"Escape", [](Rig& r) { r.escape(); }, false},
        {"release on the More button", [](Rig& r) { r.release(r.overMoreButton()); }, false},
        {"release on the title", [](Rig& r) { r.release(r.overTitle()); }, false},
        {"leave, grace ends, release away",
         [](Rig& r) {
             r.moveTo(r.offTheCard());
             r.body->endCableFoldGraceForTest();
             r.release(r.offTheCard());
         },
         false},
        {"release on the hidden knob", [](Rig& r) { r.release(r.overHiddenKnob()); }, true},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.name);
        Rig rig;
        const bool foldedBefore = !rig.body->isMoreUnfolded();
        const int heightBefore = rig.height();
        const auto boundsBefore = rig.osc->getBounds();
        rig.begin();
        rig.moveTo(rig.overMoreButton());
        rig.moveTo(rig.overHiddenKnob());
        c.end(rig);
        EXPECT_EQ(!rig.body->isMoreUnfolded(), foldedBefore != c.usesRevealedPart);
        EXPECT_EQ(rig.height() == heightBefore, !c.usesRevealedPart);
        if (!c.usesRevealedPart)
            EXPECT_EQ(rig.osc->getBounds(), boundsBefore);
        EXPECT_EQ(rig.routed(), c.usesRevealedPart);
    }
}

TEST(CableFold, TheRefoldAnimatesFrameByFrame) {
    FadeAnimateGuard guard;
    Rig rig;
    const int folded = rig.height();
    rig.begin();
    rig.moveTo(rig.overMoreButton());
    FadeVisibility::stepAllForTest(1.0f);
    const int open = rig.height();
    ASSERT_GT(open, folded);
    auto* hidden = rig.body->findWidget("level");

    rig.moveTo(rig.offTheCard());
    rig.body->endCableFoldGraceForTest();
    EXPECT_TRUE(hidden->isVisible()) << "still there while it fades out";
    EXPECT_EQ(rig.height(), open) << "the first frame has not moved";

    std::vector<int> heights;
    std::vector<float> alphas;
    for (int frame = 0; frame < 3; ++frame) {
        FadeVisibility::stepAllForTest(0.2f * static_cast<float>(frame + 1)); // the fade is stepped to a point in it
        heights.push_back(rig.height());
        alphas.push_back(hidden->getAlpha());
    }
    for (size_t i = 0; i < heights.size(); ++i) {
        EXPECT_LT(heights[i], i == 0 ? open : heights[i - 1]) << "frame " << i;
        EXPECT_GT(heights[i], folded) << "frame " << i;
        EXPECT_LT(alphas[i], i == 0 ? 1.0f : alphas[i - 1]) << "frame " << i;
    }
    RecordProperty("folded", folded);
    RecordProperty("open", open);
    for (size_t i = 0; i < heights.size(); ++i)
        RecordProperty("frame" + std::to_string(i),
                       std::to_string(heights[i]) + "px alpha " + std::to_string(alphas[i]));
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(rig.height(), folded);
    EXPECT_FALSE(hidden->isVisible());
    rig.release(rig.offTheCard());
}

// The cable takes the keyboard so Escape reaches the canvas; every way the drag ends gives it back.
TEST(CableFold, TheKeyboardGoesBackToWhatHadItWhenTheDragEndsOrIsCancelled) {
    Rig rig;
    juce::TextButton control("focus me");
    control.setWantsKeyboardFocus(true);
    control.setBounds(0, 0, 40, 20);
    rig.canvas.editor.addAndMakeVisible(control);
    rig.canvas.editor.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    rig.canvas.editor.setVisible(true);
    control.grabKeyboardFocus();
    if (!control.hasKeyboardFocus(false)) {
        rig.canvas.editor.removeFromDesktop();
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";
    }

    rig.begin();
    EXPECT_FALSE(control.hasKeyboardFocus(false)) << "the canvas holds the keyboard during the drag";
    rig.release(rig.offTheCard());
    EXPECT_TRUE(control.hasKeyboardFocus(false)) << "a release gives it back";

    rig.begin();
    EXPECT_TRUE(rig.escape());
    EXPECT_TRUE(control.hasKeyboardFocus(false)) << "so does Escape";
    rig.canvas.editor.removeFromDesktop();
}

// Regression test for FRO1188: a row that pushed the card the cable came from left the cable stuck to the screen,
// because the press point, read against the card's new spot, no longer hit the jack.
TEST(CableFold, ACableKeepsFollowingAndEndsWhenItsOwnCardGetsPushed) {
    Rig rig;
    const auto jack = rig.lfo->getPortCenter(0, false);
    const auto left = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    rig.lfo->mouseDown(cardbody_test::mouseAt(*rig.lfo, jack.toFloat(), left));
    ASSERT_TRUE(rig.canvas.editor.isCableDragFrom(*rig.lfo));

    rig.lfo->setTopLeftPosition(rig.lfo->getPosition() + juce::Point<int>(0, 200));
    const auto pressAfterThePush = (jack - juce::Point<int>(0, 200)).toFloat();
    const auto heldAt = [&](juce::Point<int> screen) {
        const auto now = juce::Time::getCurrentTime();
        const auto local = rig.lfo->getLocalPoint(nullptr, screen).toFloat();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), local, left, 0.0f, 0.0f, 0.0f, 0.0f,
                                0.0f, rig.lfo, rig.lfo, now, pressAfterThePush, now, 1, false);
    };

    rig.lfo->mouseDrag(heldAt(rig.overMoreButton()));
    EXPECT_TRUE(rig.body->isMoreUnfolded()) << "the cable still follows the mouse";

    rig.lfo->mouseUp(heldAt(rig.offTheCard()));
    EXPECT_FALSE(rig.canvas.editor.isCableDragFrom(*rig.lfo)) << "and the release ends it";
    EXPECT_FALSE(rig.body->isMoreUnfolded());
}

// Regression test for FRO1188: opening the row takes the hidden controls' jacks out of the gutter before the row has
// grown, so the card was briefly shorter than a cable resting on the button, read that as leaving, and folded again.
TEST(CableFold, ARowOpeningUnderAStillCableStaysOpenThoughTheCardShrinksFirst) {
    FadeAnimateGuard guard;
    Rig rig({"fine", "level", "blend", "width"});
    const auto onTheButton = rig.overMoreButton();
    rig.begin();
    rig.moveTo(onTheButton);
    ASSERT_TRUE(rig.body->isMoreUnfolded());
    EXPECT_FALSE(rig.body->cableFoldGraceRunningForTest());

    FadeVisibility::stepAllForTest(1.0f);
    rig.moveTo(onTheButton);
    EXPECT_FALSE(rig.body->cableFoldGraceRunningForTest());
    EXPECT_TRUE(rig.body->isMoreUnfolded());
    rig.release(rig.offTheCard());
    EXPECT_FALSE(rig.body->isMoreUnfolded());
}

// Regression test for FRO1501: a row a cable opened grew the card and pushed the card below it down, so a drop aimed
// at that card's jack missed. The row peeks over its neighbours instead; nothing moves while the cable is live.
TEST(CableFold, ACableOpeningARowMovesNoOtherCard) {
    FadeAnimateGuard guard;
    Rig rig({"level"}, /*withBelow=*/true);
    const auto belowBefore = rig.below->getBounds();
    const auto lfoBefore = rig.lfo->getBounds();
    const int folded = rig.height();
    rig.begin();
    rig.moveTo(rig.overMoreButton());
    ASSERT_TRUE(rig.body->isMoreUnfolded());
    FadeVisibility::stepAllForTest(1.0f);
    ASSERT_GT(rig.height(), folded);
    ASSERT_TRUE(rig.osc->getBounds().intersects(belowBefore)) << "the open row overlaps the card below";
    EXPECT_EQ(rig.below->getBounds(), belowBefore);
    EXPECT_EQ(rig.lfo->getBounds(), lfoBefore);

    rig.moveTo(rig.offTheCard());
    rig.body->endCableFoldGraceForTest();
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(rig.body->isMoreUnfolded());
    EXPECT_EQ(rig.height(), folded);
    EXPECT_EQ(rig.below->getBounds(), belowBefore);
    EXPECT_EQ(rig.lfo->getBounds(), lfoBefore);
    rig.release(rig.offTheCard());
    EXPECT_EQ(rig.below->getBounds(), belowBefore);
}

TEST(CableFold, ARowAPeekOpenedIsInFrontOfTheCardItOverlaps) {
    Rig rig({"level"}, /*withBelow=*/true);
    auto& content = *rig.osc->getParentComponent();
    ASSERT_EQ(rig.below->getParentComponent(), &content);
    rig.below->toFront(false); // the card below starts in front, as any later-added or later-clicked card can be
    ASSERT_GT(content.getIndexOfChildComponent(rig.below), content.getIndexOfChildComponent(rig.osc));
    rig.begin();
    rig.moveTo(rig.overMoreButton());
    EXPECT_GT(content.getIndexOfChildComponent(rig.osc), content.getIndexOfChildComponent(rig.below));
    rig.release(rig.offTheCard());
}

TEST(CableFold, ADropOnARevealedControlMakesRoomThen) {
    FadeAnimateGuard guard;
    Rig rig({"level"}, /*withBelow=*/true);
    const auto belowBefore = rig.below->getBounds();
    rig.begin();
    rig.moveTo(rig.overMoreButton());
    FadeVisibility::stepAllForTest(1.0f);
    rig.moveTo(rig.overHiddenKnob());
    EXPECT_EQ(rig.below->getBounds(), belowBefore) << "nothing moves until the cable lands";
    rig.release(rig.overHiddenKnob());
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_TRUE(rig.routed());
    EXPECT_TRUE(rig.body->isMoreUnfolded());
    EXPECT_FALSE(rig.osc->getBounds().intersects(rig.below->getBounds())) << "the card below made room";
    EXPECT_GT(rig.below->getY(), belowBefore.getY());
}

// Regression test for FRO1697: the room a kept-open row makes belongs to the cable's own undo step.
TEST(CableFold, OneUndoOfADropOnARevealedControlRemovesTheCableFoldsTheRowAndReturnsTheCardBelow) {
    FadeAnimateGuard guard;
    Rig rig({"level"}, /*withBelow=*/true);
    const auto belowBefore = rig.below->getBounds();
    const auto oscBefore = rig.osc->getBounds();
    rig.begin();
    rig.moveTo(rig.overMoreButton());
    FadeVisibility::stepAllForTest(1.0f);
    rig.release(rig.overHiddenKnob());
    FadeVisibility::stepAllForTest(1.0f);
    ASSERT_TRUE(rig.routed());
    ASSERT_GT(rig.below->getY(), belowBefore.getY());
    const auto belowAfter = rig.below->getBounds();

    ASSERT_TRUE(rig.canvas.undo.undo());
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(rig.canvas.undo.canUndo()) << "the cable and the room are one step";
    auto* osc = rig.canvas.card(rig.oscId);
    auto* below = rig.canvas.card(rig.belowId);
    ASSERT_NE(osc, nullptr);
    ASSERT_NE(below, nullptr);
    EXPECT_FALSE(rig.canvas.engine.getModulationRoutings().size() > 0 && rig.routed()) << "the cable is gone";
    EXPECT_FALSE(osc->getCardBody()->isMoreUnfolded()) << "the row folds";
    EXPECT_EQ(osc->getBounds(), oscBefore);
    EXPECT_EQ(below->getBounds(), belowBefore) << "the pushed card is back";

    ASSERT_TRUE(rig.canvas.undo.redo());
    FadeVisibility::stepAllForTest(1.0f);
    osc = rig.canvas.card(rig.oscId);
    below = rig.canvas.card(rig.belowId);
    EXPECT_TRUE(rig.routed());
    EXPECT_TRUE(osc->getCardBody()->isMoreUnfolded()) << "redo opens the row again";
    EXPECT_EQ(below->getBounds(), belowAfter) << "redo pushes the card again";
}
