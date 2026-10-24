// UndoGlideSurfacesTests.cpp -- the guard behind the standing rule that anything which glides forward glides on undo
// and redo: it walks UndoGlideSurfaces.h's one list, so a move path that snaps back fails here by name.
#include "UI/Layout/ReducedMotion.h"
#include "UndoGlideSurfaces.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace undo_glide_test;

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

// One restore step: drawn at `from` on the first frame, strictly between the two places a little later, settled at
// `to`.
void expectGlide(Surface& s, double from, double to, const char* what) {
    SCOPED_TRACE(what);
    ASSERT_GT(std::abs(to - from), 4.0) << "the move changed the place";
    EXPECT_TRUE(s.gliding()) << "a glide is running";
    EXPECT_NEAR(s.drawn(), from, 1.5) << "the first frame is still at the old place";
    s.stepTo(40.0);
    const double mid = s.drawn();
    const double fraction = (mid - from) / (to - from);
    EXPECT_GT(fraction, 0.02) << "it has left the old place";
    EXPECT_LT(fraction, 0.98) << "it has not snapped to the new one";
    s.stepTo(1000.0);
    EXPECT_FALSE(s.gliding()) << "it stops when settled";
    EXPECT_NEAR(s.drawn(), to, 1.5);
}

} // namespace

TEST(UndoGlideSurfaces, EveryMoveSurfaceGlidesOnUndoAndRedo) {
    ASSERT_FALSE(surfaces().empty());
    for (const auto& entry : surfaces()) {
        SCOPED_TRACE(entry.name);
        FullMotion motion;
        auto s = entry.make();
        const double before = s->drawn();
        s->move();
        s->stepTo(1000.0);
        const double moved = s->drawn();
        ASSERT_TRUE(s->undo());
        expectGlide(*s, moved, before, "undo glides back");
        ASSERT_TRUE(s->redo());
        expectGlide(*s, before, moved, "redo glides forward");
    }
}

TEST(UndoGlideSurfaces, UnderReduceMotionARestoredThingLandsAtOnceAndFadesIn) {
    for (const auto& entry : surfaces()) {
        SCOPED_TRACE(entry.name);
        FullMotion motion;
        auto s = entry.make();
        const double before = s->drawn();
        s->move();
        s->stepTo(1000.0);
        synth::ui::setAnimationMode(synth::ui::AnimationMode::reduced);
        synth::ui::setReducedMotionForTest(true);
        ASSERT_TRUE(s->undo());
        EXPECT_NEAR(s->drawn(), before, 1.5) << "it is at its new place on the first frame";
        if (entry.fadesUnderReduceMotion) {
            EXPECT_LT(s->alpha(), 0.9f) << "and starts faint";
            EXPECT_TRUE(s->gliding()) << "the fade runs";
            s->stepTo(1000.0);
        }
        EXPECT_FALSE(s->gliding()) << "settled";
        EXPECT_FLOAT_EQ(s->alpha(), 1.0f);
        EXPECT_NEAR(s->drawn(), before, 1.5);
    }
}

TEST(UndoGlideSurfaces, WithAnimationsOffEveryRestoreIsInstant) {
    for (const auto& entry : surfaces()) {
        SCOPED_TRACE(entry.name);
        FullMotion motion;
        auto s = entry.make();
        const double before = s->drawn();
        s->move();
        s->stepTo(1000.0);
        synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
        ASSERT_TRUE(s->undo());
        EXPECT_FALSE(s->gliding());
        EXPECT_FLOAT_EQ(s->alpha(), 1.0f);
        EXPECT_NEAR(s->drawn(), before, 1.5);
    }
}
