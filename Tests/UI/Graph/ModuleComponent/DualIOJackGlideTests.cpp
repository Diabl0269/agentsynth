// DualIOJackGlideTests.cpp -- clicking Dual I/O on a Channel Strip splits its one Audio jack into Left and Right with
// a glide, and merges them back the same way, the cables following the jack every frame; Cmd+Z and Cmd+Shift+Z glide
// too, a second step mid-glide starts from where things are drawn, and Reduce Motion and Animations Off land at once.
// The frames are recorded at t0, mid-way and the end. docs/layout/animation.md, "Dual I/O split and merge".
#include "../../Layout/UndoGlideSurfaces.h"
#include "DualIOGlideRig.h"
#include "UI/Layout/ReducedMotion.h"
#include <cmath>
#include <gtest/gtest.h>

using dual_io_glide_test::Rig;

namespace {

struct Motion {
    explicit Motion(synth::ui::AnimationMode mode, bool reduced = false) {
        synth::ui::setAnimationMode(mode);
        synth::ui::setReducedMotionForTest(reduced);
    }
    ~Motion() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};

struct Frames {
    double t0 = 0, mid = 0, end = 0;
};

// Steps the running glide frame by frame and checks the Right cable's end sits on the jack it lands on, every frame;
// returns the Right leg's height at t0, at 40 ms and once settled.
Frames recordRightLeg(Rig& rig, int rightJack, const char* what) {
    SCOPED_TRACE(what);
    Frames f;
    f.t0 = rig.rightCableY();
    EXPECT_TRUE(rig.gliding()) << "a glide is running";
    for (int frame = 1; frame <= 20; ++frame) {
        rig.stepTo(8.0 * frame);
        const auto end = rig.cableEndFrom(Rig::kOscRight);
        EXPECT_TRUE(end.has_value());
        if (frame == 5)
            f.mid = rig.rightCableY();
        if (end.has_value() && rightJack >= 0)
            EXPECT_NEAR(end->y, (float)rig.drawnInput(rightJack).y, 1.0f)
                << "the cable stays on its jack, frame " << frame;
    }
    rig.stepTo(1000.0);
    EXPECT_FALSE(rig.gliding()) << "it stops when settled";
    f.end = rig.rightCableY();
    return f;
}

void expectBetween(const Frames& f, double from, double to) {
    EXPECT_NEAR(f.t0, from, 1.5) << "the first frame is where it was";
    const double fraction = (f.mid - from) / (to - from);
    EXPECT_GT(fraction, 0.05) << "mid-way it has left";
    EXPECT_LT(fraction, 0.95) << "and not arrived";
    EXPECT_NEAR(f.end, to, 1.5) << "it settles on the laid-out jack";
}

} // namespace

// Regression test for FRO1829: the Audio jack popped into Left and Right in one frame, the cables jumping with it.
TEST(DualIOJackGlide, ClickingDualIOSplitsTheAudioJackAndMergesItBackWithAGlide) {
    Motion motion(synth::ui::AnimationMode::full);
    Rig rig;
    const double audioY = rig.card->getPortCenter(0, true).y;
    ASSERT_NEAR(rig.rightCableY(), audioY, 1.0) << "collapsed, both legs land on the one Audio jack";

    rig.clickDualIO();
    const double rightY = rig.card->getPortCenter(1, true).y;
    ASSERT_GT(rightY - audioY, 10.0) << "split, Right has a row of its own";
    EXPECT_NEAR(rig.drawnInput(1).y, audioY, 1.0) << "Right starts out of the Audio jack";
    EXPECT_LT(rig.glide()->labelAlpha(1, true), 0.1f) << "its label fades in";
    expectBetween(recordRightLeg(rig, 1, "split"), audioY, rightY);
    EXPECT_FLOAT_EQ(rig.glide()->labelAlpha(1, true), 1.0f);

    rig.clickDualIO();
    expectBetween(recordRightLeg(rig, -1, "merge"), rightY, audioY);
    EXPECT_NEAR(rig.drawnInput(0).y, audioY, 1.0) << "the merged jack is the Audio jack";
}

// Regression test for FRO1829: from a collapsed Oscillator the strip's Right cable is a second leg on both ends, so a
// merge dropped it in one frame and a split popped it in; and the controls below the jacks jumped a row at once.
TEST(DualIOJackGlide, FromACollapsedSourceTheRightCableSlidesInAndOutAndTheControlsFollow) {
    Motion motion(synth::ui::AnimationMode::full);
    Rig rig(false);
    ASSERT_LT(rig.rightCableY(), 0.0) << "at rest the collapsed pair draws one cable";
    const double audioY = rig.card->getPortCenter(0, true).y;
    auto* control = rig.bodyControl();
    ASSERT_NE(control, nullptr);
    const double controlBefore = Rig::drawnTop(*control);

    rig.clickDualIO();
    const double rightY = rig.card->getPortCenter(1, true).y;
    const double controlAfter = control->getY();
    ASSERT_GT(controlAfter - controlBefore, 4.0) << "the split pushes the controls down a row";
    EXPECT_NEAR(Rig::drawnTop(*control), controlBefore, 1.0) << "drawn where it was on the first frame";
    expectBetween(recordRightLeg(rig, 1, "split, from a collapsed source"), audioY, rightY);
    EXPECT_NEAR(Rig::drawnTop(*control), controlAfter, 0.5) << "and settled with no transform left";

    rig.clickDualIO();
    EXPECT_GE(rig.rightCableY(), 0.0) << "the merge keeps the Right cable drawn";
    EXPECT_NEAR(rig.rightCableY(), rightY, 1.5);
    EXPECT_NEAR(Rig::drawnTop(*control), controlAfter, 1.0);
    for (int frame = 1; frame <= 20; ++frame) {
        rig.stepTo(8.0 * frame);
        if (rig.gliding()) {
            EXPECT_GE(rig.rightCableY(), 0.0) << "frame " << frame;
            EXPECT_NEAR(Rig::drawnTop(*control) - controlBefore, rig.rightCableY() - audioY, 1.5)
                << "the controls move with the jack row, frame " << frame;
        }
    }
    rig.stepTo(1000.0);
    EXPECT_LT(rig.rightCableY(), 0.0) << "landed, the merged pair draws one cable again";
    EXPECT_NEAR(Rig::drawnTop(*control), controlBefore, 0.5);
}

