// Where the mod dot's panel goes: beside the dot, slid up near the bottom of the screen with the arrow tip level with
// the dot's centre the whole way, one outline for body and arrow, and the frame that follows the panel's growth.
// docs/modules/modulation.md#the-mod-dot-menu.

#include "UI/Graph/ModDot/ModDotPanelFrame.h"
#include "UI/Graph/ModDot/ModDotPanelGeometry.h"
#include "UI/Layout/PopupMotion.h"
#include <functional>
#include <gtest/gtest.h>

namespace {

namespace panel = synth::ui::modDotPanel;
using synth::ui::ModDotPanelFrame;

const juce::Rectangle<int> kScreen(0, 0, 800, 600);
const juce::ModifierKeys kPlainMouse(juce::ModifierKeys::leftButtonModifier);

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

namespace {

struct AnimateOffScreenGuard {
    AnimateOffScreenGuard() { synth::ui::PopupMotion::setAnimateOffScreenForTest(true); }
    ~AnimateOffScreenGuard() { synth::ui::PopupMotion::setAnimateOffScreenForTest(false); }
};

bool pumpUntil(const std::function<bool()>& done) {
    const auto deadline = juce::Time::getMillisecondCounter() + 2000;
    while (!done() && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    return done();
}

struct MotionFrame {
    juce::Component anchor;
    std::unique_ptr<ModDotPanelFrame> frame;
    int closed = 0;
    MotionFrame() {
        auto content = std::make_unique<juce::Component>();
        content->setSize(280, 100);
        frame = std::make_unique<ModDotPanelFrame>(std::move(content), anchor, juce::Rectangle<int>(100, 200, 12, 12),
                                                   kScreen);
        frame->onClosed = [this] { ++closed; };
        synth::ui::PopupMotion::attach(*frame, frame->motionStyle());
        frame->setVisible(true);
    }
};

} // namespace

TEST(ModDotPanelFrameTest, MotionGrowsOutOfTheDotWithASoftOvershoot) {
    juce::Component anchor;
    auto content = std::make_unique<juce::Component>();
    content->setSize(280, 100);
    ModDotPanelFrame frame(std::move(content), anchor, juce::Rectangle<int>(100, 200, 12, 12), kScreen);
    const auto style = frame.motionStyle();
    EXPECT_TRUE(style.overshoot);
    EXPECT_FLOAT_EQ(style.inStartScale, 0.4f);
    EXPECT_FLOAT_EQ(style.outEndScale, 0.5f);
    EXPECT_DOUBLE_EQ(style.inMs, 200.0);
    EXPECT_DOUBLE_EQ(style.outMs, 140.0);
    EXPECT_FLOAT_EQ(style.inSlidePx, 0.0f) << "the scale moves the panel; the window itself stays put";
    ASSERT_TRUE(style.anchor != nullptr);
    EXPECT_EQ(style.anchor(), juce::Point<int>(106, 206)) << "the dot's centre, not the pointer";
    ASSERT_TRUE(style.body != nullptr);
    EXPECT_EQ(style.body(), &frame.body());
    ASSERT_TRUE(style.bodyPivot != nullptr);
    EXPECT_EQ(style.bodyPivot(), (juce::Point<int>(106, 206) - frame.getPosition()).toFloat())
        << "scales about the dot's centre in the frame's own coordinates";
}

TEST(ModDotPanelFrameTest, EverythingTheWindowDrawsLivesInOneBodyThatFillsTheFrame) {
    juce::Component anchor;
    auto content = std::make_unique<juce::Component>();
    auto* contentPtr = content.get();
    contentPtr->setSize(280, 100);
    ModDotPanelFrame frame(std::move(content), anchor, juce::Rectangle<int>(100, 200, 12, 12), kScreen);
    EXPECT_EQ(frame.body().getBounds(), frame.getLocalBounds());
    EXPECT_TRUE(frame.body().isParentOf(contentPtr)) << "the panel scales with its outline";
    contentPtr->setSize(280, 300);
    EXPECT_EQ(frame.body().getBounds(), frame.getLocalBounds()) << "the body follows the frame's growth";
    const auto& p = frame.placement();
    EXPECT_TRUE(frame.body().hitTest(p.tipX + 2, p.tipY));
    EXPECT_FALSE(frame.body().hitTest(1, 1));
    EXPECT_FALSE(frame.body().isTransformed()) << "at rest nothing is scaled";
}

TEST(ModDotPanelFrameTest, TheLeavingShrinkEndsWithTheBodyBackAtFullSize) {
    AnimateOffScreenGuard seam;
    MotionFrame m;
    m.frame->close();
    ASSERT_TRUE(pumpUntil([&] { return m.closed == 1; }));
    pumpUntil([] { return false; });
    EXPECT_FALSE(m.frame->body().isTransformed()) << "a panel that lives on after a vetoed close is whole";
}

TEST(ModDotPanelFrameTest, EscapeFadesTheLiveWindowOutThenClosesAndTheDotStaysReachable) {
    AnimateOffScreenGuard seam;
    MotionFrame m;
    EXPECT_TRUE(m.frame->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(m.frame->isClosing());
    EXPECT_EQ(m.closed, 0) << "the window is still there while it fades";
    EXPECT_TRUE(synth::ui::PopupMotion::isDismissing(*m.frame));
    bool frameClicks = true, frameChildClicks = true, anchorClicks = false, anchorChildClicks = false;
    m.frame->getInterceptsMouseClicks(frameClicks, frameChildClicks);
    m.anchor.getInterceptsMouseClicks(anchorClicks, anchorChildClicks);
    EXPECT_FALSE(frameClicks) << "a fading panel takes no clicks";
    EXPECT_TRUE(anchorClicks) << "the dot keeps taking clicks";
    ASSERT_TRUE(pumpUntil([&] { return m.closed == 1; }));
    EXPECT_FALSE(m.frame->isVisible());
}

TEST(ModDotPanelFrameTest, ClickingTheDotAgainMidFadeCutsTheFadeShortAndClosesOnce) {
    AnimateOffScreenGuard seam;
    MotionFrame m;
    m.frame->close();
    ASSERT_TRUE(m.frame->isClosing());
    m.frame->finishClosingNow();
    EXPECT_FALSE(m.frame->isVisible());
    ASSERT_TRUE(pumpUntil([&] { return m.closed >= 1; }));
    pumpUntil([] { return false; }); // let any second close show up
    EXPECT_EQ(m.closed, 1);
}

TEST(ModDotPanelFrameTest, ClosesAtOnceWhenNoWindowIsOnScreen) {
    MotionFrame m; // no animate-off-screen seam: the headless contract
    m.frame->close();
    EXPECT_EQ(m.closed, 1) << "final state is there before close() returns";
}

// A press on an empty canvas (another top-level component) closes the panel through ONE fade: the window stays shown,
// only gets more transparent, and nothing re-shows or pictures it again.
TEST(ModDotPanelFrameTest, ClickingEmptyCanvasFadesOutExactlyOnceWithoutAReshow) {
    AnimateOffScreenGuard seam;
    MotionFrame m;
    juce::Component canvas;
    canvas.setBounds(0, 0, 400, 300);
    int shown = 0;
    struct Watch : juce::ComponentListener {
        int* count;
        void componentVisibilityChanged(juce::Component& c) override {
            if (c.isVisible())
                ++*count;
        }
    } watch;
    watch.count = &shown;
    m.frame->addComponentListener(&watch);
    // Let the intro fade finish first: a slow runner can still be mid fade-in at the press.
    ASSERT_TRUE(pumpUntil([&] { return m.frame->getAlpha() >= 0.999f; }));

    const juce::MouseEvent press(juce::Desktop::getInstance().getMainMouseSource(), {50, 50}, kPlainMouse, 0.0f, 0.0f,
                                 0.0f, 0.0f, 0.0f, &canvas, &canvas, juce::Time::getCurrentTime(), {50, 50},
                                 juce::Time::getCurrentTime(), 1, false);
    static_cast<juce::MouseListener&>(*m.frame).mouseDown(press);
    static_cast<juce::MouseListener&>(*m.frame).mouseDown(press); // a second press while fading changes nothing

    EXPECT_TRUE(m.frame->isClosing());
    EXPECT_TRUE(synth::ui::PopupMotion::isDismissing(*m.frame));
    float lastAlpha = 1.0f;
    bool monotonic = true;
    ASSERT_TRUE(pumpUntil([&] {
        monotonic = monotonic && m.frame->getAlpha() <= lastAlpha + 1e-4f;
        lastAlpha = m.frame->getAlpha();
        return m.closed >= 1;
    }));
    EXPECT_TRUE(monotonic) << "the alpha only goes down";
    EXPECT_EQ(shown, 0) << "nothing re-shows the window";
    EXPECT_EQ(synth::ui::PopupMotion::getNumLeavingGhosts(), 0) << "no second, leaving picture on top of the fade";
    pumpUntil([] { return false; });
    EXPECT_EQ(m.closed, 1);
    m.frame->removeComponentListener(&watch);
}
