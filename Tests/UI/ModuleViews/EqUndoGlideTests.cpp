// EqUndoGlideTests.cpp -- what an undo or redo does to a Parametric EQ's drawn bands beyond the shared checklist
// (UndoGlideSurfaces): an added band fades out and back in where it stands, frequency, gain, Q and on/off move
// together, a drag still lands at once, and Cmd+Z / Cmd+Shift+Z reach the app from the curve and from the EQ window.
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FX/ParametricEQModule.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/ModuleViews/EQWindow.h"
#include "UI/ModuleViews/EqBandGlide.h"
#include <gtest/gtest.h>

namespace {

struct FullMotion {
    FullMotion() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
        synth::ui::setReducedMotionForTest(false);
    }
    ~FullMotion() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};

class EqUndoGlide : public ::testing::Test {
protected:
    EqUndoGlide() {
        auto* eq = new ParametricEQModule();
        module_ = eq;
        node_ = engine_.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(eq));
        window_ = std::make_unique<EQWindow>(*module_);
        window_->setGestureCallbacks([this] { undo_.captureBeforeState(engine_.getGraph()); },
                                     [this] { undo_.pushSnapshotFromCapture(engine_.getGraph()); });
        window_->setUndoManager(&undo_);
        curve().getBandGlideForTest().forceAnimateForTest(true);
    }

    EQCurveComponent& curve() { return window_->getCurve(); }
    void restore(bool forward) {
        ASSERT_TRUE(forward ? undo_.redo() : undo_.undo());
        curve().timerCallback();
    }
    void step(double ms) { curve().getBandGlideForTest().stepForTest(ms); }

    FullMotion motion_;
    AudioEngine engine_;
    AppUndoManager undo_;
    juce::AudioProcessorGraph::Node::Ptr node_;
    ParametricEQModule* module_ = nullptr; // owned by the graph
    std::unique_ptr<EQWindow> window_;
};

} // namespace

TEST_F(EqUndoGlide, UndoingAnAddFadesTheBandOutWhereItStoodAndRedoBringsItBackIn) {
    const int band = curve().addPointAt({300.0f, 120.0f});
    ASSERT_GE(band, 0);
    step(1000.0);
    const auto stood = curve().getDrawnBandPosition(band);
    ASSERT_FLOAT_EQ(curve().getDrawnBandAlpha(band), 1.0f);

    restore(false); // undo the add
    EXPECT_FALSE(module_->isBandEnabled(band)) << "the band is off at once";
    EXPECT_GT(curve().getDrawnBandAlpha(band), 0.95f) << "but is still drawn on the first frame";
    step(40.0);
    EXPECT_GT(curve().getDrawnBandAlpha(band), 0.02f);
    EXPECT_LT(curve().getDrawnBandAlpha(band), 0.98f) << "fading out";
    EXPECT_EQ(curve().getDrawnBandPosition(band), stood) << "where it stood, not at its hidden settings";
    step(1000.0);
    EXPECT_FLOAT_EQ(curve().getDrawnBandAlpha(band), 0.0f);
    EXPECT_FALSE(curve().getBandGlideForTest().isAnimating());

    restore(true); // redo it
    EXPECT_LT(curve().getDrawnBandAlpha(band), 0.05f) << "starts invisible";
    step(40.0);
    EXPECT_GT(curve().getDrawnBandAlpha(band), 0.02f);
    EXPECT_LT(curve().getDrawnBandAlpha(band), 0.98f) << "fading in";
    EXPECT_EQ(curve().getDrawnBandPosition(band), stood) << "at its place from the first frame";
    step(1000.0);
    EXPECT_FLOAT_EQ(curve().getDrawnBandAlpha(band), 1.0f);
}

