#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../Layout/FadeVisibilityTestGuard.h"
#include "AudioEngine/AudioEngine.h"
#include "UI/Chrome/WelcomeScreenComponent.h"

// Topic: the Welcome screen reopened over an open project. It shows on a dimmed scrim with the project
// beneath, and Escape, the close button or a click outside the card hide it with the project untouched. The
// launch screen (no project yet) is opaque and has no way out.

namespace {

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }

// A synthesized left-button event at `pos` (the overlay's own coordinates), delivered through the Component
// interface like the real one.
juce::MouseEvent mouseEventAt(juce::Component& c, juce::Point<int> pos) {
    const auto p = pos.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c,
                            &c, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false);
}

class WelcomeDismissTest : public MainComponentTest {
protected:
    void open(MainComponent& mc) {
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        welcome = mc.getWelcomeScreenForTest();
        ASSERT_NE(welcome, nullptr);
    }
    // The project is open: the launch screen is gone and the welcome screen is reopened over it.
    void reopenOverProject(MainComponent& mc) {
        open(mc);
        mc.hideWelcomeScreenForTest();
        mc.showWelcomeScreenForTest();
        ASSERT_TRUE(welcome->isVisible());
    }
    juce::Point<int> outsideCard() const {
        return welcome->getCardBoundsForTest().getTopLeft() - juce::Point<int>(10, 10);
    }

    synth::ui::WelcomeScreenComponent* welcome = nullptr;
};

} // namespace

TEST_F(WelcomeDismissTest, LaunchScreenIsOpaqueAndNothingDismissesIt) {
    MainComponent mc(std::make_unique<MockProvider>());
    open(mc);
    EXPECT_FALSE(welcome->isDismissible());
    EXPECT_TRUE(welcome->isOpaque());
    EXPECT_FALSE(welcome->getCloseButtonForTest().isVisible());

    EXPECT_FALSE(welcome->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    welcome->mouseDown(mouseEventAt(*welcome, outsideCard()));
    pump();
    EXPECT_FALSE(mc.isWelcomeScreenHiddenForTest());
}

TEST_F(WelcomeDismissTest, ReopenedOverAProjectIsAClosableScrim) {
    MainComponent mc(std::make_unique<MockProvider>());
    reopenOverProject(mc);
    EXPECT_TRUE(welcome->isDismissible());
    EXPECT_FALSE(welcome->isOpaque()) << "the project must paint through the scrim";
    EXPECT_TRUE(welcome->getCloseButtonForTest().isVisible());
}

TEST_F(WelcomeDismissTest, EscapeClosesItAndLeavesTheProjectUntouched) {
    MainComponent mc(std::make_unique<MockProvider>());
    open(mc);
    mc.hideWelcomeScreenForTest();
    mc.simulateAddMidiTrackClick();
    pump();
    const int nodes = mc.getAudioEngine().getGraph().getNumNodes();
    const bool dirty = mc.isProjectDirty();
    ASSERT_TRUE(dirty);

    mc.showWelcomeScreenForTest();
    EXPECT_TRUE(welcome->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(mc.isWelcomeScreenHiddenForTest());
    EXPECT_FALSE(welcome->isVisible());
    EXPECT_EQ(mc.getAudioEngine().getGraph().getNumNodes(), nodes);
    EXPECT_TRUE(mc.isProjectDirty()) << "closing must not save, discard or reset anything";
}

TEST_F(WelcomeDismissTest, ClickOutsideTheCardClosesItButAClickOnTheCardDoesNot) {
    MainComponent mc(std::make_unique<MockProvider>());
    reopenOverProject(mc);

    welcome->mouseDown(mouseEventAt(*welcome, welcome->getCardBoundsForTest().getCentre()));
    EXPECT_FALSE(mc.isWelcomeScreenHiddenForTest()) << "a click on the card itself is not a dismissal";

    welcome->mouseDown(mouseEventAt(*welcome, outsideCard()));
    EXPECT_TRUE(mc.isWelcomeScreenHiddenForTest());
    EXPECT_FALSE(welcome->isVisible());
}

TEST_F(WelcomeDismissTest, CloseButtonIsAccessibleAndWorksFromTheKeyboard) {
    MainComponent mc(std::make_unique<MockProvider>());
    reopenOverProject(mc);
    auto& close = welcome->getCloseButtonForTest();

    EXPECT_TRUE(close.getWantsKeyboardFocus()) << "a Tab stop";
    EXPECT_TRUE(close.getTitle().isNotEmpty());
    EXPECT_TRUE(close.getTooltip().isNotEmpty());
    const auto card = welcome->getCardBoundsForTest();
    EXPECT_TRUE(card.contains(close.getBounds())) << "inside the card";
    EXPECT_GT(close.getX(), card.getCentreX()) << "top-right";
    EXPECT_LT(close.getY(), card.getY() + 40);

    // juce::Button turns Space and Return on a focused button into this same click.
    close.triggerClick();
    EXPECT_TRUE(mc.isWelcomeScreenHiddenForTest());
}

TEST_F(WelcomeDismissTest, CloseButtonClickDismissesWithTheProjectStillThere) {
    MainComponent mc(std::make_unique<MockProvider>());
    reopenOverProject(mc);
    welcome->getCloseButtonForTest().triggerClick();
    EXPECT_TRUE(mc.isWelcomeScreenHiddenForTest());
}

TEST_F(WelcomeDismissTest, DismissFadesOutThenHides) {
    MainComponent mc(std::make_unique<MockProvider>());
    reopenOverProject(mc);
    FadeAnimateGuard guard;
    welcome->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    EXPECT_TRUE(welcome->isVisible()) << "stays on screen while it fades out";
    EXPECT_TRUE(mc.isWelcomeScreenHiddenForTest());
    synth::ui::FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(welcome->getAlpha(), 0.5f, 0.01f);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(welcome->isVisible());
}
