// CanvasFrameTests.cpp
//
// The growing canvas frame: pure sizing (CanvasFrame::targetFor), the update modes, and the GraphEditor glue (frame
// follows the outermost card, content is target + slack, no 10000 px wall, drops at x=20000 stay put).
// (docs/layout/layout.md#canvas-frame)

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CanvasFrame/CanvasFrame.h"
#include <gtest/gtest.h>

namespace {

using Mode = CanvasFrame::Mode;

struct Rig {
    juce::Component host;
    juce::VBlankAnimatorUpdater updater{&host};
    CanvasFrame frame{updater};
    int changes = 0;
    Rig() {
        frame.onChanged = [this] { ++changes; };
    }
};

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    Canvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
    }
    NodeID osc(int x, int y) { return addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), x, y); }
    CanvasFrame& frame() { return editor.getCanvasFrameForTest(); }
    void land() {
        editor.updateComponents();
        frame().finishAnimationForTest();
    }
};

} // namespace

TEST(CanvasFrameTarget, EmptyUnionIsTheStartSize) {
    EXPECT_EQ(CanvasFrame::targetFor({}), juce::Rectangle<int>(0, 0, 1800, 1100));
}

TEST(CanvasFrameTarget, ContentBelowTheFloorStaysAtTheStartSize) {
    EXPECT_EQ(CanvasFrame::targetFor({100, 100, 300, 300}), juce::Rectangle<int>(0, 0, 1800, 1100));
}

TEST(CanvasFrameTarget, GrowsInFourHundredPixelStepsPastTheOutermostCard) {
    // Right 2050 + 400 = 2450 -> 2800; bottom 1250 + 400 = 1650 -> 2000.
    EXPECT_EQ(CanvasFrame::targetFor({1000, 900, 1050, 350}), juce::Rectangle<int>(0, 0, 2800, 2000));
    // Exactly on a step: 2000 + 400 = 2400 stays 2400.
    EXPECT_EQ(CanvasFrame::targetFor({1800, 100, 200, 100}).getWidth(), 2400);
}

TEST(CanvasFrameUpdate, GrowOnlyIgnoresShrinkAndAnimateShrinks) {
    Rig r;
    r.frame.update({0, 0, 5000, 100}, Mode::Snap);
    ASSERT_EQ(r.frame.target().getWidth(), 5600);
    const int before = r.changes;
    r.frame.update({0, 0, 100, 100}, Mode::GrowOnly);
    EXPECT_EQ(r.frame.target().getWidth(), 5600);
    EXPECT_EQ(r.changes, before) << "an unchanged target fires nothing";
    r.frame.update({0, 0, 100, 100}, Mode::Animate);
    EXPECT_EQ(r.frame.target(), juce::Rectangle<int>(0, 0, 1800, 1100));
    r.frame.finishAnimationForTest();
    EXPECT_EQ(r.frame.current(), juce::Rectangle<float>(0.0f, 0.0f, 1800.0f, 1100.0f));
}

TEST(CanvasFrameUpdate, SnapIsInstantAndAnimateIsNot) {
    Rig r;
    r.frame.update({0, 0, 3000, 100}, Mode::Snap);
    EXPECT_FALSE(r.frame.isAnimating());
    EXPECT_EQ(r.frame.current().getWidth(), 3600.0f);

    r.frame.update({0, 0, 5000, 100}, Mode::Animate);
    EXPECT_TRUE(r.frame.isAnimating());
    EXPECT_EQ(r.frame.current().getWidth(), 3600.0f) << "the tween has not run a frame yet";
    EXPECT_EQ(r.frame.target().getWidth(), 5600);
}

TEST(CanvasFrameUpdate, SameTargetFiresNoCallback) {
    Rig r;
    r.frame.update({0, 0, 3000, 100}, Mode::Snap);
    const int before = r.changes;
    for (auto mode : {Mode::Animate, Mode::GrowOnly, Mode::Snap})
        r.frame.update({0, 0, 3000, 100}, mode);
    EXPECT_EQ(r.changes, before);
}

TEST(CanvasFrameUpdate, SnapRequestedAppliesToTheNextAnimateUpdateOnly) {
    Rig r;
    r.frame.requestSnapOnNextUpdate();
    r.frame.update({0, 0, 3000, 100}, Mode::GrowOnly); // the 30 Hz tick must not eat the request
    r.frame.update({0, 0, 4000, 100}, Mode::Animate);
    EXPECT_FALSE(r.frame.isAnimating());
    EXPECT_EQ(r.frame.current().getWidth(), 4400.0f);
    r.frame.update({0, 0, 6000, 100}, Mode::Animate);
    EXPECT_TRUE(r.frame.isAnimating());
}

TEST(CanvasFrameUpdate, ShiftMovesTheAnimatedRect) {
    Rig r;
    r.frame.shiftCurrentBy({-50.0f, 20.0f});
    EXPECT_EQ(r.frame.current().getPosition(), juce::Point<float>(-50.0f, 20.0f));
}

TEST(CanvasFrameEditor, FrameFollowsTheOutermostCardAndShrinksBack) {
    Canvas c;
    EXPECT_EQ(c.editor.getCanvasFrameRect(), juce::Rectangle<float>(0.0f, 0.0f, 1800.0f, 1100.0f));

    const auto id = c.osc(5000, 300);
    c.land();
    const auto* comp = findComponent(c.editor, id);
    ASSERT_NE(comp, nullptr);
    const float minW = static_cast<float>(5000 + comp->getWidth() + CanvasFrame::kPad);
    EXPECT_GE(c.editor.getCanvasFrameRect().getWidth(), minW);

    // Content covers the frame TARGET plus slack, so a fast drag never clips a card.
    auto* content = c.editor.getModuleComponents()[0]->getParentComponent();
    ASSERT_NE(content, nullptr);
    EXPECT_GE(content->getWidth(), static_cast<int>(c.editor.getCanvasFrameRect().getWidth()));
    EXPECT_EQ(content->getWidth(), c.frame().target().getWidth() + CanvasFrame::kContentSlack);

    c.engine.getGraph().removeNode(id);
    c.land();
    EXPECT_EQ(c.editor.getCanvasFrameRect().getWidth(), 1800.0f);
    EXPECT_EQ(c.editor.getCanvasFrameRect().getHeight(), 1100.0f);
}

TEST(CanvasFrameEditor, ADropFarRightIsNotClampedToTenThousand) {
    Canvas c;
    const auto id = c.osc(300, 300);
    auto* comp = findComponent(c.editor, id);
    ASSERT_NE(comp, nullptr);
    comp->setTopLeftPosition(20000, 300);
    c.editor.finalizeModuleDrag(comp);
    EXPECT_EQ(comp->getX(), 20000);
    EXPECT_GE(c.frame().target().getWidth(), 20000 + comp->getWidth() + CanvasFrame::kPad);
}
