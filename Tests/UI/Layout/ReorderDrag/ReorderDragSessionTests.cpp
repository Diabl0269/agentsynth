// ReorderDragSessionTests.cpp: the owner-side glue around ReorderDragAnimator on a real, showing owner, where the
// animated path is taken. Every list that reorders by drag (mixer inserts and sends, lanes, card layout rows) runs on
// it.
#include "../NativeWindowTestGuard.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/ReorderDrag/ReorderDragSession.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::AnimationMode;
using synth::ui::ReorderDragAnimator;
using synth::ui::ReorderDragSession;

std::vector<ReorderDragAnimator::Slot> threeRows() { return {{0, 40}, {40, 40}, {80, 40}}; }

struct AnimationsOff {
    AnimationMode before = synth::ui::animationMode();
    AnimationsOff() { synth::ui::setAnimationMode(AnimationMode::off); }
    ~AnimationsOff() { synth::ui::setAnimationMode(before); }
};

struct ReduceMotionOn {
    ReduceMotionOn() { synth::ui::setReducedMotionForTest(true); }
    ~ReduceMotionOn() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

struct ShowingOwner {
    juce::Component owner;
    ShowingOwner() {
        owner.setSize(200, 200);
        owner.addToDesktop(juce::ComponentPeer::windowIsTemporary);
        owner.setVisible(true);
    }
    ~ShowingOwner() { owner.removeFromDesktop(); }
};

} // namespace

// Regression test for FRO1480: with Animations Off the frame pump lands at once, while the session still timed its
// tweens on the real clock, so a keyboard lane move stayed "reordering" for good and refused every later move.
TEST(ReorderDragSessionTest, UnderAnimationsOffEverySettleOnAShowingOwnerLandsAtOnce) {
    if (nativeWindowsAbortHere())
        GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
    AnimationsOff off;
    ShowingOwner rig;
    ASSERT_TRUE(rig.owner.isShowing());
    int frames = 0;
    ReorderDragSession session(rig.owner, [&] { ++frames; });

    // A keyboard move: the list is already rebuilt and the moved row glides in from where it stood.
    auto moved = threeRows();
    session.settleInto(moved, 1, 0.0f);
    EXPECT_FALSE(session.isReordering()) << "the keyboard move's settle lands at once";
    EXPECT_FLOAT_EQ(session.animator().getLayoutStart(1), 40.0f);

    // A mouse drag dropped one place down.
    session.begin(threeRows(), 0, 10.0f, 10.0f);
    ASSERT_TRUE(session.dragTo(60.0f));
    ASSERT_EQ(session.end(), ReorderDragSession::End::Commit);
    session.release({40.0f, 0.0f, 80.0f});
    EXPECT_FALSE(session.isReordering()) << "the drop lands at once";

    // A mouse drag cancelled with Esc.
    session.begin(threeRows(), 0, 10.0f, 10.0f);
    ASSERT_TRUE(session.dragTo(60.0f));
    session.abort();
    EXPECT_FALSE(session.isReordering()) << "Esc puts everything back at once";
    EXPECT_FLOAT_EQ(session.animator().getLayoutStart(0), 0.0f);
}

// Regression test for FRO1503: Reduce Motion has no plain-fade form for a reorder, so every settle on a showing owner
// lands at once, as under Animations Off.
TEST(ReorderDragSessionTest, UnderReduceMotionEverySettleOnAShowingOwnerLandsAtOnce) {
    if (nativeWindowsAbortHere())
        GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
    ReduceMotionOn reduced;
    ShowingOwner rig;
    ASSERT_TRUE(rig.owner.isShowing());
    ReorderDragSession session(rig.owner, [] {});

    auto moved = threeRows();
    session.settleInto(moved, 1, 0.0f);
    EXPECT_FALSE(session.isReordering()) << "the keyboard move's settle lands at once";
    EXPECT_FLOAT_EQ(session.animator().getLayoutStart(1), 40.0f);

    session.begin(threeRows(), 0, 10.0f, 10.0f);
    ASSERT_TRUE(session.dragTo(60.0f));
    ASSERT_EQ(session.end(), ReorderDragSession::End::Commit);
    session.release({40.0f, 0.0f, 80.0f});
    EXPECT_FALSE(session.isReordering()) << "the drop lands at once";
}
