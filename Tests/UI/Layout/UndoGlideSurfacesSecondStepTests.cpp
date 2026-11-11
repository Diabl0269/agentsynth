// UndoGlideSurfacesSecondStepTests.cpp -- a second undo or redo pressed while the first one still glides. Every
// surface in UndoGlideSurfaces.h's list must take it from where the thing is drawn at that moment: nothing jumps at the
// second press, and the glide settles at the second step's place (docs/layout/undo-glide.md). The canvas's macro fold,
// which is not a surface of that list, has the same guard in MacroFoldSecondStepMidMotion.
#include "UI/Layout/ReducedMotion.h"
#include "UndoGlideSurfaces.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace undo_glide_test;

namespace {

struct FullMotionScope {
    FullMotionScope() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
        synth::ui::setReducedMotionForTest(false);
    }
    ~FullMotionScope() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};

// Regression test for FRO1664: a second step mid-motion snapped the running motion to its end first.
void expectSecondStepContinues(Surface& s, bool undoFirst, double from, double to, const char* what) {
    SCOPED_TRACE(what);
    ASSERT_TRUE(undoFirst ? s.undo() : s.redo());
    s.stepTo(40.0);
    ASSERT_TRUE(s.gliding()) << "the first step still glides at 40 ms";
    const double drawnAtPress = s.drawn();
    ASSERT_GT(std::abs(drawnAtPress - from), 0.5) << "the first step has left its old place";
    ASSERT_TRUE(undoFirst ? s.redo() : s.undo());
    EXPECT_NEAR(s.drawn(), drawnAtPress, 1.5) << "the second press starts from where it is drawn";
    s.stepTo(1000.0);
    EXPECT_FALSE(s.gliding()) << "it stops when settled";
    EXPECT_NEAR(s.drawn(), to, 1.5) << "and settles where the second step puts it";
}

} // namespace

TEST(UndoGlideSurfaces, ASecondStepMidGlideStartsFromTheDrawnPlace) {
    for (const auto& entry : surfaces()) {
        SCOPED_TRACE(entry.name);
        FullMotionScope motion;
        auto s = entry.make();
        const double before = s->drawn();
        s->move();
        s->stepTo(1000.0);
        const double moved = s->drawn();
        // Undo, then redo while the undo still glides: it heads back to the moved place.
        expectSecondStepContinues(*s, true, moved, moved, "redo mid-undo");
        // Undo after that settled, then the mirror: redo, and undo while the redo still glides.
        ASSERT_TRUE(s->undo());
        s->stepTo(1000.0);
        expectSecondStepContinues(*s, false, before, before, "undo mid-redo");
    }
}