TEST(DualIOJackGlide, TheStripsOutputSplitsAndMergesTheSameWay) {
    Motion motion(synth::ui::AnimationMode::full);
    Rig rig;
    const double outY = rig.card->getPortCenter(0, false).y;
    rig.clickDualIO();
    const double rightOutY = rig.card->getPortCenter(1, false).y;
    ASSERT_GT(rightOutY - outY, 10.0);
    EXPECT_NEAR(rig.drawnOutput(1).y, outY, 1.0) << "Right out starts out of the one output jack";
    rig.stepTo(40.0);
    EXPECT_GT(rig.drawnOutput(1).y, outY + 1.0);
    EXPECT_LT(rig.drawnOutput(1).y, rightOutY - 1.0);
    rig.stepTo(1000.0);
    EXPECT_NEAR(rig.drawnOutput(1).y, rightOutY, 1.0);
}

TEST(DualIOJackGlide, UndoAndRedoGlideLikeTheClick) {
    Motion motion(synth::ui::AnimationMode::full);
    Rig rig;
    const double audioY = rig.card->getPortCenter(0, true).y;
    rig.clickDualIO();
    rig.stepTo(1000.0);
    const double rightY = rig.card->getPortCenter(1, true).y;

    ASSERT_TRUE(rig.undo.undo());
    expectBetween(recordRightLeg(rig, -1, "undo merges"), rightY, audioY);
    ASSERT_TRUE(rig.undo.redo());
    expectBetween(recordRightLeg(rig, 1, "redo splits"), audioY, rightY);
}

TEST(DualIOJackGlide, ASecondUndoMidGlideStartsFromWhereTheJackIsDrawn) {
    Motion motion(synth::ui::AnimationMode::full);
    Rig rig;
    const double audioY = rig.card->getPortCenter(0, true).y;
    rig.clickDualIO();
    rig.stepTo(1000.0);
    const double rightY = rig.card->getPortCenter(1, true).y;

    ASSERT_TRUE(rig.undo.undo());
    rig.stepTo(40.0);
    const double atPress = rig.rightCableY();
    ASSERT_GT(rightY - atPress, 2.0) << "the undo has left";
    ASSERT_TRUE(rig.undo.redo());
    EXPECT_NEAR(rig.rightCableY(), atPress, 1.5) << "the redo starts from the drawn place, nothing jumps";
    EXPECT_NEAR(rig.drawnInput(1).y, atPress, 1.5) << "and so does the jack";
    rig.stepTo(1000.0);
    EXPECT_NEAR(rig.rightCableY(), rightY, 1.5);
    EXPECT_GT(rightY, audioY);
}

TEST(DualIOJackGlide, ReduceMotionAndAnimationsOffLandAtOnce) {
    for (const bool reduced : {true, false}) {
        SCOPED_TRACE(reduced ? "Reduce Motion" : "Animations Off");
        Motion motion(reduced ? synth::ui::AnimationMode::reduced : synth::ui::AnimationMode::off, reduced);
        Rig rig;
        rig.clickDualIO();
        EXPECT_FALSE(rig.gliding());
        EXPECT_NEAR(rig.rightCableY(), rig.card->getPortCenter(1, true).y, 1.0) << "Right is at its row at once";
        ASSERT_TRUE(rig.undo.undo());
        EXPECT_FALSE(rig.gliding());
        EXPECT_NEAR(rig.rightCableY(), rig.card->getPortCenter(0, true).y, 1.0);
    }
}

TEST(DualIOJackGlide, ACardNeverPaintedLandsAtOnce) {
    Motion motion(synth::ui::AnimationMode::full);
    Rig rig;
    rig.card->getDualIOGlide()->noteDrawn(true, {});
    rig.card->getDualIOGlide()->noteDrawn(false, {});
    rig.clickDualIO();
    EXPECT_FALSE(rig.gliding()) << "nothing was on screen to glide from";
}

// The undo-glide registry's "Dual I/O jack split" entry (UndoGlideSurfaces.h): a toggle that changes the jack layout,
// whose undo and redo glide like the click. drawn() is the Right cable's end on the strip.
namespace undo_glide_test {
namespace {
class DualIOSplitSurface : public Surface {
public:
    void move() override { rig_.clickDualIO(); }
    bool undo() override { return rig_.undo.undo(); }
    bool redo() override { return rig_.undo.redo(); }
    double drawn() const override { return rig_.rightCableY(); }
    bool gliding() const override { return rig_.gliding(); }
    void stepTo(double elapsedMs) override { rig_.stepTo(elapsedMs); }

private:
    mutable Rig rig_;
};
} // namespace

std::unique_ptr<Surface> makeDualIOSplit() { return std::make_unique<DualIOSplitSurface>(); }
} // namespace undo_glide_test