TEST_F(EqUndoGlide, FrequencyGainAndQMoveTogetherInOneGlide) {
    const int band = curve().addPointAt({300.0f, 120.0f});
    step(1000.0);
    const auto before = curve().getDrawnBandPosition(band);
    const float qBefore = module_->getBandSnapshots()[(size_t)band].q;

    undo_.captureBeforeState(engine_.getGraph());
    module_->setBandFreq(band, 4000.0f);
    module_->setBandGain(band, -9.0f);
    module_->setBandQ(band, 4.0f);
    undo_.pushSnapshotFromCapture(engine_.getGraph());
    curve().timerCallback(); // a card knob edit: not a restore, lands at once
    EXPECT_FALSE(curve().getBandGlideForTest().isAnimating());
    const auto moved = curve().getDrawnBandPosition(band);
    ASSERT_NE(moved, before);

    restore(false);
    using synth::ui::EqBandGlide;
    auto& tween = curve().getBandGlideForTest();
    EXPECT_NE(tween.offsetFor(EqBandGlide::placeKey(band)).x, 0.0f) << "frequency glides";
    EXPECT_NE(tween.offsetFor(EqBandGlide::placeKey(band)).y, 0.0f) << "gain glides";
    EXPECT_NE(tween.offsetFor(EqBandGlide::shapeKey(band)).x, 0.0f) << "Q glides";
    step(40.0);
    const auto mid = curve().getDrawnBandPosition(band);
    EXPECT_LT(mid.x, moved.x) << "frequency is between its two places";
    EXPECT_GT(mid.x, before.x);
    EXPECT_LT(mid.y, moved.y) << "gain is between its two places";
    EXPECT_GT(mid.y, before.y);
    step(1000.0);
    EXPECT_NEAR(curve().getDrawnBandPosition(band).x, before.x, 0.01f);
    EXPECT_NEAR(module_->getBandSnapshots()[(size_t)band].q, qBefore, 1.0e-4f);
}

TEST_F(EqUndoGlide, ADragLandsAtOnceEvenRightAfterARestore) {
    const int band = curve().addPointAt({300.0f, 120.0f});
    step(1000.0);
    restore(false);
    step(40.0);
    ASSERT_TRUE(curve().getBandGlideForTest().isAnimating());
    const int again = curve().addPointAt({420.0f, 80.0f}); // a new edit mid-glide
    ASSERT_GE(again, 0);
    EXPECT_FALSE(curve().getBandGlideForTest().isAnimating()) << "the glide ends and the edit lands at once";
    EXPECT_EQ(curve().getDrawnBandPosition(again), curve().bandPosition(module_->getBandSnapshots()[(size_t)again]));
    juce::ignoreUnused(band);
}

TEST_F(EqUndoGlide, AnEditAfterARestoreOfSomethingElseDoesNotGlide) {
    const int band = curve().addPointAt({300.0f, 120.0f});
    step(1000.0);
    undo_.captureBeforeState(engine_.getGraph());
    module_->setBandGain(band, 12.0f);
    undo_.pushSnapshotFromCapture(engine_.getGraph());
    curve().timerCallback();
    step(1000.0);
    EXPECT_TRUE(undo_.undo());
    // The poll runs before the next ordinary edit: the restore is spent, a later change must land at once.
    curve().timerCallback();
    step(1000.0);
    module_->setBandGain(band, -6.0f);
    curve().timerCallback();
    EXPECT_FALSE(curve().getBandGlideForTest().isAnimating());
}

TEST_F(EqUndoGlide, CmdZAndCmdShiftZReachTheAppFromTheCurveAndTheWindow) {
    const juce::KeyPress undoKey('z', juce::ModifierKeys::commandModifier, 0);
    const juce::KeyPress redoKey('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    EXPECT_FALSE(curve().keyPressed(undoKey)) << "the curve leaves Cmd+Z to its parents";
    EXPECT_FALSE(curve().keyPressed(redoKey));

    std::vector<juce::KeyPress> relayed;
    window_->onAppShortcut = [&relayed](const juce::KeyPress& key) {
        relayed.push_back(key);
        return true;
    };
    EXPECT_TRUE(window_->keyPressed(undoKey));
    EXPECT_TRUE(window_->keyPressed(redoKey));
    ASSERT_EQ(relayed.size(), 2u);
    EXPECT_EQ(relayed[0], undoKey);
    EXPECT_EQ(relayed[1], redoKey);
}
