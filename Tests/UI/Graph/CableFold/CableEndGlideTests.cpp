// A modulation cable's end glides, not jumps, when its card's More row folds or unfolds: it moves between the knob
// it lands on and the More button over the row's fade (docs/layout/cables.md, docs/layout/animation.md).
//
// Kinds: motion-over-time (a More toggle with a cable attached is a glide, not an instant jump).

#include "../../Layout/FadeVisibilityTestGuard.h"
#include "../CardBody/CardBodyTestHelpers.h"
#include "../CardBody/CardCanvas.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "UI/Graph/GraphEditor/CableEndGlide.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"

using namespace cardbody_test;
using synth::ui::AnimationMode;

namespace {

struct GlideRig {
    CardCanvas canvas;
    NodeID lfoId, oscId;
    ModuleComponent* lfo = nullptr;
    ModuleComponent* osc = nullptr;
    synth::CardBody* body = nullptr;

    // A Pan cable on a Filter whose Pan knob sits in the More row, with the row open.
    GlideRig() {
        FilterModule probe;
        lfoId = canvas.add(std::make_unique<LFOModule>(), 0, 0);
        oscId = canvas.add(std::make_unique<FilterModule>(), 600, 0, automaticLayoutHiding(probe, {"pan"}));
        canvas.editor.updateComponents();
        lfo = canvas.card(lfoId);
        osc = canvas.card(oscId);
        body = osc->getCardBody();
        body->setMoreUnfolded(true);
        const auto knob = osc->localPointToGlobal(body->findWidget("pan")->getBounds().getCentre());
        canvas.editor.beginConnectionDrag(lfo, 0, false, false, lfo->localPointToGlobal(juce::Point<int>(0, 0)));
        canvas.editor.dragConnection(knob);
        canvas.editor.endConnectionDrag(knob);
    }

    CableEndGlide& glide() { return graph_editor_paint::CanvasMemo::of(canvas.editor)->cableEndGlide(); }

    std::optional<GraphEditor::VisibleCable> cable() {
        for (const auto& c : canvas.editor.buildVisibleCables())
            if (c.destNodeId == oscId.uid)
                return c;
        return std::nullopt;
    }
    juce::Point<float> end() { return cable().value().p2; }
};

float distance(juce::Point<float> a, juce::Point<float> b) { return a.getDistanceFrom(b); }

} // namespace

TEST(CableEndGlide, FoldingTheRowGlidesTheCableEndFromTheKnobToTheMoreButton) {
    GlideRig rig;
    ASSERT_TRUE(rig.cable().has_value());
    ASSERT_TRUE(rig.cable()->landsOnKnob);
    const auto onKnob = rig.end();

    FadeAnimateGuard guard;
    rig.body->setMoreUnfolded(false);
    ASSERT_TRUE(rig.glide().isLive());
    EXPECT_LT(distance(rig.end(), onKnob), 0.5f) << "the first frame has not moved";

    rig.glide().advanceForTest(0.5f);
    const auto mid = rig.end();
    rig.glide().completeForTest();
    const auto onButton = rig.end();

    EXPECT_GT(distance(onKnob, onButton), 5.0f) << "the knob and the More button are different places";
    EXPECT_LT(distance(mid, onKnob + (onButton - onKnob) * 0.5f), 0.5f) << "halfway through, halfway there";
    EXPECT_FALSE(rig.cable()->landsOnKnob);
    const auto button = rig.osc->getPosition().toFloat() +
                        juce::Point<float>(10.0f, (float)rig.body->getMoreButton()->getBounds().getCentreY());
    EXPECT_LT(distance(onButton, button), 0.5f) << "it ends at the left edge of the More button";
    EXPECT_FALSE(rig.glide().isLive());
    EXPECT_FALSE(rig.glide().isDriverRunningForTest());
}

TEST(CableEndGlide, UnfoldingTheRowGlidesTheCableEndFromTheButtonToTheKnob) {
    GlideRig rig;
    rig.body->setMoreUnfolded(false);
    const auto onButton = rig.end();

    FadeAnimateGuard guard;
    rig.body->setMoreUnfolded(true);
    ASSERT_TRUE(rig.glide().isLive());
    EXPECT_LT(distance(rig.end(), onButton), 0.5f);
    rig.glide().advanceForTest(0.5f);
    const auto mid = rig.end();
    rig.glide().completeForTest();
    const auto onKnob = rig.end();
    EXPECT_TRUE(rig.cable()->landsOnKnob);
    EXPECT_GT(distance(onKnob, onButton), 5.0f);
    EXPECT_LT(distance(mid, onButton + (onKnob - onButton) * 0.5f), 0.5f);
    EXPECT_FALSE(rig.glide().isDriverRunningForTest());
}

TEST(CableEndGlide, TheGlideFollowsACardThatMovesWhileItRuns) {
    GlideRig rig;
    FadeAnimateGuard guard;
    const auto before = rig.end();
    rig.body->setMoreUnfolded(false);
    rig.osc->setTopLeftPosition(rig.osc->getX() + 40, rig.osc->getY() + 25);
    EXPECT_LT(distance(rig.end(), before + juce::Point<float>(40.0f, 25.0f)), 0.5f)
        << "the recorded point is relative to the card, so the end moves with it";
}

TEST(CableEndGlide, WithAnimationsOffTheEndLandsAtOnce) {
    GlideRig rig;
    FadeAnimateGuard guard(AnimationMode::off);
    rig.body->setMoreUnfolded(false);
    EXPECT_FALSE(rig.glide().isLive());
    EXPECT_FALSE(rig.glide().isDriverRunningForTest());
    EXPECT_FALSE(rig.cable()->landsOnKnob);
    const auto button = rig.osc->getPosition().toFloat() +
                        juce::Point<float>(10.0f, (float)rig.body->getMoreButton()->getBounds().getCentreY());
    EXPECT_LT(distance(rig.end(), button), 0.5f);
}

TEST(CableEndGlide, ACardNotOnScreenLandsAtOnce) {
    GlideRig rig; // headless: no animate-off-screen guard
    rig.body->setMoreUnfolded(false);
    EXPECT_FALSE(rig.glide().isLive());
    EXPECT_FALSE(rig.cable()->landsOnKnob);
}

TEST(CableEndGlide, UnderReduceMotionItIsAPlainLinearMove) {
    GlideRig rig;
    FadeAnimateGuard guard(AnimationMode::reduced);
    const auto onKnob = rig.end();
    rig.body->setMoreUnfolded(false);
    ASSERT_TRUE(rig.glide().isLive());
    rig.glide().completeForTest();
    EXPECT_GT(distance(rig.end(), onKnob), 5.0f);
    EXPECT_FALSE(rig.glide().isLive());
}
