// Where the mod dot's panel goes: beside the dot, slid up near the bottom of the screen with the arrow tip level with
// the dot's centre the whole way, one outline for body and arrow, and the frame that follows the panel's growth.
// docs/modules/modulation.md#the-mod-dot-menu.

#include "UI/Graph/ModDot/ModDotPanelFrame.h"
#include "UI/Graph/ModDot/ModDotPanelGeometry.h"
#include <gtest/gtest.h>

namespace {

namespace panel = synth::ui::modDotPanel;
using synth::ui::ModDotPanelFrame;

const juce::Rectangle<int> kScreen(0, 0, 800, 600);

} // namespace

TEST(ModDotPanelGeometry, OpensRightOfTheDotWithTheArrowOnTheDotsCentreLine) {
    const juce::Rectangle<int> dot(100, 200, 12, 12);
    const auto p = panel::place(dot, {280, 120}, kScreen);

    EXPECT_TRUE(p.arrowOnLeft);
    EXPECT_GE(p.panel.getX(), dot.getRight()) << "beside the dot, not over it";
    EXPECT_EQ(p.tipY, dot.getCentreY());
    EXPECT_EQ(p.tipX, dot.getRight() + panel::kGap) << "the tip sits just off the dot";
    EXPECT_EQ(p.panel.getWidth(), 280);
    EXPECT_EQ(p.panel.getHeight(), 120);
}

TEST(ModDotPanelGeometry, NearTheBottomThePanelSlidesUpAndTheTipStaysLevelWithTheDot) {
    const juce::Rectangle<int> dot(100, 520, 12, 12);

    int lastTop = 100000;
    for (const int height : {120, 220, 320, 420}) { // the panel growing, as the list unfolds
        const auto p = panel::place(dot, {280, height}, kScreen);
        EXPECT_LE(p.panel.getBottom(), kScreen.getBottom() - panel::kScreenMargin) << "fully on screen at " << height;
        EXPECT_GE(p.panel.getY(), kScreen.getY());
        EXPECT_EQ(p.tipY, dot.getCentreY()) << "the arrow tip stays level with the dot centre at " << height;
        EXPECT_GE(p.tipY, p.panel.getY());
        EXPECT_LE(p.tipY, p.panel.getBottom());
        EXPECT_LE(p.panel.getY(), lastTop) << "growing never moves the panel down";
        lastTop = p.panel.getY();
    }
}

TEST(ModDotPanelGeometry, FlipsToTheLeftWhenThereIsNoRoomOnTheRight) {
    const juce::Rectangle<int> dot(700, 200, 12, 12);
    const auto p = panel::place(dot, {280, 150}, kScreen);

    EXPECT_FALSE(p.arrowOnLeft);
    EXPECT_LE(p.panel.getRight(), dot.getX());
    EXPECT_EQ(p.tipX, dot.getX() - panel::kGap);
    EXPECT_EQ(p.tipY, dot.getCentreY());
}

TEST(ModDotPanelGeometry, ATallPanelIsCappedToTheScreenAndKeepsItsArrowReachable) {
    const juce::Rectangle<int> dot(100, 590, 12, 12);
    const auto p = panel::place(dot, {280, 5000}, kScreen);

    EXPECT_EQ(p.panel.getHeight(), panel::maxPanelHeight(kScreen));
    EXPECT_TRUE(kScreen.contains(p.panel));
    EXPECT_GE(p.tipY, p.panel.getY());
    EXPECT_LE(p.tipY, p.panel.getBottom());
}

TEST(ModDotPanelGeometry, TheOutlineIsOneClosedPathThatTakesInTheArrow) {
    const juce::Rectangle<int> dot(100, 200, 12, 12);
    for (const auto& d : {dot, juce::Rectangle<int>(700, 200, 12, 12)}) {
        const auto p = panel::place(d, {280, 150}, kScreen);
        const auto path = panel::outline(p);
        const auto bounds = path.getBounds();

        const float tipEdge = p.arrowOnLeft ? bounds.getX() : bounds.getRight();
        EXPECT_FLOAT_EQ(tipEdge, (float)p.tipX) << "the arrow is part of the same path";
        EXPECT_TRUE(path.contains((float)p.panel.getCentreX(), (float)p.panel.getCentreY()));
        const float nearTip = p.arrowOnLeft ? (float)p.tipX + 2.0f : (float)p.tipX - 2.0f;
        EXPECT_TRUE(path.contains(nearTip, (float)p.tipY)) << "inside the arrow";
        EXPECT_FALSE(path.contains(nearTip, (float)p.tipY - 12.0f)) << "and nothing wider than the arrow";

        int subPaths = 0;
        for (juce::Path::Iterator it(path); it.next();)
            subPaths += it.elementType == juce::Path::Iterator::startNewSubPath ? 1 : 0;
        EXPECT_EQ(subPaths, 1);
    }
}

TEST(ModDotPanelFrameTest, FollowsThePanelsGrowthKeepingTheArrowLevelWithTheDotAndOnScreen) {
    juce::Component screen;
    screen.setBounds(kScreen);
    juce::Component anchor;
    const juce::Rectangle<int> dot(100, 500, 12, 12);
    auto content = std::make_unique<juce::Component>();
    auto* contentPtr = content.get();
    contentPtr->setSize(280, 120);
    ModDotPanelFrame frame(std::move(content), anchor, dot, kScreen);
    screen.addAndMakeVisible(frame);

    int lastTop = frame.getY();
    for (const int height : {200, 300, 400}) {
        contentPtr->setSize(280, height);
        EXPECT_TRUE(kScreen.contains(frame.getBounds().reduced(ModDotPanelFrame::kShadow - panel::kScreenMargin)))
            << "the window follows on screen at " << height;
        EXPECT_EQ(frame.getY() + frame.placement().tipY, dot.getCentreY())
            << "arrow tip level with the dot at " << height;
        EXPECT_LE(frame.getY(), lastTop);
        lastTop = frame.getY();
        EXPECT_EQ(contentPtr->getHeight(), height);
        EXPECT_EQ(frame.getY() + contentPtr->getY(), frame.getY() + ModDotPanelFrame::kShadow);
    }
}

TEST(ModDotPanelFrameTest, OnlyThePanelAndItsArrowTakeClicks) {
    juce::Component screen;
    screen.setBounds(kScreen);
    juce::Component anchor;
    auto content = std::make_unique<juce::Component>();
    content->setSize(280, 200);
    ModDotPanelFrame frame(std::move(content), anchor, juce::Rectangle<int>(100, 200, 12, 12), kScreen);
    screen.addAndMakeVisible(frame);

    const auto& p = frame.placement();
    EXPECT_TRUE(frame.hitTest(p.panel.getCentreX(), p.panel.getCentreY()));
    EXPECT_TRUE(frame.hitTest(p.tipX + 2, p.tipY)) << "the arrow is part of the shape";
    EXPECT_FALSE(frame.hitTest(1, 1)) << "the shadow margin lets clicks through";
}

TEST(ModDotPanelFrameTest, ClosingReportsOnceItIsGone) {
    juce::Component anchor;
    auto content = std::make_unique<juce::Component>();
    content->setSize(280, 100);
    ModDotPanelFrame frame(std::move(content), anchor, juce::Rectangle<int>(100, 200, 12, 12), kScreen);
    int closed = 0;
    frame.onClosed = [&] { ++closed; };

    frame.close();
    frame.close();

    EXPECT_EQ(closed, 1) << "a second close while closing does nothing";
    EXPECT_TRUE(frame.isClosing());
}
